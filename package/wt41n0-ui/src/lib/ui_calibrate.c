/*
 * Touch calibration, as the stock OS does it: tap a crosshair at the centre, the four
 * corners, then the centre again. The first five taps give a least-squares affine
 * mapping from raw panel readings to the screen (it covers swapped or mirrored axes,
 * scale and offset); the last one checks it. Saved as "touch_cal=" in ui.conf.
 */
#include <math.h>
#include <stdio.h>
#include "ui_internal.h"

#define NPOINTS		6
#define MAX_ERROR	10	/* px allowed for the check tap */
#define MIN_SAMPLES	3

static const lv_point_t targets[NPOINTS] = {
	{ 160, 120 }, { 24, 24 }, { 296, 24 }, { 296, 216 }, { 24, 216 }, { 160, 120 },
};

static struct {
	lv_obj_t *cross, *step, *msg;
	lv_timer_t *timer;
	void (*done)(bool saved);
	int index;
	bool was_down;
	long sum_x, sum_y;
	int samples;
	double raw_x[NPOINTS], raw_y[NPOINTS];
} cal;

bool ui_touch_present(void)
{
	return platform_has_touch();
}

bool ui_touch_calibrated(void)
{
	char buf[256];

	return conf_get("touch_cal", buf, sizeof(buf)) == 0;
}

/* Solve the 3x3 system m * r = v by Cramer's rule; returns false if singular */
static bool solve3(const double m[3][3], const double v[3], double r[3])
{
	double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
		     m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
		     m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
	int c, i, j;

	if (fabs(det) < 1e-9)
		return false;
	for (c = 0; c < 3; c++) {
		double t[3][3];

		for (i = 0; i < 3; i++)
			for (j = 0; j < 3; j++)
				t[i][j] = j == c ? v[i] : m[i][j];
		r[c] = (t[0][0] * (t[1][1] * t[2][2] - t[1][2] * t[2][1]) -
			t[0][1] * (t[1][0] * t[2][2] - t[1][2] * t[2][0]) +
			t[0][2] * (t[1][0] * t[2][1] - t[1][1] * t[2][0])) / det;
	}
	return true;
}

/* Least-squares fit over the first five points: screen = a * rx + b * ry + c */
static bool fit(double out[6])
{
	double m[3][3] = { { 0 } }, vx[3] = { 0 }, vy[3] = { 0 };
	int i;

	for (i = 0; i < 5; i++) {
		double rx = cal.raw_x[i], ry = cal.raw_y[i];

		m[0][0] += rx * rx; m[0][1] += rx * ry; m[0][2] += rx;
		m[1][1] += ry * ry; m[1][2] += ry;
		vx[0] += rx * targets[i].x; vx[1] += ry * targets[i].x; vx[2] += targets[i].x;
		vy[0] += rx * targets[i].y; vy[1] += ry * targets[i].y; vy[2] += targets[i].y;
	}
	m[1][0] = m[0][1]; m[2][0] = m[0][2]; m[2][1] = m[1][2]; m[2][2] = 5;
	return solve3(m, vx, out) && solve3(m, vy, out + 3);
}

static void show_target(void)
{
	lv_obj_set_pos(cal.cross, targets[cal.index].x - 16, targets[cal.index].y - 16);
	lv_label_set_text_fmt(cal.step, "%d/%d", cal.index + 1, NPOINTS);
}

static void finish(bool saved)
{
	void (*done)(bool) = cal.done;

	lv_timer_delete(cal.timer);
	cal.timer = NULL;
	platform_touch_enable(true);
	if (done)
		done(saved);
}

static void complete(void)
{
	double c[6], x, y, err;
	char buf[256];

	if (!fit(c)) {
		lv_label_set_text(cal.msg, "CALIBRATION FAILED // TRY AGAIN");
		cal.index = 0;
		show_target();
		return;
	}
	x = c[0] * cal.raw_x[5] + c[1] * cal.raw_y[5] + c[2];
	y = c[3] * cal.raw_x[5] + c[4] * cal.raw_y[5] + c[5];
	err = hypot(x - targets[5].x, y - targets[5].y);
	if (err > MAX_ERROR) {
		lv_label_set_text_fmt(cal.msg, "MISSED BY %d PX // TRY AGAIN", (int)err);
		cal.index = 0;
		show_target();
		return;
	}
	snprintf(buf, sizeof(buf), "%.8g %.8g %.8g %.8g %.8g %.8g", c[0], c[1], c[2], c[3], c[4], c[5]);
	conf_set("touch_cal", buf);
	platform_touch_set_cal(c);
	finish(true);
}

