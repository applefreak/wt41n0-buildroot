/*
 * Scanner: barcodes from the ring scanner. Runs wt41n0-scan in the background (trigger
 * mode, -v for its state and the barcode types) and lists what it reads, newest first.
 * SCAN starts a read without the trigger (wt41n0-scan -s). ENT on a scan shows it whole,
 * with DELETE (ENT arms it, a second ENT within 5 s deletes, like Power).
 *
 * The scans are kept in /tmp/wt41n0-scans ("time<TAB>type<TAB>code" per line) until the
 * unit restarts; the launcher's readout shows the count and the last one from there.
 * Without a scanner plugged in, wt41n0-scan gives up after 5 s; it is retried every 3 s.
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "wt41n0ui.h"

#define CY(y)		((y) - WT_CONTENT_Y)
#define MAX_SCANS	40
#define CODE_MAX	256
#define RETRY_MS	3000
#define HINT_SCAN	"\xE2\x86\x91\xE2\x86\x93 SELECT|ENT SCAN|ESC BACK"
#define HINT_ROW	"\xE2\x86\x91\xE2\x86\x93 SELECT|ENT OPEN|ESC BACK"

struct scan {
	time_t when;
	int type;		/* SSI code type, -1 unknown */
	char code[CODE_MAX];
};

static struct scan scans[MAX_SCANS];	/* newest first */
static int nscans, total, fresh = -1;	/* fresh: index of this session's newest */

static enum { S_STARTING, S_READY, S_SCANNING, S_ABSENT, S_ERROR } state;
static char detail[96];

static struct {
	pid_t pid;
	int out, err;		/* read ends, non-blocking */
	bool soft;
	char obuf[CODE_MAX + 2], ebuf[256];
	int olen, elen;
	int types[8], ntypes;	/* "barcode type" lines (stderr) not yet paired with a code (stdout) */
} child = { .pid = -1, .out = -1, .err = -1 };

static lv_obj_t *state_label, *detail_label, *card_icon, *scan_btn;
static lv_timer_t *poll_timer;
static uint32_t retry_at;
static int list_focus = -1;		/* -1: SCAN */
static char marker[16];
static const char *scan_cmd;

static void list_screen(void);

/* ---- names ------------------------------------------------------------------------ */

static const char *type_name(int t, char *buf, size_t len)
{
	static const char *const names[] = {
		[0x01] = "CODE 39", [0x02] = "CODABAR", [0x03] = "CODE 128", [0x04] = "D 2OF5",
		[0x05] = "IATA", [0x06] = "I 2OF5", [0x07] = "CODE 93", [0x08] = "UPC-A",
		[0x09] = "UPC-E", [0x0A] = "EAN-8", [0x0B] = "EAN-13", [0x0C] = "CODE 11",
		[0x0E] = "MSI", [0x0F] = "GS1-128", [0x10] = "UPC-E1",
	};

	if (t > 0 && t < (int)(sizeof(names) / sizeof(names[0])) && names[t])
		return names[t];
	if (t < 0)
		return "?";
	snprintf(buf, len, "TYPE %02X", t);
	return buf;
}

/* ---- the history file ------------------------------------------------------------- */

static const char *history_path(void)
{
	const char *p = getenv("WT41N0_SCANS");

	return p ? p : "/tmp/wt41n0-scans";
}

static void push(time_t when, int type, const char *code)
{
	if (nscans < MAX_SCANS)
		nscans++;
	memmove(&scans[1], &scans[0], sizeof(scans[0]) * (nscans - 1));
	scans[0].when = when;
	scans[0].type = type;
	snprintf(scans[0].code, sizeof(scans[0].code), "%s", code);
	total++;
}

static void history_load(void)
{
	FILE *f = fopen(history_path(), "r");
	char line[CODE_MAX + 64];

	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		char *t1 = strchr(line, '\t'), *t2 = t1 ? strchr(t1 + 1, '\t') : NULL;

		if (!t2)
			continue;
		line[strcspn(line, "\n")] = 0;
		push((time_t)atoll(line), atoi(t1 + 1), t2 + 1);
	}
	fclose(f);
}

static void history_add(int type, const char *code)
{
	FILE *f = fopen(history_path(), "a");
	time_t now = time(NULL);

	push(now, type, code);
	fresh = 0;				/* the newest, tagged NEW */
	if (f) {
		fprintf(f, "%lld\t%d\t%s\n", (long long)now, type, code);
		fclose(f);
	}
}

