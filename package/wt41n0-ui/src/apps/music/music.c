/*
 * Music: a client for MPD (/etc/mpd.conf, socket /run/mpd.socket).
 *
 * NOW PLAYING (the start screen): the song card (state, title, artist, album or
 * station, progress, volume), then PREV / PLAY / NEXT / VOL- / VOL+, then QUEUE,
 * LIBRARY and RADIO. Number keys reach them directly: 4 5 6 previous / play-pause /
 * next, 7 8 volume down / up, 1 2 3 the other screens. The state is polled every 500 ms.
 *
 * QUEUE: the play queue, the current song tagged; ENT plays one, BKSP removes it.
 * LIBRARY: the folders of /root/Music; ENT on a folder opens it, on a song plays the
 * folder from that song; PLAY ALL plays the folder and its subfolders; UPDATE rescans.
 * RADIO: the stations (the stored playlist "Radio"), the other stored playlists, and
 * ADD STATION, which takes a stream URL.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wt41n0ui.h"
#include "mpdc.h"

#define CY(y)		((y) - WT_CONTENT_Y)
#define POLL_MS		500
#define VOL_STEP	5
#define NOW_HINT	"5 PLAY|4/6 SKIP|7/8 VOL|ESC BACK"
#define CARD_H		104
#define ROW1_Y		(WT_INNER_Y + CARD_H + 6)
#define ROW2_Y		(ROW1_Y + 40 + 6)

enum { B_PREV, B_PLAY, B_NEXT, B_VOLDN, B_VOLUP, B_QUEUE, B_LIBRARY, B_RADIO, B_COUNT };

static struct mpdc_status st;
static struct mpdc_item items[MPDC_MAX_ITEMS];
static int nitems;
static lv_timer_t *poll_timer;

/* NOW PLAYING's live parts (NULL while another screen shows) */
static lv_obj_t *state_l, *title_l, *artist_l, *album_l, *time_l, *vol_l, *bar, *buttons[B_COUNT];
static int now_focus = B_PLAY;

static char path[256];			/* LIBRARY's folder */
static int list_focus;
static char marker[24];
static lv_obj_t *url_field;

static void now_screen(void);
static void queue_screen(void);
static void library_screen(void);
static void radio_screen(void);

/* ---- helpers ------------------------------------------------------------------------ */

static void upper(char *s)
{
	for (; *s; s++)
		if (*s >= 'a' && *s <= 'z')
			*s -= 32;
}

static lv_obj_t *text(lv_obj_t *parent, ui_role_t role, lv_color_t c, const char *s)
{
	lv_obj_t *l = lv_label_create(parent);

	lv_label_set_text(l, s);
	ui_text_role(l, role);
	lv_obj_set_style_text_color(l, c, 0);
	return l;
}

static void clock_text(char *buf, size_t len, unsigned s)
{
	if (s >= 3600)
		snprintf(buf, len, "%u:%02u:%02u", s / 3600, s / 60 % 60, s % 60);
	else
		snprintf(buf, len, "%u:%02u", s / 60, s % 60);
}

static void quit(void)
{
	if (poll_timer)
		lv_timer_delete(poll_timer);
	poll_timer = NULL;
	ui_quit(0);
}

/* a screen with only a message when MPD cannot be reached */
static void offline_status(lv_obj_t *c)
{
	char detail[96];

	snprintf(detail, sizeof(detail), "%s", *mpdc_error() ? mpdc_error() : "IS IT RUNNING?");
	ui_status_line(c, UI_TONE_WARN, "warning", "NO MUSIC SERVER", detail);
}

/* ---- NOW PLAYING ---------------------------------------------------------------------- */

