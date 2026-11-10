/*
 * Settings: Wi-Fi, display, sound, theme, text input, time zone and touch.
 * Started with "wifi" as its argument it opens on the Wi-Fi screen (for a manifest that
 * wants a Wi-Fi tile; the home screen has none by default).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "settings.h"
#include "wpa.h"

enum { ROW_WIFI, ROW_DISPLAY, ROW_KEYLIGHT, ROW_SOUND, ROW_THEME, ROW_INPUT, ROW_TZ, ROW_TOUCH };

static lv_obj_t *list;

static void quit(void) { ui_quit(0); }

lv_obj_t *settings_note(lv_obj_t *parent, const char *text)
{
	lv_obj_t *l = lv_label_create(parent);

	lv_label_set_text(l, text);
	lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(l, LV_PCT(100));
	lv_obj_set_style_text_font(l, ui_font(12), 0);
	lv_obj_set_style_text_color(l, ui_palette()->dim, 0);
	return l;
}

static void calibrated(bool saved);

/* ---- display and sound (slider cards) ------------------------------------------- */

static void back_display(void) { settings_menu(ROW_DISPLAY); }
static void back_sound(void) { settings_menu(ROW_SOUND); }
static void back_keylight(void) { settings_menu(ROW_KEYLIGHT); }
static int backlight_max;

static void brightness_changed(int percent)
{
	char buf[8];
	int v = (percent * backlight_max + 50) / 100;

	sys_set_brightness(v < 1 ? 1 : v);	/* never 0: the screen would go dark */
	snprintf(buf, sizeof(buf), "%d", v < 1 ? 1 : v);
	ui_conf_set("brightness", buf);
}

static void unavailable(lv_obj_t *content, const char *icon, const char *msg)
{
	ui_status_line(content, UI_TONE_WARN, icon, msg, "");
	ui_hint_keys("ESC BACK", "");
}

static void display_screen(void)
{
	int cur = sys_brightness(&backlight_max), pct;
	lv_obj_t *c = ui_screen("SETTINGS/DISPLAY");

	ui_on_back(back_display);
	if (cur < 0 || backlight_max <= 0) {
		unavailable(c, "warning", "NO BACKLIGHT CONTROL");
		return;
	}
	pct = (cur * 100 + backlight_max / 2) / backlight_max;
	pct = (pct + WT_SLIDER_STEP / 2) / WT_SLIDER_STEP * WT_SLIDER_STEP;
	lv_group_focus_obj(ui_slider_card(c, "display", "BRIGHTNESS", "%", WT_SLIDER_STEP, 100,
					  pct < WT_SLIDER_STEP ? WT_SLIDER_STEP : pct, brightness_changed));
}

/* ---- keypad light: mode, auto timeout, brightness -------------------------------- */

static const char *const keylight_modes[] = { "auto", "on", "off" };
static const char *const keylight_mode_names[] = { "AUTO", "ALWAYS ON", "OFF" };
static const int keylight_timeouts[] = { 5, 10, 30, 60 };
#define NTIMEOUTS (int)(sizeof(keylight_timeouts) / sizeof(keylight_timeouts[0]))
enum { KL_MODE, KL_TIMEOUT, KL_LEVEL };
static lv_obj_t *kl_rows[3];

static void keylight_screen(int focus);
static void back_keylight_list(void) { keylight_screen(KL_LEVEL); }

static void timeout_text(int s, char *buf, size_t len)
{
	if (s >= 60)
		snprintf(buf, len, "%d MIN", s / 60);
	else
		snprintf(buf, len, "%d S", s);
}

static void keylight_values(void)
{
	char t[16];

	lv_label_set_text(ui_row_value(kl_rows[KL_MODE]), keylight_mode_names[ui_keylight_mode()]);
	timeout_text(ui_keylight_timeout(), t, sizeof(t));
	ui_row_set_value(kl_rows[KL_TIMEOUT], ui_keylight_mode() == UI_KEYLIGHT_AUTO ? t : "--");
	snprintf(t, sizeof(t), "%d%%", ui_keylight_level());
	ui_row_set_value(kl_rows[KL_LEVEL], t);
}

