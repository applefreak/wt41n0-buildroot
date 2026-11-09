/*
 * Icons (SPEC.md: 24 px in a 32 px box on tiles, 18 px in rows). FontAwesome glyphs from
 * LVGL's built-in symbol fonts where it has them; terminal, scanner (barcode), system
 * (chip), display (sun), theme (grid), clock and touch (crosshair) are drawn from lines and
 * rectangles, designed on a 32 px box and scaled. ui_icon_color() recolours all.
 */
#include <string.h>
#include "ui_internal.h"

#define BOX 32

static const struct { const char *name, *symbol; } symbols[] = {
	{ "wifi", LV_SYMBOL_WIFI }, { "settings", LV_SYMBOL_SETTINGS }, { "power", LV_SYMBOL_POWER },
	{ "list", LV_SYMBOL_LIST }, { "keyboard", LV_SYMBOL_KEYBOARD }, { "eye", LV_SYMBOL_EYE_OPEN },
	{ "home", LV_SYMBOL_HOME }, { "image", LV_SYMBOL_IMAGE }, { "audio", LV_SYMBOL_AUDIO },
	{ "volume", LV_SYMBOL_VOLUME_MAX }, { "video", LV_SYMBOL_VIDEO }, { "bluetooth", LV_SYMBOL_BLUETOOTH },
	{ "usb", LV_SYMBOL_USB }, { "battery", LV_SYMBOL_BATTERY_FULL }, { "charge", LV_SYMBOL_CHARGE },
	{ "file", LV_SYMBOL_FILE }, { "folder", LV_SYMBOL_DIRECTORY }, { "edit", LV_SYMBOL_EDIT },
	{ "bell", LV_SYMBOL_BELL }, { "call", LV_SYMBOL_CALL }, { "mail", LV_SYMBOL_ENVELOPE },
	{ "gps", LV_SYMBOL_GPS }, { "refresh", LV_SYMBOL_REFRESH }, { "play", LV_SYMBOL_PLAY },
	{ "download", LV_SYMBOL_DOWNLOAD }, { "upload", LV_SYMBOL_UPLOAD }, { "warning", LV_SYMBOL_WARNING },
	{ "ok", LV_SYMBOL_OK }, { "trash", LV_SYMBOL_TRASH }, { "drive", LV_SYMBOL_DRIVE },
	{ "sd", LV_SYMBOL_SD_CARD }, { "tint", LV_SYMBOL_TINT }, { "check", LV_SYMBOL_OK },
	{ "link", LV_SYMBOL_WIFI },
};

/* drawn icons are designed on a 32 px box and scaled to the requested size */
static int scale = 32;
#define S(v) ((v) * scale / 32)

static lv_obj_t *bar(lv_obj_t *parent, int x, int y, int w, int h)
{
	lv_obj_t *o = lv_obj_create(parent);

	lv_obj_remove_style_all(o);
	lv_obj_set_pos(o, S(x), S(y));
	lv_obj_set_size(o, S(w) > 0 ? S(w) : 1, S(h) > 0 ? S(h) : 1);
	lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
	lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
	return o;
}

static void free_points(lv_event_t *e)
{
	lv_free(lv_event_get_user_data(e));
}

static void line(lv_obj_t *parent, const lv_point_precise_t *pts, int n)
{
	lv_point_precise_t *scaled = lv_malloc(sizeof(*scaled) * n);
	lv_obj_t *l = lv_line_create(parent);
	int i;

	for (i = 0; i < n; i++) {
		scaled[i].x = S(pts[i].x);
		scaled[i].y = S(pts[i].y);
	}
	lv_line_set_points_mutable(l, scaled, n);	/* kept for the line's lifetime */
	lv_obj_set_style_line_width(l, scale >= 24 ? 2 : 1, 0);
	lv_obj_set_style_line_rounded(l, false, 0);
	lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(l, free_points, LV_EVENT_DELETE, scaled);
}

static lv_obj_t *ring(lv_obj_t *parent, int x, int y, int d, int width)
{
	lv_obj_t *o = bar(parent, x, y, d, d);

	lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
	lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_border_width(o, width, 0);
	return o;
}

/* terminal: a chevron and an underline, two strokes */
static void draw_terminal(lv_obj_t *b)
{
	static const lv_point_precise_t chevron[] = { { 6, 9 }, { 13, 16 }, { 6, 23 } };
	static const lv_point_precise_t under[] = { { 16, 24 }, { 26, 24 } };

	line(b, chevron, 3);
	line(b, under, 2);
}

/* scanner: barcode bars */
static void draw_barcode(lv_obj_t *b)
{
	static const uint8_t widths[] = { 2, 1, 1, 3, 1, 2, 1, 1, 3, 1, 2 };
	int x = 6;
	unsigned int i;

	for (i = 0; i < sizeof(widths); i++) {
		bar(b, x, 6, widths[i], 20);
		x += widths[i] + 1;
	}
}

/* system: a chip, 16 px body with three pins a side */
static void draw_chip(lv_obj_t *b)
{
	lv_obj_t *body = bar(b, 8, 8, 16, 16);
	int i;

	lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(body, scale >= 24 ? 2 : 1, 0);
	bar(b, 13, 13, 6, 6);
	for (i = 0; i < 3; i++) {
		int at = 10 + i * 5;

		bar(b, at, 4, 2, 3);
		bar(b, at, 25, 2, 3);
		bar(b, 4, at, 3, 2);
		bar(b, 25, at, 3, 2);
	}
}