/* the PLAY button's icon and word follow the state */
static void play_button(bool playing)
{
	lv_obj_t *in, *icon;

	if (!buttons[B_PLAY])
		return;
	in = lv_obj_get_child(buttons[B_PLAY], -1);
	icon = lv_obj_get_child(in, 0);
	lv_label_set_text(lv_obj_get_child(icon, 0), playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
	lv_label_set_text(lv_obj_get_child(in, -1), playing ? "PAUSE" : "PLAY");
}

static void show_status(void)
{
	static const char *const words[] = { "STOPPED", "PLAYING", "PAUSED" };
	const ui_palette_t *p = ui_palette();
	char buf[160], t1[16], t2[16];

	if (!state_l)
		return;
	if (st.state == MPDC_OFFLINE) {
		lv_label_set_text(state_l, "OFFLINE");
		lv_obj_set_style_text_color(state_l, p->warn, 0);
		lv_label_set_text(title_l, "NO MUSIC SERVER");
		lv_label_set_text(artist_l, *mpdc_error() ? mpdc_error() : "MPD IS NOT RUNNING");
		lv_label_set_text(album_l, "");
		lv_label_set_text(time_l, "");
		lv_label_set_text(vol_l, "");
		lv_bar_set_value(bar, 0, LV_ANIM_OFF);
		play_button(false);
		return;
	}
	lv_label_set_text(state_l, words[st.state]);
	lv_obj_set_style_text_color(state_l, st.state == MPDC_PLAY ? p->accent : p->dim, 0);
	if (st.song_id < 0) {
		lv_label_set_text(title_l, st.queue_len ? "READY" : "QUEUE IS EMPTY");
		lv_label_set_text(artist_l, st.queue_len ? "" : "2 LIBRARY OR 3 RADIO TO START");
		lv_label_set_text(album_l, "");
	} else {
		snprintf(buf, sizeof(buf), "%s", st.title);
		upper(buf);
		lv_label_set_text(title_l, buf);
		snprintf(buf, sizeof(buf), "%s", st.artist);
		upper(buf);
		lv_label_set_text(artist_l, buf);
		snprintf(buf, sizeof(buf), "%s", st.album);
		upper(buf);
		lv_label_set_text(album_l, buf);
	}
	clock_text(t1, sizeof(t1), st.elapsed);
	if (st.total) {
		clock_text(t2, sizeof(t2), st.total);
		snprintf(buf, sizeof(buf), "%s / %s", t1, t2);
		lv_bar_set_value(bar, (int)(st.elapsed * 1000 / st.total), LV_ANIM_OFF);
	} else {
		if (st.song_id >= 0)
			snprintf(buf, sizeof(buf), "%s  LIVE", t1);
		else
			buf[0] = 0;
		lv_bar_set_value(bar, 0, LV_ANIM_OFF);
	}
	lv_label_set_text(time_l, buf);
	if (st.volume >= 0)
		snprintf(buf, sizeof(buf), "VOL %d%%%s%s", st.volume, st.repeat ? "  RPT" : "", st.random ? "  RND" : "");
	else
		snprintf(buf, sizeof(buf), "%s%s", st.repeat ? "RPT" : "", st.random ? "  RND" : "");
	lv_label_set_text(vol_l, buf);
	play_button(st.state == MPDC_PLAY);
}

static void poll_cb(lv_timer_t *t)
{
	(void)t;
	mpdc_status(&st);
	show_status();
}

static void refresh(void)
{
	mpdc_status(&st);
	show_status();
}

static void act(int b)
{
	switch (b) {
	case B_PREV:	mpdc_prev(); break;
	case B_PLAY:	mpdc_toggle(); break;
	case B_NEXT:	mpdc_next(); break;
	case B_VOLDN:	mpdc_volume(-VOL_STEP); break;
	case B_VOLUP:	mpdc_volume(VOL_STEP); break;
	case B_QUEUE:	list_focus = st.song_pos > 0 ? st.song_pos : 0; queue_screen(); return;
	case B_LIBRARY:	path[0] = 0; list_focus = 0; library_screen(); return;
	case B_RADIO:	list_focus = 0; radio_screen(); return;
	}
	refresh();
}

static void button_event(lv_event_t *e)
{
	int b = (int)(intptr_t)lv_event_get_user_data(e);
	static const char keys[] = "456781" "23";	/* B_PREV..B_RADIO */

	switch (lv_event_get_code(e)) {
	case LV_EVENT_FOCUSED:
		now_focus = b;
		break;
	case LV_EVENT_CLICKED:
		act(b);
		break;
	case LV_EVENT_KEY: {
		uint32_t k = lv_event_get_key(e);
		const char *at = k < 128 ? strchr(keys, (int)k) : NULL;

		if (at && k) {
			now_focus = (int)(at - keys);
			lv_group_focus_obj(buttons[now_focus]);
			act(now_focus);
		}
		break;
	}
	default:
		break;
	}
}

static void now_deleted(lv_event_t *e)
{
	(void)e;
	state_l = NULL;
	memset(buttons, 0, sizeof(buttons));
}

static void now_screen(void)
{
	static const char *const icons[B_COUNT] = { "prev", "play", "next", "minus", "plus", "list", "folder", "audio" };
	static const char *const names[B_COUNT] = { "PREV", "PLAY", "NEXT", "VOL-", "VOL+", "QUEUE", "LIBRARY", "RADIO" };
	const ui_palette_t *p = ui_palette();
	lv_obj_t *c, *card, *col;
	int i, x;

	c = ui_screen("MUSIC");
	ui_hint_keys(NOW_HINT, "");
	ui_on_back(quit);

	card = ui_card(c, WT_INNER_X, WT_INNER_Y, WT_INNER_W, CARD_H);
	lv_obj_add_event_cb(card, now_deleted, LV_EVENT_DELETE, NULL);
	state_l = text(card, UI_ROLE_DATA, p->accent, "");
	lv_obj_set_pos(state_l, 9, 6);
	vol_l = text(card, UI_ROLE_DATA, p->dim, "");
	lv_obj_align(vol_l, LV_ALIGN_TOP_RIGHT, -9, 6);
	/* title, artist, album stacked under the state line: the styles' fonts differ in height */
	col = lv_obj_create(card);
	lv_obj_remove_style_all(col);
	lv_obj_set_size(col, WT_INNER_W - 20, LV_SIZE_CONTENT);
	lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_row(col, 1, 0);
	lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(col, LV_OBJ_FLAG_OVERFLOW_VISIBLE);	/* Aldrich draws above its box */
	lv_obj_update_layout(state_l);
	lv_obj_set_pos(col, 9, lv_obj_get_y2(state_l) + 5);
	title_l = text(col, UI_ROLE_NAME, p->text, "");
	lv_label_set_long_mode(title_l, LV_LABEL_LONG_DOT);
	lv_obj_set_width(title_l, LV_PCT(100));
	artist_l = text(col, UI_ROLE_DATA, p->text, "");
	lv_label_set_long_mode(artist_l, LV_LABEL_LONG_DOT);
	lv_obj_set_width(artist_l, LV_PCT(100));
	album_l = text(col, UI_ROLE_DATA, p->dim, "");
	lv_label_set_long_mode(album_l, LV_LABEL_LONG_DOT);
	lv_obj_set_width(album_l, LV_PCT(100));
	bar = lv_bar_create(card);
	lv_obj_set_size(bar, 190, 4);
	lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 9, -10);
	lv_bar_set_range(bar, 0, 1000);
	lv_obj_set_style_radius(bar, 0, 0);
	lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR);
	lv_obj_set_style_bg_color(bar, p->dim, 0);
	lv_obj_set_style_bg_opa(bar, LV_OPA_40, 0);
	lv_obj_set_style_bg_color(bar, p->accent, LV_PART_INDICATOR);
	time_l = text(card, UI_ROLE_DATA, p->dim, "");
	lv_obj_align(time_l, LV_ALIGN_BOTTOM_RIGHT, -9, -5);

	for (i = 0, x = WT_INNER_X; i <= B_VOLUP; i++, x += 54 + 8)
		buttons[i] = ui_button(c, x, ROW1_Y, 54, 40, icons[i], names[i]);
	for (i = B_QUEUE, x = WT_INNER_X; i < B_COUNT; i++, x += 96 + 8)
		buttons[i] = ui_button(c, x, ROW2_Y, 96, 32, icons[i], names[i]);
	for (i = 0; i < B_COUNT; i++)
		lv_obj_add_event_cb(buttons[i], button_event, LV_EVENT_ALL, (void *)(intptr_t)i);
	lv_group_focus_obj(buttons[now_focus]);
	refresh();
}