static void keylight_level_changed(int v)
{
	char buf[8];

	snprintf(buf, sizeof(buf), "%d", v);
	ui_conf_set("keylight", buf);
	ui_keylight_apply();		/* lights it at the new level to show it */
}

static void keylight_row_clicked(lv_event_t *e)
{
	int row = (int)(intptr_t)lv_event_get_user_data(e), i;
	char buf[8];

	switch (row) {
	case KL_MODE:			/* AUTO -> ALWAYS ON -> OFF */
		ui_conf_set("keylight_mode", keylight_modes[(ui_keylight_mode() + 1) % 3]);
		break;
	case KL_TIMEOUT:		/* 5 s -> 10 s -> 30 s -> 1 min */
		for (i = 0; i < NTIMEOUTS && keylight_timeouts[i] != ui_keylight_timeout(); i++)
			;
		snprintf(buf, sizeof(buf), "%d", keylight_timeouts[(i + 1) % NTIMEOUTS]);
		ui_conf_set("keylight_timeout", buf);
		break;
	default: {
		lv_obj_t *c = ui_screen("KEYPAD/BRIGHTNESS");
		int cur = (ui_keylight_level() + WT_SLIDER_STEP / 2) / WT_SLIDER_STEP * WT_SLIDER_STEP;

		ui_on_back(back_keylight_list);
		lv_group_focus_obj(ui_slider_card(c, "keyboard", "KEYPAD LIGHT", "%", WT_SLIDER_STEP, 100, cur,
						  keylight_level_changed));
		return;
	}
	}
	ui_keylight_apply();
	keylight_values();
}

