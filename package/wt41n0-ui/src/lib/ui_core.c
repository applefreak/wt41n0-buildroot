/* wt41n0ui core: setup, main loop, screens with status bar and key hints. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "ui_internal.h"

static lv_group_t *group;
static bool running;
static int exit_code;

static struct {
	lv_obj_t *bar, *layers, *wifi, *battery, *clock, *hint;
} status;

/* LVGL log to stderr. TinyTTF reports every glyph it hands over to the symbol
 * fallback font as "cache not allocated"; that is not an error here. */
static void log_cb(lv_log_level_t level, const char *buf)
{
	(void)level;
	if (!strstr(buf, "cache not allocated"))
		fputs(buf, stderr);
}

void ui_init(void)
{
	static bool done;

	if (done)		/* the host harness runs several apps in one process */
		return;
	done = true;
	lv_init();
	lv_log_register_print_cb(log_cb);
	lv_tick_set_cb(platform_tick);
	platform_init();
	theme_init();
	fonts_init();
	group = lv_group_create();
	lv_group_set_default(group);
	lv_group_set_wrap(group, true);
	keys_init(group);
	if (!getenv("WT41N0_UI_PERF"))
		lv_sysmon_hide_performance(NULL);
}

int ui_run(void)
{
#ifdef WT41N0_UI_HOST
	return 0;		/* the harness drives the timers itself */
#endif
	running = true;
	while (running)
		platform_wait(lv_timer_handler());
	platform_exit();
	return exit_code;
}

void ui_quit(int code)
{
	exit_code = code;
	running = false;
}

lv_group_t *ui_group(void)
{
	return group;
}

void ui_suspend(void)
{
	platform_suspend();
}

void ui_resume(void)
{
	platform_resume();
	lv_obj_invalidate(lv_screen_active());
	lv_refr_now(NULL);
}

int ui_wait_key(int timeout_ms, unsigned int *code, int *value)
{
	return platform_wait_key(timeout_ms, code, value);
}

int ui_console_activate(int vt)
{
	return platform_vt_activate(vt);
}

int ui_battery_percent(bool *charging)
{
	return platform_battery(charging);
}

int ui_wifi_quality(void)
{
	return platform_wifi();
}

/* ---- status bar ------------------------------------------------------------- */

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
	lv_obj_t *l = lv_label_create(parent);

	lv_obj_set_style_text_font(l, font, 0);
	lv_obj_set_style_text_color(l, color, 0);
	lv_label_set_text(l, text);
	return l;
}

void status_refresh(void)
{
	const ui_palette_t *p = ui_palette();
	char buf[32];
	time_t now;
	bool charging = false;
	int layers, pct, q;

	if (!status.clock)
		return;
	now = time(NULL);
	strftime(buf, sizeof(buf), "%H:%M", localtime(&now));
	lv_label_set_text(status.clock, buf);

	pct = ui_battery_percent(&charging);
	if (pct < 0) {
		lv_label_set_text(status.battery, "");
	} else {
		const char *sym = charging ? LV_SYMBOL_CHARGE :
				  pct > 80 ? LV_SYMBOL_BATTERY_FULL : pct > 55 ? LV_SYMBOL_BATTERY_3 :
				  pct > 30 ? LV_SYMBOL_BATTERY_2 : pct > 10 ? LV_SYMBOL_BATTERY_1 :
				  LV_SYMBOL_BATTERY_EMPTY;
		lv_label_set_text_fmt(status.battery, "%s%d%%", sym, pct);
		lv_obj_set_style_text_color(status.battery, pct <= 10 && !charging ? lv_color_hex(0xff3355) : p->text, 0);
	}

	q = ui_wifi_quality();
	lv_obj_set_style_text_color(status.wifi, q < 0 ? p->line : q < 40 ? p->dim : p->accent, 0);

	layers = ui_key_layers();
	snprintf(buf, sizeof(buf), "%s%s%s", layers & UI_LAYERS_GRAY ? "G" : "",
		 layers & UI_LAYERS_ORANGE ? "O" : "", layers & UI_LAYERS_BLUE ? "B" : "");
	lv_label_set_text(status.layers, buf);
	lv_obj_set_style_text_decor(status.layers,
				    layers & UI_LAYERS_LOCKED ? LV_TEXT_DECOR_UNDERLINE : LV_TEXT_DECOR_NONE, 0);
}

