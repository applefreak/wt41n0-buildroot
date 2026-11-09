/*
 * Wi-Fi (SPEC.md, Screens > Wi-Fi, Wi-Fi network): a link card with SCAN beside it, then
 * the networks as compact rows; a network screen with its password field, SHOW PASSWORD
 * and ON-SCREEN KEYBOARD toggles, CONNECT / FORGET and a status line.
 * Networks are added to wpa_supplicant and saved with SAVE_CONFIG
 * (/etc/wpa_supplicant.conf needs update_config=1, which the build adds).
 */
#include <stdio.h>
#include <string.h>
#include "settings.h"
#include "wpa.h"

#define CONNECT_TIMEOUT_MS 20000
#define CY(y) ((y) - WT_CONTENT_Y)

static void (*leave)(void);
static struct wpa_net nets[WPA_MAX_NETS];
static int nnets, list_focus;
static struct wpa_net *sel;
static lv_obj_t *content, *link_name, *link_ip, *link_bars, *scan_btn;
static lv_obj_t *password, *connect_btn, *forget_btn, *status;
static lv_timer_t *timer, *link_timer;	/* scan or connect; the link card's refresh */
static int connect_id = -1;
static bool connect_new, scanning;
static uint32_t connect_start;

static void list_screen(void);

static void stop_timer(void)
{
	if (timer)
		lv_timer_delete(timer);
	timer = NULL;
}

static int bars(int dbm)
{
	return dbm >= -55 ? 4 : dbm >= -65 ? 3 : dbm >= -75 ? 2 : 1;
}

static void upper(char *s)
{
	for (; *s; s++)
		if (*s >= 'a' && *s <= 'z')
			*s -= 32;
}

static lv_obj_t *text(lv_obj_t *parent, ui_role_t role, lv_color_t color, const char *s)
{
	lv_obj_t *l = lv_label_create(parent);

	lv_label_set_text(l, s);
	ui_text_role(l, role);
	lv_obj_set_style_text_color(l, color, 0);
	return l;
}

/* the status line at y 192, replaced on each change */
static void set_status(enum ui_tone tone, const char *icon, const char *msg, const char *detail)
{
	if (status)
		lv_obj_delete(status);
	status = msg ? ui_status_line(content, tone, icon, msg, detail) : NULL;
}

/* ---- one network ------------------------------------------------------------------ */

static void back_to_list(void)
{
	stop_timer();
	status = NULL;
	list_screen();
}

static void back_after_connect(lv_timer_t *t)
{
	(void)t;
	timer = NULL;
	ui_screen_transition(UI_SLIDE_OUT);
	back_to_list();
}

static void connect_poll(lv_timer_t *t)
{
	char ssid[140], state[32], ip[48], reply[64];

	(void)t;
	if (wpa_status(ssid, sizeof(ssid), state, sizeof(state), ip, sizeof(ip)))
		return;
	if (!strcmp(state, "COMPLETED") && !strcmp(ssid, sel->ssid)) {
		/* other saved networks stay usable */
		wpa_cmd("ENABLE_NETWORK all", reply, sizeof(reply));
		wpa_cmd("SAVE_CONFIG", reply, sizeof(reply));
		set_status(UI_TONE_GOOD, "ok", "CONNECTED", ip[0] ? ip : "NO ADDRESS YET");
		stop_timer();
		timer = lv_timer_create(back_after_connect, 1500, NULL);
		lv_timer_set_repeat_count(timer, 1);
		return;
	}
	upper(state);
	set_status(UI_TONE_ACCENT, "refresh", "CONNECTING", state);
	if (lv_tick_elaps(connect_start) > CONNECT_TIMEOUT_MS) {
		char cmd[48];

		if (connect_new) {		/* do not keep a network that did not work */
			snprintf(cmd, sizeof(cmd), "REMOVE_NETWORK %d", connect_id);
			wpa_cmd(cmd, reply, sizeof(reply));
		}
		wpa_cmd("ENABLE_NETWORK all", reply, sizeof(reply));
		if (password) {
			set_status(UI_TONE_WARN, "warning", "WRONG PASSWORD", "TRY AGAIN");
			ui_text_field_set_error(password, true);
		} else {
			set_status(UI_TONE_WARN, "warning", "COULD NOT CONNECT", "TRY AGAIN");
		}
		stop_timer();
	}
}