/* the keypad light settings, as a list: ENT cycles MODE and TURN OFF AFTER */
static void keylight_screen(int focus)
{
	lv_obj_t *c = ui_screen("SETTINGS/KEYPAD"), *list;
	int i;

	ui_hint_keys("\xE2\x86\x91\xE2\x86\x93 SELECT|ENT CHANGE|ESC BACK", "");
	ui_on_back(back_keylight);
	list = ui_rows_create(c, WT_INNER_Y, WT_INNER_H, false);
	kl_rows[KL_MODE] = ui_row(list, "keyboard", "MODE", "");
	kl_rows[KL_TIMEOUT] = ui_row(list, "clock", "TURN OFF AFTER", "");
	kl_rows[KL_LEVEL] = ui_row(list, "display", "BRIGHTNESS", "");
	for (i = 0; i < 3; i++)
		lv_obj_add_event_cb(kl_rows[i], keylight_row_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
	keylight_values();
	ui_rows_done(list, focus);
}

static void volume_changed(int v)
{
	char buf[8];

	sys_set_volume(v);
	snprintf(buf, sizeof(buf), "%d", v);
	ui_conf_set("volume", buf);
}

static void sound_screen(void)
{
	int cur = sys_volume();
	lv_obj_t *c = ui_screen("SETTINGS/SOUND");

	ui_on_back(back_sound);
	if (cur < 0) {
		unavailable(c, "warning", "NO MIXER FOUND");
		return;
	}
	cur = (cur + WT_SLIDER_STEP / 2) / WT_SLIDER_STEP * WT_SLIDER_STEP;
	lv_group_focus_obj(ui_slider_card(c, "volume", "VOLUME", "%", 0, 100, cur, volume_changed));
}

/* ---- theme: STYLE / PALETTE / ACCENT chips --------------------------------------- */

/* Moving the focus previews that chip's choice (the screen is rebuilt in it); ENT
 * applies it and saves; ESC goes back with the saved theme. Applying a style resets
 * palette and accent to AUTO, its defaults. */
enum { G_STYLE, G_PALETTE, G_ACCENT, GROUPS };
static const int group_first[GROUPS + 1] = { 0, 4, 9, 14 };
static const char *const group_label[GROUPS] = { "STYLE", "PALETTE", "ACCENT" };
static const int group_y[GROUPS] = { 41, 99, 157 };
static char saved_style[16], saved_palette[16], saved_accent[16];	/* as chosen (auto) */
static char applied[GROUPS][16];					/* resolved: checked */
static int theme_focus;

static void theme_screen(int focus);
static void upper(char *s);

static const char *chip_name(int i)
{
	return i < group_first[G_PALETTE] ? ui_style_names()[i] :
	       i < group_first[G_ACCENT] ? ui_palette_names()[i - group_first[G_PALETTE]] :
	       ui_accent_names()[i - group_first[G_ACCENT]];
}

static int chip_group(int i)
{
	return i < group_first[G_PALETTE] ? G_STYLE : i < group_first[G_ACCENT] ? G_PALETTE : G_ACCENT;
}

static void theme_save_current(void)
{
	snprintf(saved_style, sizeof(saved_style), "%s", ui_theme_style_name());
	snprintf(saved_palette, sizeof(saved_palette), "%s", ui_theme_palette_choice());
	snprintf(saved_accent, sizeof(saved_accent), "%s", ui_theme_accent_choice());
	snprintf(applied[G_STYLE], sizeof(applied[0]), "%s", ui_theme_style_name());
	snprintf(applied[G_PALETTE], sizeof(applied[0]), "%s", ui_theme_palette());
	snprintf(applied[G_ACCENT], sizeof(applied[0]), "%s", ui_theme_accent());
}

/* the saved theme with chip i's choice on top */
static void theme_preview(int i, bool save)
{
	const char *n = chip_name(i);

	switch (chip_group(i)) {
	case G_STYLE: ui_theme_set(n, "auto", "auto", save); break;
	case G_PALETTE: ui_theme_set(saved_style, n, saved_accent, save); break;
	default: ui_theme_set(saved_style, saved_palette, n, save); break;
	}
}

static void theme_rebuild(void *arg)
{
	ui_screen_transition(UI_SLIDE_NONE);
	theme_screen((int)(intptr_t)arg);
}

static void back_theme(void)
{
	ui_theme_set(saved_style, saved_palette, saved_accent, false);
	settings_menu(ROW_THEME);
}

static void chip_event(lv_event_t *e)
{
	int i = (int)(intptr_t)lv_event_get_user_data(e);

	switch (lv_event_get_code(e)) {
	case LV_EVENT_FOCUSED:
		if (i == theme_focus)
			break;
		theme_focus = i;
		theme_preview(i, false);
		lv_async_call(theme_rebuild, (void *)(intptr_t)i);
		break;
	case LV_EVENT_CLICKED:
		theme_preview(i, true);
		theme_save_current();
		lv_async_call(theme_rebuild, (void *)(intptr_t)i);
		break;
	default:
		break;
	}
}

static lv_obj_t *swatch(lv_obj_t *parent, uint32_t rgb, lv_color_t edge)
{
	lv_obj_t *o = lv_obj_create(parent);

	lv_obj_remove_style_all(o);
	lv_obj_set_size(o, 8, 8);
	lv_obj_set_style_bg_color(o, lv_color_hex(rgb), 0);
	lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(o, edge, 0);
	lv_obj_set_style_border_width(o, 1, 0);
	lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
	return o;
}

static void theme_screen(int focus)
{
	static const wt_palette_t pals[] = { WT_PAL_NIGHT, WT_PAL_VOID, WT_PAL_DUSK, WT_PAL_AERO, WT_PAL_TAPE };
	static const wt_accent_t accs[] = { WT_ACC_CYAN, WT_ACC_MAGENTA, WT_ACC_GREEN, WT_ACC_AMBER, WT_ACC_RED };
	static char marker[48];
	const ui_palette_t *p = ui_palette();
	const char *current[GROUPS] = { ui_theme_style_name(), ui_theme_palette(), ui_theme_accent() };
	lv_obj_t *c, *chips[14], *l;
	char text[16];
	int g, i, k;

	c = ui_screen("SETTINGS/THEME");
	ui_on_back(back_theme);
	/* the marker names the previewed choice (the whole theme does not fit beside the keys) */
	snprintf(marker, sizeof(marker), "%s", current[chip_group(focus >= 0 && focus < 14 ? focus : 0)]);
	upper(marker);
	ui_hint_keys("\xE2\x86\x91\xE2\x86\x93 SELECT|ENT APPLY|ESC BACK", marker);
	theme_focus = focus;

	for (g = 0; g < GROUPS; g++) {
		int n = group_first[g + 1] - group_first[g], w = (WT_INNER_W - (n - 1) * 4) / n;

		l = lv_label_create(c);			/* group label, current value right */
		lv_label_set_text(l, group_label[g]);
		ui_text_role(l, UI_ROLE_HINT);
		lv_obj_set_style_text_color(l, p->dim, 0);
		lv_obj_set_pos(l, WT_INNER_X, group_y[g] - 15 - WT_CONTENT_Y);
		l = lv_label_create(c);
		snprintf(text, sizeof(text), "%s", current[g]);
		upper(text);
		lv_label_set_text(l, text);
		ui_text_role(l, UI_ROLE_DATA);
		lv_obj_set_style_text_color(l, p->text, 0);
		lv_obj_align(l, LV_ALIGN_TOP_RIGHT, -(WT_SCREEN_W - WT_INNER_X - WT_INNER_W),
			     group_y[g] - 15 - WT_CONTENT_Y);

		for (k = 0; k < n; k++) {
			lv_obj_t *chip, *name, *row;

			i = group_first[g] + k;
			chip = ui_panel_kind(c, UI_PANEL_CHIP, WT_INNER_X + k * (w + 4), group_y[g] - WT_CONTENT_Y,
					     w, 36, true);
			chips[i] = chip;
			snprintf(text, sizeof(text), "%s", chip_name(i));
			upper(text);
			name = lv_label_create(chip);
			lv_label_set_text(name, text);
			ui_text_role(name, g == G_STYLE ? UI_ROLE_NAME : UI_ROLE_DATA);
			lv_obj_set_style_text_color(name, p->text, 0);
			if (g == G_STYLE) {
				lv_obj_center(name);
			} else {			/* swatches over the name */
				row = lv_obj_create(chip);
				lv_obj_remove_style_all(row);
				lv_obj_set_size(row, LV_SIZE_CONTENT, 8);
				lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
				lv_obj_set_style_pad_column(row, 3, 0);
				lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
				if (g == G_PALETTE) {
					swatch(row, pals[k].bg, p->line);
					swatch(row, pals[k].highlight, p->line);
					swatch(row, pals[k].text, p->line);
				} else {
					swatch(row, accs[k].accent, p->line);
					swatch(row, accs[k].accent2, p->line);
				}
				lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 6);
				lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -4);
			}
			if (!strcmp(chip_name(i), applied[g])) {	/* the applied one: check, top right */
				lv_obj_t *ck = ui_icon_sized(chip, "check", 10);

				lv_obj_set_style_text_font(lv_obj_get_child(ck, 0), &lv_font_montserrat_12, 0);
				ui_icon_color(ck, p->accent);
				lv_obj_align(ck, LV_ALIGN_TOP_RIGHT, -3, 2);
			}
			lv_obj_add_event_cb(chip, chip_event, LV_EVENT_ALL, (void *)(intptr_t)i);
		}
	}
	l = lv_label_create(c);
	lv_label_set_text(l, "MOVING PREVIEWS // ENT APPLIES");
	ui_text_role(l, UI_ROLE_HINT);
	lv_obj_set_style_text_color(l, p->dim, 0);
	lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
	lv_obj_set_width(l, WT_INNER_W);
	lv_obj_set_pos(l, WT_INNER_X, 199 - WT_CONTENT_Y);
	lv_group_focus_obj(chips[focus >= 0 && focus < 14 ? focus : 0]);
}