static void poll_cb(lv_timer_t *t)
{
	int x, y;
	bool down = platform_touch_raw(&x, &y);

	(void)t;
	if (down) {
		if (cal.was_down) {	/* skip the first reading of a press: it is noisy */
			cal.sum_x += x;
			cal.sum_y += y;
			cal.samples++;
		}
		cal.was_down = true;
		return;
	}
	if (!cal.was_down)
		return;
	cal.was_down = false;
	if (cal.samples >= MIN_SAMPLES) {
		cal.raw_x[cal.index] = (double)cal.sum_x / cal.samples;
		cal.raw_y[cal.index] = (double)cal.sum_y / cal.samples;
		if (++cal.index == NPOINTS)
			complete();
		else
			show_target();
	}
	cal.sum_x = cal.sum_y = 0;
	cal.samples = 0;
}

static void key_cb(lv_event_t *e)
{
	if (lv_event_get_key(e) == LV_KEY_ESC && cal.timer)
		finish(false);
}

static lv_obj_t *bar(lv_obj_t *parent, int w, int h, int x, int y, lv_color_t color)
{
	lv_obj_t *o = lv_obj_create(parent);

	lv_obj_remove_style_all(o);
	lv_obj_set_size(o, w, h);
	lv_obj_set_pos(o, x, y);
	lv_obj_set_style_bg_color(o, color, 0);
	lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
	return o;
}

void ui_touch_calibrate(void (*done)(bool saved))
{
	const ui_palette_t *p = ui_palette();
	lv_obj_t *scr, *old, *ring, *title, *keys;

	scr = lv_obj_create(NULL);
	lv_obj_remove_style_all(scr);
	lv_obj_set_style_bg_color(scr, p->bg, 0);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
	lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

	title = lv_label_create(scr);
	lv_label_set_text(title, "TOUCH CALIBRATION");
	lv_obj_set_style_text_font(title, ui_font(16), 0);
	lv_obj_set_style_text_color(title, p->accent, 0);
	lv_obj_align(title, LV_ALIGN_CENTER, 0, -46);
	cal.msg = lv_label_create(scr);
	lv_label_set_text(cal.msg, "TAP THE CENTRE OF THE CROSSHAIR");
	lv_obj_set_style_text_font(cal.msg, ui_font(12), 0);
	lv_obj_set_style_text_color(cal.msg, p->text, 0);
	lv_obj_align(cal.msg, LV_ALIGN_CENTER, 0, 40);
	cal.step = lv_label_create(scr);
	lv_obj_set_style_text_font(cal.step, ui_font(12), 0);
	lv_obj_set_style_text_color(cal.step, p->dim, 0);
	lv_obj_align(cal.step, LV_ALIGN_CENTER, 0, 58);
	keys = lv_label_create(scr);
	lv_label_set_text(keys, "ESC SKIP");
	lv_obj_set_style_text_font(keys, ui_font(12), 0);
	lv_obj_set_style_text_color(keys, p->dim, 0);
	lv_obj_align(keys, LV_ALIGN_CENTER, 0, 76);

	/* crosshair: 32x32, a ring and two lines */
	cal.cross = lv_obj_create(scr);
	lv_obj_remove_style_all(cal.cross);
	lv_obj_set_size(cal.cross, 33, 33);
	bar(cal.cross, 33, 1, 0, 16, p->accent);
	bar(cal.cross, 1, 33, 16, 0, p->accent);
	ring = lv_obj_create(cal.cross);
	lv_obj_remove_style_all(ring);
	lv_obj_set_size(ring, 15, 15);
	lv_obj_set_pos(ring, 9, 9);
	lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_border_color(ring, p->accent2, 0);
	lv_obj_set_style_border_width(ring, 2, 0);

	/* Esc through the keypad group */
	lv_group_remove_all_objs(ui_group());
	lv_obj_add_event_cb(scr, key_cb, LV_EVENT_KEY, NULL);
	lv_group_add_obj(ui_group(), scr);
	lv_group_focus_obj(scr);

	cal.done = done;
	cal.index = 0;
	cal.was_down = false;
	cal.samples = 0;
	cal.sum_x = cal.sum_y = 0;
	show_target();
	platform_touch_enable(false);
	cal.timer = lv_timer_create(poll_cb, 10, NULL);

	old = lv_screen_active();
	lv_screen_load(scr);
	if (old && old != scr)
		lv_obj_delete(old);
}