/* The screen with the status bar is going away: stop updating its labels */
static void status_deleted(lv_event_t *e)
{
	/* only when it is the current one: ui_screen() deletes the old screen after
	 * building the new one */
	if (lv_event_get_target(e) == status.bar)
		memset(&status, 0, sizeof(status));
}

static void status_timer(lv_timer_t *t)
{
	(void)t;
	status_refresh();
}

lv_obj_t *ui_screen(const char *title)
{
	const ui_palette_t *p = ui_palette();
	lv_obj_t *scr, *old, *bar, *right, *content, *hint, *l;
	static lv_timer_t *timer;
	char host[32] = "wt41n0";

	scr = lv_obj_create(NULL);
	lv_obj_remove_style_all(scr);
	lv_obj_set_style_bg_color(scr, p->bg, 0);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
	lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

	/* status bar: title left; layers, Wi-Fi, battery, clock right */
	bar = lv_obj_create(scr);
	lv_obj_remove_style_all(bar);
	lv_obj_set_size(bar, UI_WIDTH, UI_STATUS_H);
	lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
	lv_obj_set_style_border_width(bar, 1, 0);
	lv_obj_set_style_border_color(bar, p->accent2, 0);
	lv_obj_set_style_border_opa(bar, LV_OPA_60, 0);
	lv_obj_set_style_pad_hor(bar, 6, 0);
	lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_event_cb(bar, status_deleted, LV_EVENT_DELETE, NULL);
	status.bar = bar;

	gethostname(host, sizeof(host) - 1);
	l = label(bar, ui_font(16), p->accent, title ? title : "WT41N0");
	lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
	if (!title) {
		lv_obj_t *sub = label(bar, ui_font(12), p->dim, "");
		lv_label_set_text_fmt(sub, "// %s", host);
		lv_label_set_long_mode(sub, LV_LABEL_LONG_CLIP);
		lv_obj_set_width(sub, 90);
		lv_obj_align_to(sub, l, LV_ALIGN_OUT_RIGHT_MID, 6, 1);
	}

	right = lv_obj_create(bar);
	lv_obj_remove_style_all(right);
	lv_obj_set_size(right, 160, LV_PCT(100));	/* fixed: its labels change width */
	lv_obj_align(right, LV_ALIGN_RIGHT_MID, 0, 0);
	lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_column(right, 8, 0);
	status.layers = label(right, ui_font(12), p->accent2, "");
	status.wifi = label(right, ui_font(12), p->line, LV_SYMBOL_WIFI);
	status.battery = label(right, ui_font(12), p->text, "");
	status.clock = label(right, ui_font(16), p->text, "--:--");

	content = lv_obj_create(scr);
	lv_obj_remove_style_all(content);
	lv_obj_set_pos(content, 0, UI_STATUS_H);
	lv_obj_set_size(content, UI_WIDTH, UI_HEIGHT - UI_STATUS_H - UI_HINT_H);
	lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);

	hint = lv_obj_create(scr);
	lv_obj_remove_style_all(hint);
	lv_obj_set_size(hint, UI_WIDTH, UI_HINT_H);
	lv_obj_set_pos(hint, 0, UI_HEIGHT - UI_HINT_H);
	lv_obj_set_style_border_side(hint, LV_BORDER_SIDE_TOP, 0);
	lv_obj_set_style_border_width(hint, 1, 0);
	lv_obj_set_style_border_color(hint, p->line, 0);
	lv_obj_set_style_pad_hor(hint, 6, 0);
	status.hint = label(hint, ui_font(12), p->dim, "");
	lv_obj_align(status.hint, LV_ALIGN_LEFT_MID, 0, 0);

	lv_group_remove_all_objs(group);
	keys_set_back(NULL);
	old = lv_screen_active();
	lv_screen_load(scr);
	if (old && old != scr)
		lv_obj_delete(old);
	if (!timer)
		timer = lv_timer_create(status_timer, 1000, NULL);
	status_refresh();
	return content;
}

void ui_hint(const char *text)
{
	if (status.hint)
		lv_label_set_text(status.hint, text);
}
