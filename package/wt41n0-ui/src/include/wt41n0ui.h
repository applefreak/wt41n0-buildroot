/*
 * wt41n0ui: the shared base of the WT41N0 launcher and its apps.
 *
 * One call sets up everything an app needs: the display (the 320x240 landscape view of
 * the rotated panel), keypad and touch input, the fonts and the current theme. Apps
 * then build LVGL objects as usual and run the main loop:
 *
 *	ui_init();
 *	lv_obj_t *content = ui_screen("SETTINGS");
 *	... build widgets in content, add focusable ones to ui_group() ...
 *	return ui_run();
 *
 * Keys arrive as LVGL keys: Up/Down move the focus (LV_KEY_PREV/NEXT) or adjust the
 * object being edited (LV_KEY_UP/DOWN), Enter clicks, Esc is LV_KEY_ESC. Characters
 * typed through the orange/gray/blue layers arrive as their ASCII code, exactly as the
 * console's keymap produces them.
 */
#ifndef WT41N0UI_H
#define WT41N0UI_H

#include "lvgl.h"
#include "wt_tokens.h"	/* all sizes and colours of the UI spec */

#ifdef __cplusplus
extern "C" {
#endif

/* The current theme's colours (SPEC.md, Colour tokens). The derived ones are the
 * WT_OPA_* blends, pre-mixed against the surface they sit on. */
typedef struct {
	lv_color_t bg;		/* background */
	lv_color_t panel;	/* surface: tiles, rows, cards */
	lv_color_t line;	/* idle lines and rules */
	lv_color_t text;
	lv_color_t dim;		/* dim text: numbers, labels, hints */
	lv_color_t highlight;	/* bevel light */
	lv_color_t dark;	/* bevel dark */
	lv_color_t accent;	/* focus, highlights */
	lv_color_t accent2;	/* second accent */
	lv_color_t warn;	/* warning */
	/* derived */
	lv_color_t accent_fill;		/* accent 16 % over surface: focused fill */
	lv_color_t accent_pressed;	/* accent 35 % over surface: pressed fill */
	lv_color_t glow;		/* accent 35 % over background: outer focus line */
	lv_color_t text_hilight;	/* text 15 % over surface: CYBER panel top edge */
	lv_color_t text_edge;		/* text 8 % over background: CYBER idle border */
	lv_color_t text_bright;		/* text 70 % over background: corner pieces */
	lv_color_t text_tick;		/* text 25 % over surface: ticks, idle notch */
	lv_color_t text_white;		/* text 80 % over surface: AERO focus ring, icon rings */
} ui_palette_t;

/* Set up display, input, fonts and theme (from /etc/wt41n0/ui.conf). */
void ui_init(void);
/* Run until ui_quit(); returns its code. */
int ui_run(void);
void ui_quit(int code);

/* Theme */
const ui_palette_t *ui_palette(void);
const lv_font_t *ui_font(int px);	/* text font at 12, 14, 16, 20 or 28 px (symbols included) */
const lv_font_t *ui_font_icons(void);	/* 28 px LV_SYMBOL_* icons */
const lv_font_t *ui_font_icons24(void);	/* 24 px LV_SYMBOL_* icons (tile icons) */

/* Typography roles (SPEC.md, Typography): font and letter spacing for the current
 * style. Apply with ui_text_role(label, role). */
typedef enum {
	UI_ROLE_TITLE, UI_ROLE_NAME, UI_ROLE_DATA, UI_ROLE_HINT, UI_ROLE_BIG,
	UI_ROLE_KEY, UI_ROLE_KEY_SMALL, UI_ROLE_BODY, UI_ROLE_COUNT
} ui_role_t;
const lv_font_t *ui_font_role(ui_role_t role);
void ui_text_role(lv_obj_t *label, ui_role_t role);
/* Style, palette and accent (SPEC.md, Theming). Names are lower case; palette and accent
 * may also be "auto" (the style's default). ui_theme_set() with save = false previews. */
const char *const *ui_style_names(void);	/* NULL-terminated */
const char *const *ui_palette_names(void);
const char *const *ui_accent_names(void);
void ui_theme_set(const char *style, const char *palette, const char *accent, bool save);
void ui_set_theme(const char *palette, const char *accent);	/* NULL keeps the current one */
wt_style_t ui_theme_style(void);
const char *ui_theme_style_name(void);
const char *ui_theme_palette(void);		/* resolved */
const char *ui_theme_accent(void);
const char *ui_theme_palette_choice(void);	/* as chosen: may be "auto" */
const char *ui_theme_accent_choice(void);
const wt_style_metrics_t *ui_style_metrics(void);

/* Screens: the status bar, a content area and the key hint bar. Returns the content
 * area (WT_SCREEN_W x WT_SCREEN_H - WT_STATUS_H - WT_HINT_H). The title shows in the status
 * bar; NULL shows the home title. */
lv_obj_t *ui_screen(const char *title);

/* How the next ui_screen() appears: sliding in from the right (the default after the
 * first screen), sliding back out to the right (set automatically when Esc goes back),
 * or at once. */
enum ui_transition { UI_SLIDE_NONE, UI_SLIDE_IN, UI_SLIDE_OUT };
void ui_screen_transition(enum ui_transition t);
void ui_hint(const char *text);
/* Key hints: "KEY LABEL|KEY LABEL|..." ("↑↓" is drawn), and a right-aligned marker such
 * as "1/2" (NULL: none). Content positions: WT_INNER_* are absolute, so place children
 * of the content area at (x, y - WT_CONTENT_Y). */
void ui_hint_keys(const char *pairs, const char *marker);
void ui_hint_marker(const char *marker);	/* change the marker */

/* Lists of rows (SPEC.md): ui_rows_create() makes the scrolling column at absolute y,
 * h px tall (rows: WT_INNER_Y, WT_INNER_H; compact rows under a card: y 70), then
 * ui_row() per item (icon name: see ui_icon), then ui_rows_done() with the row to focus.
 * React to a row with LV_EVENT_CLICKED; rows carry numbers 01, 02, ... */
lv_obj_t *ui_rows_create(lv_obj_t *content, int y, int h, bool compact);
#define UI_ROW_NO_NUMBER	1
#define UI_ROW_NO_CHEVRON	2
#define UI_ROW_CHECK		4	/* a check column first: ui_row_set_check() */
lv_obj_t *ui_row_ex(lv_obj_t *list, const char *icon, const char *name, const char *value, int flags);
lv_obj_t *ui_row_value(lv_obj_t *row);
void ui_row_set_check(lv_obj_t *row, bool on);
lv_obj_t *ui_row_tag(lv_obj_t *row, const char *text, lv_color_t color);
void ui_rows_marker(lv_obj_t *list, const char *marker);	/* static string */
lv_obj_t *ui_row(lv_obj_t *list, const char *icon, const char *name, const char *value);
void ui_row_set_value(lv_obj_t *row, const char *value);
void ui_rows_done(lv_obj_t *list, int focus);
lv_obj_t *ui_icon_sized(lv_obj_t *parent, const char *name, int size);

/* Controls (SPEC.md, Components). y values are absolute, from the tokens. */
/* Slider card at the content proper: ENT starts adjusting, ↑↓ change by WT_SLIDER_STEP
 * (changed() runs live), ENT sets, ESC restores; touch sets the value on the track. */
lv_obj_t *ui_slider_card(lv_obj_t *content, const char *icon, const char *name, const char *unit,
			 int min, int max, int value, void (*changed)(int value));
/* Toggle row, 304 x 28 at y (WT_TOGGLE_ROW_H rows at y 84 and 114) */
lv_obj_t *ui_toggle_row(lv_obj_t *content, int y, const char *name, bool on,
			void (*changed)(lv_obj_t *row, bool on));
/* Button: wide (WT_BTN_WIDE_W x WT_BTN_WIDE_H, icon + label) or small (54 x 40, icon over
 * label). React with LV_EVENT_CLICKED; a disabled one stays focusable but should do nothing. */
lv_obj_t *ui_button(lv_obj_t *content, int x, int y, int w, int h, const char *icon, const char *name);
void ui_button_set_disabled(lv_obj_t *b, bool disabled);
bool ui_button_disabled(lv_obj_t *b);
/* Big button (304 x 72 at y): the first ENT arms it ("CONFIRM <name>", warning, 5 s
 * countdown), a second ENT runs act(); ESC or the timeout disarms. idle_hint is the
 * screen's hint to restore. */
lv_obj_t *ui_big_button(lv_obj_t *content, int y, const char *icon, const char *name, const char *sub,
			const char *key_label, const char *idle_hint, void (*act)(void *data), void *data);
/* Status line at y 192: CONNECTING (accent), CONNECTED (second accent), warnings */
enum ui_tone { UI_TONE_ACCENT, UI_TONE_GOOD, UI_TONE_WARN };
lv_obj_t *ui_status_line(lv_obj_t *content, enum ui_tone tone, const char *icon, const char *msg,
			 const char *detail);
/* Card: an unfocusable panel (the Wi-Fi link card) */
lv_obj_t *ui_card(lv_obj_t *content, int x, int y, int w, int h);
/* The focus ring in another colour (NULL: back to the accent) */
bool ui_focus_ring_grow(void (*done)(void *arg), void *arg);	/* app launch; false: one running */
void ui_focus_ring_refresh(void);
void ui_focus_ring_color(const lv_color_t *color);
lv_obj_t *ui_arrows(lv_obj_t *parent, lv_color_t color);	/* ↑↓ glyph, drawn */

/* Themed widgets: a scrolling column, and focusable rows in it (added to ui_group()).
 * React to a row with lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, data). */
lv_obj_t *ui_list(lv_obj_t *parent);
lv_obj_t *ui_list_button(lv_obj_t *list, const char *symbol, const char *text);
void ui_list_button_set_text(lv_obj_t *row, const char *symbol, const char *text);

/* Text entry: a one-line field (typed into through the keypad layers; ready() runs on
 * Enter), and the on-screen keyboard for it. */
lv_obj_t *ui_text_field(lv_obj_t *parent, const char *placeholder, bool password,
			void (*ready)(lv_obj_t *field));
void ui_keyboard_show(lv_obj_t *field, bool show);
void ui_text_field_set_error(lv_obj_t *field, bool error);	/* warning border (WRONG PASSWORD) */
bool ui_keyboard_visible(void);

/* A labelled slider row (Enter to adjust, Up/Down to change, Enter/Esc to finish);
 * returns the slider: watch LV_EVENT_VALUE_CHANGED on it. */
lv_obj_t *ui_slider_row(lv_obj_t *list, const char *text, int min, int max, int value);

/* Settings in /etc/wt41n0/ui.conf ("key=value" lines), shared by all apps */
int ui_conf_get(const char *key, char *buf, size_t len);	/* 0 when found */
int ui_conf_set(const char *key, const char *value);		/* NULL removes it */

/* Panels (tile, row, card, button, chip, readout strip, key) at x, y inside parent, in the
 * current style's idle / focused / pressed looks, with its decoration children (floating;
 * add content after). focusable adds it to ui_group() and makes it clickable. Owners keep
 * their data with ui_panel_set_data(). */
enum ui_panel_kind { UI_PANEL_TILE, UI_PANEL_ROW, UI_PANEL_CARD, UI_PANEL_BUTTON, UI_PANEL_CHIP,
		     UI_PANEL_STRIP, UI_PANEL_KEY };
lv_obj_t *ui_panel_kind(lv_obj_t *parent, enum ui_panel_kind kind, int x, int y, int w, int h, bool focusable);
lv_obj_t *ui_panel(lv_obj_t *parent, int x, int y, int w, int h, bool focusable);	/* a card */
int ui_panel_radius(enum ui_panel_kind kind);
void *ui_panel_data(lv_obj_t *panel);
void ui_panel_set_data(lv_obj_t *panel, void *data);
lv_obj_t *ui_well(lv_obj_t *parent, int x, int y, int w, int h);	/* DECK inset well */
lv_obj_t *ui_chip(lv_obj_t *parent, ui_role_t role, const char *text);	/* accent name chip */

/* Style parts (SPEC.md, Style tables): item numbers, icons in the style's frame, the
 * focused-name treatment, toggles, hint keys */
lv_obj_t *ui_number(lv_obj_t *parent, const char *text);
void ui_number_focus(lv_obj_t *number, bool focused);
lv_obj_t *ui_style_icon(lv_obj_t *parent, const char *name, int size);
void ui_style_icon_focus(lv_obj_t *icon, bool focused);
void ui_name_focus(lv_obj_t *label, const char *name, bool focused);
lv_obj_t *ui_toggle(lv_obj_t *parent);
void ui_toggle_set(lv_obj_t *toggle, bool on);
lv_obj_t *ui_hint_key(lv_obj_t *parent, const char *key, bool arrows);

/* Icons: a 32 x 32 box with a 24 px icon by name (wifi, settings, power, terminal,
 * scanner, system, list, keyboard, ... see ui_icons.c); recolour with ui_icon_color(). */
lv_obj_t *ui_icon(lv_obj_t *parent, const char *name);
void ui_icon_color(lv_obj_t *icon, lv_color_t color);

/* Keypad focus group of the current screen */
lv_group_t *ui_group(void);

/* Esc outside an edited widget calls this (reset by ui_screen()); NULL ignores Esc.
 * Apps usually pass a function that goes back one screen or calls ui_quit(). */
void ui_on_back(void (*cb)(void));

/* For the launcher: hand the screen and the input devices to another program and
 * take them back. While suspended, LVGL does not run; ui_wait_key() reports raw key
 * events (Linux codes) so the launcher can watch for its home combination. */
void ui_suspend(void);
void ui_resume(void);
int ui_wait_key(int timeout_ms, unsigned int *code, int *value);	/* 1 = event */
int ui_console_activate(int vt);	/* switch the active virtual terminal */

/* System state for the status bar and info screens; -1 when unknown */
int ui_battery_percent(bool *charging);
int ui_wifi_quality(void);		/* 0..100 */

/* Touch panel. ui_touch_calibrate() shows the calibration screen (centre, four
 * corners, centre check) and calls done() when it is saved or skipped with Esc. */
bool ui_touch_present(void);
bool ui_touch_calibrated(void);
void ui_touch_calibrate(void (*done)(bool saved));

/* Sticky key layers being active (for the status bar) */
enum { UI_LAYERS_GRAY = 1, UI_LAYERS_ORANGE = 2, UI_LAYERS_BLUE = 4, UI_LAYERS_LOCKED = 8 };
int ui_key_layers(void);

#ifdef __cplusplus
}
#endif

/* Boot splash (full screen): ui_splash() then ui_splash_progress() per step */
#define WT41N0_UI_VERSION "0.3"
void ui_splash(const char *version);
void ui_splash_progress(int percent, const char *word);

#endif