static void back_to_now(void)
{
	now_screen();
}

/* ---- QUEUE ---------------------------------------------------------------------------- */

static void queue_event(lv_event_t *e)
{
	int i = (int)(intptr_t)lv_event_get_user_data(e);

	switch (lv_event_get_code(e)) {
	case LV_EVENT_FOCUSED:
		list_focus = i;
		break;
	case LV_EVENT_CLICKED:
		mpdc_play_pos(i);
		ui_screen_transition(UI_SLIDE_OUT);
		now_screen();
		break;
	case LV_EVENT_KEY:
		if (lv_event_get_key(e) == LV_KEY_BACKSPACE) {
			mpdc_delete(i);
			if (list_focus >= nitems - 1 && list_focus > 0)
				list_focus--;
			ui_screen_transition(UI_SLIDE_NONE);
			queue_screen();
		}
		break;
	default:
		break;
	}
}

static void queue_screen(void)
{
	const ui_palette_t *p = ui_palette();
	lv_obj_t *c, *list, *row;
	int i;

	c = ui_screen("MUSIC/QUEUE");
	ui_on_back(back_to_now);
	mpdc_status(&st);
	nitems = mpdc_queue(items, MPDC_MAX_ITEMS);
	if (st.state == MPDC_OFFLINE) {
		ui_hint_keys("ESC BACK", "");
		offline_status(c);
		return;
	}
	ui_hint_keys("ENT PLAY|BKSP REMOVE|ESC BACK", "");
	list = ui_rows_create(c, WT_INNER_Y, WT_INNER_H, false);
	for (i = 0; i < nitems; i++) {
		upper(items[i].name);
		row = ui_row_ex(list, NULL, items[i].name, items[i].info, UI_ROW_NO_CHEVRON);
		if (!row)
			break;			/* the list is full */
		if (i == st.song_pos)
			ui_row_tag(row, st.state == MPDC_PLAY ? "NOW" : "CUR", p->accent2);
		lv_obj_add_event_cb(row, queue_event, LV_EVENT_ALL, (void *)(intptr_t)i);
	}
	ui_rows_done(list, list_focus < nitems ? list_focus : 0);
	if (!nitems) {
		ui_hint_keys("ESC BACK", "");
		lv_obj_set_pos(text(c, UI_ROLE_HINT, p->dim, "THE QUEUE IS EMPTY\nADD MUSIC FROM LIBRARY OR RADIO"),
			       WT_INNER_X + 8, CY(WT_INNER_Y + 8));
	}
}