static void connect(void)
{
	char cmd[400], reply[64], hex[140];
	const char *pass = password ? lv_textarea_get_text(password) : "";

	if (ui_button_disabled(connect_btn))
		return;
	ui_keyboard_show(NULL, false);
	if (sel->sec == SEC_PSK && sel->saved_id < 0 && (strlen(pass) < 8 || strlen(pass) > 63)) {
		set_status(UI_TONE_WARN, "warning", "PASSWORD", "8 TO 63 CHARACTERS");
		ui_text_field_set_error(password, true);
		return;
	}
	connect_new = sel->saved_id < 0;
	if (connect_new) {
		if (wpa_cmd("ADD_NETWORK", reply, sizeof(reply)) || sscanf(reply, "%d", &connect_id) != 1) {
			set_status(UI_TONE_WARN, "warning", "WI-FI IS NOT RUNNING", "");
			return;
		}
		wpa_ssid_hex(sel->ssid, hex, sizeof(hex));
		snprintf(cmd, sizeof(cmd), "SET_NETWORK %d ssid %s", connect_id, hex);
		wpa_cmd(cmd, reply, sizeof(reply));
		if (sel->sec == SEC_PSK)
			snprintf(cmd, sizeof(cmd), "SET_NETWORK %d psk \"%s\"", connect_id, pass);
		else
			snprintf(cmd, sizeof(cmd), "SET_NETWORK %d key_mgmt NONE", connect_id);
		wpa_cmd(cmd, reply, sizeof(reply));
		if (strncmp(reply, "OK", 2)) {
			set_status(UI_TONE_WARN, "warning", "PASSWORD NOT ACCEPTED", "");
			snprintf(cmd, sizeof(cmd), "REMOVE_NETWORK %d", connect_id);
			wpa_cmd(cmd, reply, sizeof(reply));
			return;
		}
	} else {
		connect_id = sel->saved_id;
	}
	snprintf(cmd, sizeof(cmd), "SELECT_NETWORK %d", connect_id);
	wpa_cmd(cmd, reply, sizeof(reply));
	connect_start = lv_tick_get();
	set_status(UI_TONE_ACCENT, "refresh", "CONNECTING", "");
	stop_timer();
	timer = lv_timer_create(connect_poll, 500, NULL);
}

static void forget(void)
{
	char cmd[48], reply[64];

	if (ui_button_disabled(forget_btn))
		return;
	snprintf(cmd, sizeof(cmd), "REMOVE_NETWORK %d", sel->saved_id);
	wpa_cmd(cmd, reply, sizeof(reply));
	wpa_cmd("SAVE_CONFIG", reply, sizeof(reply));
	ui_screen_transition(UI_SLIDE_OUT);
	back_to_list();
}

/* ENT on the field edits it with the keyboard; the keyboard's enter connects */
static void field_ready(lv_obj_t *field)
{
	if (ui_keyboard_visible())
		connect();
	else
		ui_keyboard_show(field, true);
}

static void show_changed(lv_obj_t *row, bool on)
{
	(void)row;
	lv_textarea_set_password_mode(password, !on);
}

static void keyboard_changed(lv_obj_t *row, bool on)
{
	(void)row;
	ui_conf_set("input", on ? "keyboard" : "keypad");
}

static void hint_for(lv_event_t *e)
{
	const char *what = lv_event_get_user_data(e);
	char hint[80];

	if (lv_event_get_code(e) != LV_EVENT_FOCUSED || ui_keyboard_visible())
		return;
	snprintf(hint, sizeof(hint), "\xE2\x86\x91\xE2\x86\x93 SELECT|%s%sESC BACK", what, *what ? "|" : "");
	ui_hint_keys(hint, "");
}

static void button_clicked(lv_event_t *e)
{
	if (lv_event_get_user_data(e))
		forget();
	else
		connect();
}

static const char *sec_label(const struct wpa_net *n)
{
	return n->sec == SEC_OPEN ? "OPEN" : n->sec == SEC_PSK ? "WPA2" : "EAP";
}