/* drop scan i from the list and from the file (its last matching line) */
static void history_delete(int i)
{
	struct scan gone = scans[i];
	const char *path = history_path();
	char tmp[256], line[CODE_MAX + 64], want[CODE_MAX + 64];
	FILE *in, *out;
	long n = 0, match = -1;

	memmove(&scans[i], &scans[i + 1], sizeof(scans[0]) * (nscans - i - 1));
	nscans--;
	total--;
	if (fresh == i)
		fresh = -1;
	else if (fresh > i)
		fresh--;

	snprintf(want, sizeof(want), "%lld\t%d\t%s\n", (long long)gone.when, gone.type, gone.code);
	in = fopen(path, "r");
	if (!in)
		return;
	while (fgets(line, sizeof(line), in)) {
		if (!strcmp(line, want))
			match = n;
		n++;
	}
	snprintf(tmp, sizeof(tmp), "%s.new", path);
	out = fopen(tmp, "w");
	if (!out) {
		fclose(in);
		return;
	}
	rewind(in);
	for (n = 0; fgets(line, sizeof(line), in); n++)
		if (n != match)
			fputs(line, out);
	fclose(in);
	if (fclose(out) == 0)
		rename(tmp, path);
}

/* ---- wt41n0-scan in the background ------------------------------------------------ */

static void child_spawn(bool soft)
{
	int out[2], err[2];
	pid_t pid;

	if (pipe(out))
		return;
	if (pipe(err)) {
		close(out[0]);
		close(out[1]);
		return;
	}
	pid = fork();
	if (pid == 0) {
#ifdef __linux__
		prctl(PR_SET_PDEATHSIG, SIGTERM);	/* never outlive the app */
#endif
		dup2(out[1], 1);
		dup2(err[1], 2);
		close(out[0]); close(out[1]); close(err[0]); close(err[1]);
		if (soft)
			execl(scan_cmd, scan_cmd, "-v", "-s", (char *)NULL);
		else
			execl(scan_cmd, scan_cmd, "-v", (char *)NULL);
		fprintf(stderr, "wt41n0-scan: cannot run %s\n", scan_cmd);
		_exit(127);
	}
	close(out[1]);
	close(err[1]);
	if (pid < 0) {
		close(out[0]);
		close(err[0]);
		return;
	}
	fcntl(out[0], F_SETFL, O_NONBLOCK);
	fcntl(err[0], F_SETFL, O_NONBLOCK);
	child.pid = pid;
	child.out = out[0];
	child.err = err[0];
	child.soft = soft;
	child.olen = child.elen = child.ntypes = 0;
}

/* stop it and wait: it switches the accessory port off on SIGTERM */
static void child_stop(void)
{
	if (child.pid > 0) {
		kill(child.pid, SIGTERM);
		waitpid(child.pid, NULL, 0);
	}
	if (child.out >= 0)
		close(child.out);
	if (child.err >= 0)
		close(child.err);
	child.pid = -1;
	child.out = child.err = -1;
}

static void on_term(int sig)
{
	(void)sig;
	if (child.pid > 0)
		kill(child.pid, SIGTERM);
	_exit(0);
}

/* ---- state -------------------------------------------------------------------------- */

static void show_state(void)
{
	static const char *const words[] = { "STARTING", "READY", "SCANNING", "NOT CONNECTED", "ERROR" };
	const ui_palette_t *p = ui_palette();
	bool bad = state == S_ABSENT || state == S_ERROR;

	if (!state_label)
		return;
	lv_label_set_text(state_label, words[state]);
	lv_obj_set_style_text_color(state_label, bad ? p->warn : state == S_READY ? p->text : p->accent, 0);
	lv_label_set_text(detail_label, detail);
	ui_icon_color(card_icon, bad ? p->warn : p->accent);
	ui_button_set_disabled(scan_btn, state == S_SCANNING || child.soft);
}

static void set_state(int s, const char *d)
{
	state = s;
	snprintf(detail, sizeof(detail), "%s", d);
	show_state();
}

static void got_code(const char *code)
{
	int type = -1;

	if (!*code)
		return;
	if (child.ntypes) {		/* wt41n0-scan prints the type before the code */
		type = child.types[0];
		memmove(&child.types[0], &child.types[1], sizeof(int) * --child.ntypes);
	}
	history_add(type, code);
	if (child.soft)
		set_state(S_SCANNING, "GOT IT");
	if (!state_label)
		return;				/* on a scan's own screen: the list shows it later */
	ui_screen_transition(UI_SLIDE_NONE);
	if (list_focus >= 0 && list_focus < MAX_SCANS - 1)
		list_focus++;			/* the same scan stays focused */
	list_screen();
}