static void theme_open(void)
{
	int i;

	theme_save_current();
	for (i = 0; ui_style_names()[i]; i++)
		if (!strcmp(ui_style_names()[i], saved_style))
			break;
	theme_screen(i);
}

/* ---- time zone ------------------------------------------------------------------- */

/* fixed offsets (POSIX TZ signs are inverted); the city is only a reference */
static const struct { const char *name, *tz, *city; } zones[] = {
	{ "UTC-12:00", "<-12>12", "BAKER ISLAND" }, { "UTC-11:00", "<-11>11", "PAGO PAGO" },
	{ "UTC-10:00", "<-10>10", "HONOLULU" }, { "UTC-09:00", "<-09>9", "ANCHORAGE" },
	{ "UTC-08:00", "<-08>8", "LOS ANGELES" }, { "UTC-07:00", "<-07>7", "DENVER" },
	{ "UTC-06:00", "<-06>6", "CHICAGO" }, { "UTC-05:00", "<-05>5", "NEW YORK" },
	{ "UTC-04:00", "<-04>4", "HALIFAX" }, { "UTC-03:00", "<-03>3", "SAO PAULO" },
	{ "UTC-02:00", "<-02>2", "SOUTH GEORGIA" }, { "UTC-01:00", "<-01>1", "AZORES" },
	{ "UTC+00:00", "UTC0", "LONDON" }, { "UTC+01:00", "<+01>-1", "BERLIN" },
	{ "UTC+02:00", "<+02>-2", "ATHENS" }, { "UTC+03:00", "<+03>-3", "MOSCOW" },
	{ "UTC+04:00", "<+04>-4", "DUBAI" }, { "UTC+05:00", "<+05>-5", "KARACHI" },
	{ "UTC+05:30", "<+0530>-5:30", "MUMBAI" }, { "UTC+06:00", "<+06>-6", "DHAKA" },
	{ "UTC+07:00", "<+07>-7", "BANGKOK" }, { "UTC+08:00", "<+08>-8", "SINGAPORE" },
	{ "UTC+09:00", "<+09>-9", "TOKYO" }, { "UTC+10:00", "<+10>-10", "SYDNEY" },
	{ "UTC+11:00", "<+11>-11", "NOUMEA" }, { "UTC+12:00", "<+12>-12", "AUCKLAND" },
	{ "UTC+13:00", "<+13>-13", "APIA" },
};
#define NZONES (int)(sizeof(zones) / sizeof(zones[0]))
#define ZONE_UTC 12