/* ---- LIBRARY -------------------------------------------------------------------------- */

#define LIB_PLAY_ALL	-1
#define LIB_UPDATE	-2

static void library_up(void)
{
	char *s = strrchr(path, '/');

	if (!path[0]) {
		now_screen();
		return;
	}
	if (s)
		*s = 0;
	else
		path[0] = 0;
	list_focus = 0;
	library_screen();
}

static void library_event(lv_event_t *e)
{
	int i = (int)(intptr_t)lv_event_get_user_data(e);

	if (lv_event_get_code(e) == LV_EVENT_FOCUSED) {
		list_focus = i + 2;
		return;
	}
	if (lv_event_get_code(e) != LV_EVENT_CLICKED)
		return;
	if (i == LIB_UPDATE) {
		mpdc_update();
		ui_screen_transition(UI_SLIDE_NONE);
		list_focus = 0;
		library_screen();
		return;
	}
	if (i == LIB_PLAY_ALL || items[i].kind == MPDC_SONG) {
		mpdc_play_dir(path, i);
		now_focus = B_PLAY;
		ui_screen_transition(UI_SLIDE_OUT);
		now_screen();
		return;
	}
	snprintf(path, sizeof(path), "%s", items[i].uri);
	list_focus = 0;
	library_screen();
}

