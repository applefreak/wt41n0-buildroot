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

#ifdef __cplusplus
extern "C" {
#endif

#define UI_WIDTH	320
#define UI_HEIGHT	240
#define UI_STATUS_H	20	/* status bar at the top */
#define UI_HINT_H	16	/* key hint bar at the bottom */

typedef struct {
	lv_color_t bg;		/* screen background */
	lv_color_t panel;	/* tiles, list rows */
	lv_color_t line;	/* idle outlines and rules */
	lv_color_t text;
	lv_color_t dim;		/* secondary text */
	lv_color_t accent;	/* focus, highlights */
	lv_color_t accent2;	/* second neon colour, for contrast details */
} ui_palette_t;

/* Set up display, input, fonts and theme (from /etc/wt41n0/ui.conf). */
void ui_init(void);
/* Run until ui_quit(); returns its code. */
int ui_run(void);
void ui_quit(int code);

/* Theme */
const ui_palette_t *ui_palette(void);
const lv_font_t *ui_font(int px);	/* text font: 12, 16 or 20 px (symbols included) */
const lv_font_t *ui_font_icons(void);	/* 28 px LV_SYMBOL_* icons */
const char *const *ui_accent_names(void);	/* NULL-terminated */
const char *const *ui_palette_names(void);
void ui_set_theme(const char *palette, const char *accent);	/* NULL keeps the current one */
const char *ui_theme_accent(void);
const char *ui_theme_palette(void);

/* Screens: the status bar, a content area and the key hint bar. Returns the content
 * area (UI_WIDTH x UI_HEIGHT - UI_STATUS_H - UI_HINT_H). The title shows in the status
 * bar; NULL shows the home title. */
lv_obj_t *ui_screen(const char *title);
void ui_hint(const char *text);

/* Themed widgets: a scrolling column, and focusable rows in it (added to ui_group()).
 * React to a row with lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, data). */
lv_obj_t *ui_list(lv_obj_t *parent);
lv_obj_t *ui_list_button(lv_obj_t *list, const char *symbol, const char *text);
void ui_list_button_set_text(lv_obj_t *row, const char *symbol, const char *text);

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

#endif