/* display: a sun, ring and eight rays */
static void draw_sun(lv_obj_t *b)
{
	static const lv_point_precise_t rays[8][2] = {
		{ { 16, 2 }, { 16, 6 } }, { { 16, 26 }, { 16, 30 } }, { { 2, 16 }, { 6, 16 } },
		{ { 26, 16 }, { 30, 16 } }, { { 6, 6 }, { 9, 9 } }, { { 23, 23 }, { 26, 26 } },
		{ { 26, 6 }, { 23, 9 } }, { { 6, 26 }, { 9, 23 } },
	};
	int i;

	ring(b, 10, 10, 12, scale >= 24 ? 2 : 1);
	for (i = 0; i < 8; i++)
		line(b, rays[i], 2);
}

/* theme: four squares */
static void draw_grid(lv_obj_t *b)
{
	bar(b, 6, 6, 9, 9);
	bar(b, 17, 6, 9, 9);
	bar(b, 6, 17, 9, 9);
	bar(b, 17, 17, 9, 9);
}

/* time zone: a clock face with hands */
static void draw_clock(lv_obj_t *b)
{
	static const lv_point_precise_t hands[] = { { 16, 9 }, { 16, 16 }, { 22, 19 } };

	ring(b, 4, 4, 24, scale >= 24 ? 2 : 1);
	line(b, hands, 3);
}

/* touch calibration: a crosshair */
static void draw_crosshair(lv_obj_t *b)
{
	static const lv_point_precise_t h[] = { { 2, 16 }, { 30, 16 } };
	static const lv_point_precise_t v[] = { { 16, 2 }, { 16, 30 } };

	ring(b, 8, 8, 16, 1);
	line(b, h, 2);
	line(b, v, 2);
}

static const struct { const char *name; void (*draw)(lv_obj_t *); } drawn[] = {
	{ "terminal", draw_terminal }, { "scanner", draw_barcode }, { "system", draw_chip },
	{ "display", draw_sun }, { "theme", draw_grid }, { "clock", draw_clock },
	{ "touch", draw_crosshair },
};

/* "signal0".."signal4": the signal-strength bars (SPEC.md: 4 bars 2 x (3 + 3n); AERO
 * four 4 px dots), bottom-left in the box; unlit ones at 30 % */
static void draw_signal(lv_obj_t *b, int lit)
{
	int i, size = lv_obj_get_style_width(b, 0);

	for (i = 0; i < 4; i++) {
		lv_obj_t *o = lv_obj_create(b);
		int h = ui_theme_style() == WT_STYLE_AERO ? 4 : 3 + 3 * i;

		lv_obj_remove_style_all(o);
		lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
		lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
		if (ui_theme_style() == WT_STYLE_AERO) {
			lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
			lv_obj_set_pos(o, i * 5 - 1, size / 2 - 2);
		} else {
			lv_obj_set_pos(o, size / 2 - 6 + i * 3, size / 2 + 6 - h);
		}
		lv_obj_set_size(o, ui_theme_style() == WT_STYLE_AERO ? 4 : 2, h);
		if (i >= lit)
			lv_obj_set_style_opa(o, LV_OPA_30, 0);
	}
}

/* An icon of size x size px (32 on tiles, 18 in rows) */
lv_obj_t *ui_icon_sized(lv_obj_t *parent, const char *name, int size)
{
	lv_obj_t *b = lv_obj_create(parent);
	unsigned int i;

	lv_obj_remove_style_all(b);
	lv_obj_set_size(b, size, size);
	lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
	scale = size;
	if (!strncmp(name, "signal", 6) && name[6] >= '0' && name[6] <= '4' && !name[7]) {
		draw_signal(b, name[6] - '0');
		return b;
	}
	for (i = 0; i < sizeof(drawn) / sizeof(drawn[0]); i++) {
		if (!strcmp(drawn[i].name, name)) {
			drawn[i].draw(b);
			return b;
		}
	}
	{
		const char *sym = LV_SYMBOL_FILE;
		lv_obj_t *l;

		for (i = 0; i < sizeof(symbols) / sizeof(symbols[0]); i++)
			if (!strcmp(symbols[i].name, name))
				sym = symbols[i].symbol;
		l = lv_label_create(b);
		lv_label_set_text(l, sym);
		lv_obj_set_style_text_font(l, size >= 28 ? ui_font_icons24() :
					   size >= 18 ? &lv_font_montserrat_18 : &lv_font_montserrat_14, 0);
		lv_obj_center(l);
	}
	return b;
}

lv_obj_t *ui_icon(lv_obj_t *parent, const char *name)
{
	return ui_icon_sized(parent, name, 32);
}

void ui_icon_color(lv_obj_t *icon, lv_color_t color)
{
	uint32_t i, n = lv_obj_get_child_count(icon);

	for (i = 0; i < n; i++) {
		lv_obj_t *c = lv_obj_get_child(icon, i);

		if (lv_obj_check_type(c, &lv_label_class))
			lv_obj_set_style_text_color(c, color, 0);
		else if (lv_obj_check_type(c, &lv_line_class))
			lv_obj_set_style_line_color(c, color, 0);
		else {
			lv_obj_set_style_bg_color(c, color, 0);
			lv_obj_set_style_border_color(c, color, 0);
		}
	}
}