static void library_screen(void)
{
	const ui_palette_t *p = ui_palette();
	lv_obj_t *c, *list, *row;
	char title[64], name[128];
	const char *leaf = strrchr(path, '/');
	int i;

	snprintf(title, sizeof(title), "MUSIC/%.40s", path[0] ? (leaf ? leaf + 1 : path) : "LIBRARY");
	upper(title);
	c = ui_screen(title);
	ui_on_back(library_up);
	nitems = mpdc_browse(path, items, MPDC_MAX_ITEMS);
	mpdc_status(&st);
	if (st.state == MPDC_OFFLINE) {
		ui_hint_keys("ESC BACK", "");
		offline_status(c);
		return;
	}
	ui_hint_keys("\xE2\x86\x91\xE2\x86\x93 SELECT|ENT OPEN|ESC BACK", "");
	list = ui_rows_create(c, WT_INNER_Y, WT_INNER_H, false);
	lv_obj_add_event_cb(ui_row_ex(list, "play", "PLAY ALL", path[0] ? "THIS FOLDER" : "EVERYTHING", UI_ROW_NO_CHEVRON),
			    library_event, LV_EVENT_ALL, (void *)(intptr_t)LIB_PLAY_ALL);
	if (!path[0])
		lv_obj_add_event_cb(ui_row_ex(list, "refresh", "UPDATE", "RESCAN /ROOT/MUSIC", UI_ROW_NO_CHEVRON),
				    library_event, LV_EVENT_ALL, (void *)(intptr_t)LIB_UPDATE);
	for (i = 0; i < nitems; i++) {
		snprintf(name, sizeof(name), "%s", items[i].kind == MPDC_DIR ? strrchr(items[i].uri, '/') ?
			 strrchr(items[i].uri, '/') + 1 : items[i].uri : items[i].name);
		upper(name);
		row = ui_row_ex(list, items[i].kind == MPDC_DIR ? "folder" : "audio", name, items[i].info,
				items[i].kind == MPDC_DIR ? 0 : UI_ROW_NO_CHEVRON);
		if (!row)
			break;
		lv_obj_add_event_cb(row, library_event, LV_EVENT_ALL, (void *)(intptr_t)i);
	}
	snprintf(marker, sizeof(marker), "%d ITEM%s", nitems, nitems == 1 ? "" : "S");
	ui_rows_marker(list, marker);
	ui_rows_done(list, list_focus);
	ui_hint_marker(marker);
	if (!nitems && !path[0])
		lv_obj_set_pos(text(c, UI_ROLE_HINT, p->dim, "NO MUSIC YET: COPY FILES TO /ROOT/MUSIC\n(SCP -R ... ROOT@UNIT:/ROOT/MUSIC/)"),
			       WT_INNER_X + 8, CY(WT_INNER_Y + 80));
}

/* ---- RADIO ---------------------------------------------------------------------------- */

#define RADIO_ADD	-1

static void url_ready(lv_obj_t *field)
{
	const char *url = lv_textarea_get_text(field);

	if (!ui_keyboard_visible() && !*url) {
		ui_keyboard_show(field, true);
		return;
	}
	ui_keyboard_show(NULL, false);
	if (!strstr(url, "://") || !mpdc_add_station(url)) {
		ui_text_field_set_error(field, true);
		ui_status_line(lv_obj_get_parent(lv_obj_get_parent(field)), UI_TONE_WARN, "warning",
			       "NOT ADDED", strstr(url, "://") ? mpdc_error() : "NEEDS HTTP:// OR HTTPS://");
		return;
	}
	mpdc_play_uri(url);
	ui_screen_transition(UI_SLIDE_OUT);
	now_screen();
}

static void back_to_radio(void)
{
	radio_screen();
}