static void net_screen(struct wpa_net *n)
{
	const ui_palette_t *p = ui_palette();
	char title[64], info[96], name[16], mode[16] = "";
	lv_obj_t *l, *field_box, *first;
	bool needs_password = n->sec == SEC_PSK && n->saved_id < 0;

	stop_timer();
	sel = n;
	password = NULL;
	status = NULL;
	snprintf(title, sizeof(title), "WI-FI/%s", n->ssid);
	upper(title);
	content = ui_screen(title);
	ui_on_back(back_to_list);

	/* info line: signal, dBm, security, NEW / SAVED */
	snprintf(name, sizeof(name), "signal%d", bars(n->signal));
	ui_icon_color(ui_icon_sized(content, name, 16), p->text);
	lv_obj_set_pos(lv_obj_get_child(content, -1), WT_INNER_X - 2, CY(28));
	snprintf(info, sizeof(info), "%d dBm // %s // %s", n->signal, sec_label(n),
		 n->current ? "IN USE" : n->saved_id >= 0 ? "SAVED" : "NEW NETWORK");
	l = text(content, UI_ROLE_DATA, p->dim, info);
	lv_obj_set_pos(l, WT_INNER_X + 20, CY(29));

	if (needs_password) {
		l = text(content, UI_ROLE_HINT, p->dim, "PASSWORD");
		lv_obj_align(l, LV_ALIGN_TOP_RIGHT, -WT_INNER_X, CY(29));
		field_box = lv_obj_create(content);
		lv_obj_remove_style_all(field_box);
		lv_obj_set_pos(field_box, WT_INNER_X, CY(WT_FIELD_Y));
		lv_obj_set_size(field_box, WT_INNER_W, WT_FIELD_H);
		lv_obj_add_flag(field_box, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
		password = ui_text_field(field_box, "8 TO 63 CHARACTERS", true, field_ready);
		lv_obj_add_event_cb(password, hint_for, LV_EVENT_FOCUSED, "ENT EDIT");
		first = password;
		lv_obj_add_event_cb(ui_toggle_row(content, 84, "SHOW PASSWORD", false, show_changed),
				    hint_for, LV_EVENT_FOCUSED, "ENT TOGGLE");
		ui_conf_get("input", mode, sizeof(mode));
		lv_obj_add_event_cb(ui_toggle_row(content, 114, "ON-SCREEN KEYBOARD", !strcmp(mode, "keyboard"),
						  keyboard_changed), hint_for, LV_EVENT_FOCUSED, "ENT TOGGLE");
	} else {
		/* no password to type: what will happen, in a card */
		lv_obj_t *card = ui_card(content, WT_INNER_X, WT_FIELD_Y, WT_INNER_W, 62);

		l = text(card, UI_ROLE_NAME, p->text, n->sec == SEC_OTHER ? "NOT SUPPORTED HERE" :
			 n->sec == SEC_OPEN ? "OPEN NETWORK" : "SAVED NETWORK");
		lv_obj_set_pos(l, 9, 9);
		l = text(card, UI_ROLE_HINT, p->dim, n->sec == SEC_OTHER ?
			 "ENTERPRISE (EAP) AND WEP: ADD IT TO\n/etc/wpa_supplicant.conf" :
			 n->sec == SEC_OPEN ? "NO PASSWORD; TRAFFIC IS NOT ENCRYPTED" :
			 "THE PASSWORD IS STORED ON THE UNIT");
		lv_obj_set_pos(l, 9, 29);
		first = NULL;
	}

	connect_btn = ui_button(content, WT_INNER_X, 148, WT_BTN_WIDE_W, WT_BTN_WIDE_H, "wifi",
				n->current ? "CONNECTED" : "CONNECT");
	forget_btn = ui_button(content, WT_INNER_X + WT_BTN_WIDE_W + 6, 148, WT_BTN_WIDE_W, WT_BTN_WIDE_H,
			       "trash", "FORGET");
	ui_button_set_disabled(connect_btn, n->current || n->sec == SEC_OTHER);
	ui_button_set_disabled(forget_btn, n->saved_id < 0);
	lv_obj_add_event_cb(connect_btn, button_clicked, LV_EVENT_CLICKED, NULL);
	lv_obj_add_event_cb(forget_btn, button_clicked, LV_EVENT_CLICKED, (void *)1);
	lv_obj_add_event_cb(connect_btn, hint_for, LV_EVENT_FOCUSED, ui_button_disabled(connect_btn) ? "" : "ENT GO");
	lv_obj_add_event_cb(forget_btn, hint_for, LV_EVENT_FOCUSED, ui_button_disabled(forget_btn) ? "" : "ENT GO");

	if (first) {
		lv_group_focus_obj(first);
		if (!strcmp(mode, "keyboard"))
			ui_keyboard_show(password, true);
	} else {
		lv_group_focus_obj(ui_button_disabled(connect_btn) ? forget_btn : connect_btn);
	}
}

/* ---- the list --------------------------------------------------------------------- */

static void net_clicked(lv_event_t *e)
{
	list_focus = (int)(intptr_t)lv_event_get_user_data(e);
	net_screen(&nets[list_focus]);
}

static void scan_done(lv_timer_t *t)
{
	(void)t;
	timer = NULL;
	scanning = false;
	ui_screen_transition(UI_SLIDE_NONE);
	list_screen();
}

static void scan(void)
{
	char reply[64];

	wpa_cmd("SCAN", reply, sizeof(reply));
	scanning = true;
	stop_timer();
	timer = lv_timer_create(scan_done, 3000, NULL);	/* results after a few seconds */
	lv_timer_set_repeat_count(timer, 1);
}

static void scan_clicked(lv_event_t *e)
{
	(void)e;
	list_focus = -1;
	scan();
	ui_hint_keys("\xE2\x86\x91\xE2\x86\x93 SELECT|ENT OPEN|ESC BACK", "SCANNING");
}

static void leave_wifi(void)
{
	stop_timer();
	link_name = NULL;
	leave();
}

static void update_link(void)
{
	char ssid[140], state[32], ip[48];
	int i, lit = 0;

	if (!link_name)
		return;
	if (wpa_status(ssid, sizeof(ssid), state, sizeof(state), ip, sizeof(ip))) {
		lv_label_set_text(link_name, "WI-FI IS OFF");
		lv_label_set_text(link_ip, "NOT RUNNING");
	} else if (strcmp(state, "COMPLETED")) {
		upper(state);
		lv_label_set_text(link_name, "NOT CONNECTED");
		lv_label_set_text(link_ip, state);
	} else {
		upper(ssid);
		lv_label_set_text(link_name, ssid);
		lv_label_set_text(link_ip, ip[0] ? ip : "NO ADDRESS YET");
		for (i = 0; i < nnets; i++)
			if (nets[i].current)
				lit = bars(nets[i].signal);
	}
	for (i = 0; i < 4; i++)
		lv_obj_set_style_opa(lv_obj_get_child(link_bars, i), i < lit ? LV_OPA_COVER : LV_OPA_30, 0);
}

static void link_tick(lv_timer_t *t)
{
	(void)t;
	update_link();
}

/* the list screen is going: stop refreshing its card */
static void list_deleted(lv_event_t *e)
{
	(void)e;
	if (link_timer)
		lv_timer_delete(link_timer);
	link_timer = NULL;
	link_name = NULL;
}

static void list_screen(void)
{
	static char marker[16];
	const ui_palette_t *p = ui_palette();
	lv_obj_t *card, *l, *list, *row;
	char name[140], sig[8];
	int i;

	content = ui_screen("SETTINGS/WI-FI");
	ui_hint_keys("\xE2\x86\x91\xE2\x86\x93 SELECT|ENT OPEN|ESC BACK", "");
	ui_on_back(leave_wifi);

	/* link card: LINK chip, network, IP, signal */
	card = ui_card(content, WT_INNER_X, WT_INNER_Y, 236, WT_CARD_H);
	l = ui_chip(card, UI_ROLE_DATA, "LINK");
	lv_obj_align(l, LV_ALIGN_LEFT_MID, 7, 0);
	lv_obj_update_layout(l);
	link_name = text(card, UI_ROLE_DATA, p->text, "");
	lv_obj_set_pos(link_name, lv_obj_get_x2(l) + 9, 4);
	link_ip = text(card, UI_ROLE_DATA, p->text, "");
	lv_obj_set_pos(link_ip, lv_obj_get_x2(l) + 9, 19);
	link_bars = ui_icon_sized(card, "signal4", 18);
	ui_icon_color(link_bars, p->accent);
	lv_obj_align(link_bars, LV_ALIGN_RIGHT_MID, -4, 0);
	scan_btn = ui_button(content, 250, WT_INNER_Y, 54, 40, "refresh", scanning ? "..." : "SCAN");
	lv_obj_add_event_cb(scan_btn, scan_clicked, LV_EVENT_CLICKED, NULL);

	list = ui_rows_create(content, 70, WT_INNER_Y + WT_INNER_H - 70, true);
	nnets = wpa_networks(nets, WPA_MAX_NETS);
	for (i = 0; i < nnets; i++) {
		snprintf(name, sizeof(name), "%s", nets[i].ssid);
		upper(name);
		snprintf(sig, sizeof(sig), "signal%d", bars(nets[i].signal));
		row = ui_row_ex(list, sig, name, nets[i].saved_id >= 0 ? "SAVED" : nets[i].sec == SEC_OPEN ? "OPEN" : "",
				UI_ROW_NO_NUMBER | UI_ROW_NO_CHEVRON);
		if (nets[i].current)
			ui_row_tag(row, "IN USE", p->accent2);
		lv_obj_add_event_cb(row, net_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
	}
	snprintf(marker, sizeof(marker), nnets < 0 ? "OFF" : "%d NETS", nnets);
	ui_rows_marker(list, marker);
	ui_rows_done(list, list_focus);
	if (nnets <= 0) {
		l = text(content, UI_ROLE_HINT, p->dim, nnets < 0 ? "WI-FI IS NOT RUNNING" :
			 scanning ? "SCANNING ..." : "NO NETWORKS FOUND YET");
		lv_obj_set_pos(l, WT_INNER_X + 8, CY(80));
	}
	if (list_focus < 0 || nnets <= 0)
		lv_group_focus_obj(scan_btn);	/* SCAN is focus 0 */
	ui_hint_marker(marker);
	update_link();
	lv_obj_add_event_cb(card, list_deleted, LV_EVENT_DELETE, NULL);
	link_timer = lv_timer_create(link_tick, 2000, NULL);
}

void wifi_open(void (*back)(void))
{
	leave = back;
	list_focus = -1;
	scan();
	list_screen();
}