static lv_obj_t *zone_rows[NZONES];

static void back_tz(void) { settings_menu(ROW_TZ); }

static int current_zone(void)
{
	char tz[64];
	int i;

	if (ui_conf_get("tz", tz, sizeof(tz)) || !strcmp(tz, "UTC"))
		return ZONE_UTC;
	for (i = 0; i < NZONES; i++)
		if (!strcmp(zones[i].tz, tz))
			return i;
	return -1;
}

static void tz_clicked(lv_event_t *e)
{
	int i = (int)(intptr_t)lv_event_get_user_data(e), k;

	ui_conf_set("tz", zones[i].tz);
	setenv("TZ", zones[i].tz, 1);
	tzset();
	for (k = 0; k < NZONES; k++)
		ui_row_set_check(zone_rows[k], k == i);
}

static void tz_screen(void)
{
	lv_obj_t *list;
	int i, cur = current_zone();

	list = ui_rows_create(ui_screen("SETTINGS/TIME ZONE"), WT_INNER_Y, WT_INNER_H, false);
	ui_hint_keys("\xE2\x86\x91\xE2\x86\x93 SELECT|ENT SET|ESC BACK", "");
	ui_on_back(back_tz);
	for (i = 0; i < NZONES; i++) {
		zone_rows[i] = ui_row_ex(list, NULL, zones[i].name, zones[i].city,
					 UI_ROW_NO_NUMBER | UI_ROW_NO_CHEVRON | UI_ROW_CHECK);
		ui_row_set_check(zone_rows[i], i == cur);
		lv_obj_add_event_cb(zone_rows[i], tz_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
	}
	ui_rows_done(list, cur >= 0 ? cur : ZONE_UTC);
}

/* ---- main menu ------------------------------------------------------------------- */

static void back_menu(void) { settings_menu(ROW_WIFI); }
static void calibrated(bool saved) { (void)saved; settings_menu(ROW_TOUCH); }

static void menu_clicked(lv_event_t *e)
{
	char mode[16] = "";

	switch ((int)(intptr_t)lv_event_get_user_data(e)) {
	case ROW_WIFI: wifi_open(back_menu); break;
	case ROW_DISPLAY: display_screen(); break;
	case ROW_KEYLIGHT: keylight_screen(KL_MODE); break;
	case ROW_SOUND: sound_screen(); break;
	case ROW_THEME: theme_open(); break;
	case ROW_TZ: tz_screen(); break;
	case ROW_TOUCH: ui_touch_calibrate(calibrated); break;
	case ROW_INPUT:
		ui_conf_get("input", mode, sizeof(mode));
		ui_conf_set("input", strcmp(mode, "keyboard") ? "keyboard" : "keypad");
		settings_menu(ROW_INPUT);
		break;
	}
}

static void upper(char *s)
{
	for (; *s; s++)
		if (*s >= 'a' && *s <= 'z')
			*s -= 32;
}

void settings_menu(int focus)
{
	lv_obj_t *rows[ROW_TOUCH + 1] = { 0 }, *list, *content;
	char mode[16] = "", text[48], ssid[64] = "", state[32] = "", ip[48] = "";
	int z = current_zone(), max = 0, b = sys_brightness(&max), v = sys_volume(), k, i;

	content = ui_screen("SETTINGS");
	ui_hint_keys("\xE2\x86\x91\xE2\x86\x93 SELECT|ENT OPEN|ESC BACK", "");
	ui_on_back(quit);
	list = ui_rows_create(content, WT_INNER_Y, WT_INNER_H, false);

	if (wpa_status(ssid, sizeof(ssid), state, sizeof(state), ip, sizeof(ip)) || strcmp(state, "COMPLETED"))
		snprintf(ssid, sizeof(ssid), "OFF");
	upper(ssid);
	rows[ROW_WIFI] = ui_row(list, "wifi", "WI-FI", ssid);
	snprintf(text, sizeof(text), b >= 0 && max > 0 ? "%d%%" : "--", b * 100 / (max > 0 ? max : 1));
	rows[ROW_DISPLAY] = ui_row(list, "display", "DISPLAY", text);
	k = sys_keylight();
	if (k >= 0) {
		if (ui_keylight_mode() == UI_KEYLIGHT_OFF)
			snprintf(text, sizeof(text), "OFF");
		else
			snprintf(text, sizeof(text), "%s %d%%", ui_keylight_mode() == UI_KEYLIGHT_AUTO ? "AUTO" : "ON",
				 ui_keylight_level());
		rows[ROW_KEYLIGHT] = ui_row(list, "keyboard", "KEYPAD LIGHT", text);
	}
	snprintf(text, sizeof(text), v >= 0 ? "%d%%" : "--", v);
	rows[ROW_SOUND] = ui_row(list, "volume", "SOUND", text);
	snprintf(text, sizeof(text), "%s / %s", ui_theme_palette(), ui_theme_accent());	/* resolved */
	upper(text);
	rows[ROW_THEME] = ui_row(list, "theme", "THEME", text);
	ui_conf_get("input", mode, sizeof(mode));
	rows[ROW_INPUT] = ui_row(list, "keyboard", "TEXT INPUT", strcmp(mode, "keyboard") ? "KEY LAYERS" : "ON-SCREEN");
	rows[ROW_TZ] = ui_row(list, "clock", "TIME ZONE", z == ZONE_UTC ? "UTC" : z >= 0 ? zones[z].name : "CUSTOM");
	if (ui_touch_present())
		rows[ROW_TOUCH] = ui_row(list, "touch", "TOUCH CALIBRATION", ui_touch_calibration_skipped() ? "SKIPPED" : ui_touch_calibrated() ? "DONE" : "NOT SET");
	for (i = 0; i <= ROW_TOUCH; i++)
		if (rows[i])
			lv_obj_add_event_cb(rows[i], menu_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
	ui_rows_done(list, focus);
}

/* "settings [wifi|display|sound|theme|tz|touch]": open on that screen */
int main(int argc, char **argv)
{
	const char *open = argc > 1 ? argv[1] : "";

	ui_init();
	if (!strcmp(open, "wifi"))
		wifi_open(quit);
	else if (!strcmp(open, "display"))
		display_screen();
	else if (!strcmp(open, "keylight"))
		keylight_screen(KL_MODE);
	else if (!strcmp(open, "sound"))
		sound_screen();
	else if (!strcmp(open, "theme"))
		theme_open();
	else if (!strcmp(open, "tz"))
		tz_screen();
	else if (!strcmp(open, "touch"))
		ui_touch_calibrate(calibrated);
	else
		settings_menu(ROW_WIFI);
	return ui_run();
}