static void add_screen(void)
{
	const ui_palette_t *p = ui_palette();
	lv_obj_t *c, *box, *l;

	c = ui_screen("MUSIC/ADD STATION");
	ui_on_back(back_to_radio);
	ui_hint_keys("ENT EDIT|ESC BACK", "");
	l = text(c, UI_ROLE_HINT, p->dim, "STREAM OR PLAYLIST URL");
	lv_obj_set_pos(l, WT_INNER_X, CY(29));
	box = lv_obj_create(c);
	lv_obj_remove_style_all(box);
	lv_obj_set_pos(box, WT_INNER_X, CY(WT_FIELD_Y));
	lv_obj_set_size(box, WT_INNER_W, WT_FIELD_H);
	lv_obj_add_flag(box, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
	url_field = ui_text_field(box, "HTTP://...", false, url_ready);
	l = text(c, UI_ROLE_HINT, p->dim, "A STATION, OR AN .M3U / .PLS PLAYLIST\n(INTERNET ARCHIVE: .../NAME_64KB.M3U)\nSAVED IN /ROOT/MUSIC/PLAYLISTS/RADIO.M3U");
	lv_obj_set_pos(l, WT_INNER_X, CY(WT_FIELD_Y + WT_FIELD_H + 10));
	lv_group_focus_obj(url_field);
}

static void radio_event(lv_event_t *e)
{
	int i = (int)(intptr_t)lv_event_get_user_data(e);

	if (lv_event_get_code(e) == LV_EVENT_FOCUSED) {
		list_focus = i + 1;
		return;
	}
	if (lv_event_get_code(e) != LV_EVENT_CLICKED)
		return;
	if (i == RADIO_ADD) {
		add_screen();
		return;
	}
	if (items[i].kind == MPDC_PLAYLIST)
		mpdc_play_playlist(items[i].uri);
	else
		mpdc_play_uri(items[i].uri);
	now_focus = B_PLAY;
	ui_screen_transition(UI_SLIDE_OUT);
	now_screen();
}

static void radio_screen(void)
{
	lv_obj_t *c, *list, *row;
	char name[128];
	int i, n;

	c = ui_screen("MUSIC/RADIO");
	ui_on_back(back_to_now);
	mpdc_status(&st);
	if (st.state == MPDC_OFFLINE) {
		ui_hint_keys("ESC BACK", "");
		offline_status(c);
		return;
	}
	nitems = mpdc_stations(items, MPDC_MAX_ITEMS);
	n = mpdc_playlists(items + nitems, MPDC_MAX_ITEMS - nitems);
	ui_hint_keys("\xE2\x86\x91\xE2\x86\x93 SELECT|ENT PLAY|ESC BACK", "");
	list = ui_rows_create(c, WT_INNER_Y, WT_INNER_H, false);
	lv_obj_add_event_cb(ui_row(list, "plus", "ADD STATION", "URL"), radio_event, LV_EVENT_ALL,
			    (void *)(intptr_t)RADIO_ADD);
	for (i = 0; i < nitems + n; i++) {
		snprintf(name, sizeof(name), "%s", items[i].name);
		upper(name);
		if (items[i].kind == MPDC_SONG && mpdc_is_playlist_url(items[i].uri))
			snprintf(items[i].info, sizeof(items[i].info), "PLAYLIST");
		row = ui_row_ex(list, items[i].kind == MPDC_PLAYLIST || mpdc_is_playlist_url(items[i].uri) ? "list" : "audio",
				name, items[i].info,
				UI_ROW_NO_CHEVRON);
		if (!row)
			break;
		lv_obj_add_event_cb(row, radio_event, LV_EVENT_ALL, (void *)(intptr_t)i);
	}
	nitems += n;
	ui_rows_done(list, list_focus <= nitems ? list_focus : 0);
}

int main(void)
{
	ui_init();
	now_screen();
	poll_timer = lv_timer_create(poll_cb, POLL_MS, NULL);
	return ui_run();
}