static void got_message(char *line)
{
	if (line[0] == '<' || line[0] == '>')
		return;				/* SSI packet dumps */
	if (!strncmp(line, "scanner: ", 9)) {
		if (child.soft) {
			set_state(S_SCANNING, "AIM AT A BARCODE");
		} else {
			set_state(S_READY, "PULL THE TRIGGER");
		}
	} else if (!strncmp(line, "barcode type 0x", 15)) {
		if (child.ntypes < (int)(sizeof(child.types) / sizeof(child.types[0])))
			child.types[child.ntypes++] = (int)strtol(line + 15, NULL, 16);
	} else if (!strncmp(line, "wt41n0-scan: ", 13)) {
		char *m = line + 13;

		for (char *c = m; *c; c++)
			if (*c >= 'a' && *c <= 'z')
				*c -= 32;
		if (strstr(line, "NO ANSWER FROM THE RING SCANNER"))
			set_state(S_ABSENT, "PLUG IT INTO A PORT");
		else if (strstr(line, "NO BARCODE READ"))
			set_state(S_READY, "NOTHING READ IN 10 S");
		else
			set_state(S_ERROR, m);
	}
}

/* split what arrived into lines */
static void drain(int fd, char *buf, int *len, int size, void (*line)(char *))
{
	char tmp[256];
	ssize_t n;
	int i;

	while ((n = read(fd, tmp, sizeof(tmp))) > 0) {
		for (i = 0; i < n; i++) {
			if (tmp[i] == '\n') {
				buf[*len] = 0;
				line(buf);
				*len = 0;
			} else if (*len < size - 1) {
				buf[(*len)++] = tmp[i];
			}
		}
	}
}

static void code_line(char *l) { got_code(l); }

static void poll_cb(lv_timer_t *t)
{
	int status;

	(void)t;
	if (child.pid > 0) {
		drain(child.err, child.ebuf, &child.elen, sizeof(child.ebuf), got_message);
		drain(child.out, child.obuf, &child.olen, sizeof(child.obuf), code_line);
		if (waitpid(child.pid, &status, WNOHANG) == child.pid) {
			bool soft = child.soft;

			drain(child.err, child.ebuf, &child.elen, sizeof(child.ebuf), got_message);
			drain(child.out, child.obuf, &child.olen, sizeof(child.obuf), code_line);
			child.pid = -1;
			child_stop();
			if (soft) {			/* back to the trigger */
				child_spawn(false);
				if (state == S_SCANNING)
					set_state(S_STARTING, "BACK TO THE TRIGGER");
				show_state();
			} else {
				if (state != S_ABSENT && state != S_ERROR)
					set_state(S_ERROR, "THE READER STOPPED");
				retry_at = lv_tick_get() + RETRY_MS;
			}
		}
	} else if (lv_tick_get() - retry_at < 0x80000000u) {
		child_spawn(false);
		if (state != S_ABSENT)
			set_state(S_STARTING, "POWERING THE PORT");
	}
}

/* ---- screens ---------------------------------------------------------------------- */

static void quit(void)
{
	if (poll_timer)
		lv_timer_delete(poll_timer);
	poll_timer = NULL;
	child_stop();
	ui_quit(0);
}

static lv_obj_t *text(lv_obj_t *parent, ui_role_t role, lv_color_t c, const char *s)
{
	lv_obj_t *l = lv_label_create(parent);

	lv_label_set_text(l, s);
	ui_text_role(l, role);
	lv_obj_set_style_text_color(l, c, 0);
	return l;
}

static void back_to_list(void)
{
	list_screen();
}

static void delete_scan(void *data)
{
	int i = (int)(intptr_t)data;

	history_delete(i);
	if (list_focus >= nscans)
		list_focus = nscans - 1;
	ui_screen_transition(UI_SLIDE_OUT);
	list_screen();
}

static void detail_screen(int i)
{
	const ui_palette_t *p = ui_palette();
	struct scan *s = &scans[i];
	char title[32], info[96], tbuf[16], when[32];
	lv_obj_t *c, *card, *l;

	snprintf(title, sizeof(title), "SCANNER/#%02d", total - i);
	c = ui_screen(title);
	ui_on_back(back_to_list);
	strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", localtime(&s->when));
	snprintf(info, sizeof(info), "%s // %d CHARACTERS // %s", type_name(s->type, tbuf, sizeof(tbuf)),
		 (int)strlen(s->code), when);
	l = text(c, UI_ROLE_DATA, p->dim, info);
	lv_obj_set_pos(l, WT_INNER_X, CY(29));
	card = ui_card(c, WT_INNER_X, WT_FIELD_Y, WT_INNER_W, 146 - 4 - WT_FIELD_Y);
	l = text(card, UI_ROLE_KEY, p->text, s->code);
	lv_obj_set_style_text_letter_space(l, 1, 0);
	lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(l, WT_INNER_W - 20);
	lv_obj_set_height(l, 146 - 4 - WT_FIELD_Y - 18);	/* four lines; longer codes are cut */
	lv_obj_set_pos(l, 9, 9);
	lv_group_focus_obj(ui_big_button(c, 146, "trash", "DELETE", "REMOVES THIS SCAN FROM THE LIST", "",
					 "ENT DELETE|ESC BACK", delete_scan, (void *)(intptr_t)i));
	ui_hint_keys("ENT DELETE|ESC BACK", "");
}

static void row_event(lv_event_t *e)
{
	int i = (int)(intptr_t)lv_event_get_user_data(e);

	if (lv_event_get_code(e) == LV_EVENT_FOCUSED) {
		list_focus = i;
		ui_hint_keys(HINT_ROW, marker);
	} else if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
		detail_screen(i);
	}
}

static void scan_event(lv_event_t *e)
{
	if (lv_event_get_code(e) == LV_EVENT_FOCUSED) {
		list_focus = -1;
		ui_hint_keys(ui_button_disabled(scan_btn) ? "\xE2\x86\x91\xE2\x86\x93 SELECT|ESC BACK" : HINT_SCAN, marker);
		return;
	}
	if (lv_event_get_code(e) != LV_EVENT_CLICKED || ui_button_disabled(scan_btn))
		return;
	child_stop();				/* the trigger reader holds the port */
	child_spawn(true);
	set_state(S_SCANNING, "WAKING THE SCANNER");
}

static void list_deleted(lv_event_t *e)
{
	(void)e;
	state_label = NULL;
}

static void list_screen(void)
{
	const ui_palette_t *p = ui_palette();
	lv_obj_t *c, *card, *l, *list, *row;
	char time_s[16], value[40], tbuf[16];
	int i;

	c = ui_screen("SCANNER");
	ui_hint_keys(HINT_SCAN, "");
	ui_on_back(quit);

	/* state card: RING chip, state, what to do; SCAN beside it */
	card = ui_card(c, WT_INNER_X, WT_INNER_Y, 236, WT_CARD_H);
	l = ui_chip(card, UI_ROLE_DATA, "RING");
	lv_obj_align(l, LV_ALIGN_LEFT_MID, 7, 0);
	lv_obj_update_layout(l);
	state_label = text(card, UI_ROLE_DATA, p->text, "");
	lv_obj_set_pos(state_label, lv_obj_get_x2(l) + 9, 4);
	detail_label = text(card, UI_ROLE_DATA, p->dim, "");
	lv_obj_set_pos(detail_label, lv_obj_get_x2(l) + 9, 19);
	card_icon = ui_icon_sized(card, "scanner", 18);
	lv_obj_align(card_icon, LV_ALIGN_RIGHT_MID, -6, 0);
	lv_obj_add_event_cb(card, list_deleted, LV_EVENT_DELETE, NULL);
	scan_btn = ui_button(c, 250, WT_INNER_Y, 54, 40, "scanner", "SCAN");
	lv_obj_add_event_cb(scan_btn, scan_event, LV_EVENT_ALL, NULL);

	list = ui_rows_create(c, 70, WT_INNER_Y + WT_INNER_H - 70, true);
	for (i = 0; i < nscans; i++) {
		strftime(time_s, sizeof(time_s), "%H:%M", localtime(&scans[i].when));
		snprintf(value, sizeof(value), "%s  %s", type_name(scans[i].type, tbuf, sizeof(tbuf)), time_s);
		row = ui_row_ex(list, NULL, scans[i].code, value, UI_ROW_NO_NUMBER);
		if (i == fresh)
			ui_row_tag(row, "NEW", p->accent2);
		lv_obj_add_event_cb(row, row_event, LV_EVENT_ALL, (void *)(intptr_t)i);
	}
	snprintf(marker, sizeof(marker), "%d SCAN%s", total, total == 1 ? "" : "S");
	ui_rows_marker(list, marker);
	ui_rows_done(list, list_focus >= 0 && list_focus < nscans ? list_focus : 0);
	if (!nscans) {
		l = text(c, UI_ROLE_HINT, p->dim, "NO SCANS YET\nPULL THE RING SCANNER'S TRIGGER, OR ENT ON SCAN");
		lv_obj_set_pos(l, WT_INNER_X + 8, CY(84));
	}
	if (list_focus < 0 || !nscans)
		lv_group_focus_obj(scan_btn);
	ui_hint_marker(marker);
	show_state();
}

int main(void)
{
	scan_cmd = getenv("WT41N0_SCAN_CMD");
	if (!scan_cmd)
		scan_cmd = "/usr/bin/wt41n0-scan";
	signal(SIGTERM, on_term);
	signal(SIGPIPE, SIG_IGN);
	ui_init();
	nscans = total = 0;
	fresh = -1;
	history_load();
	set_state(S_STARTING, "POWERING THE PORT");
	list_screen();
	child_spawn(false);
	poll_timer = lv_timer_create(poll_cb, 50, NULL);
	return ui_run();
}
