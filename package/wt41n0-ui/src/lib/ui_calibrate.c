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
	lv_obj_t *scr, *cross, *xh, *xv, *xring, *xdot, *step, *msg, *detail, *ghost, *dots[NPOINTS];
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

#define XHAIR WT_XHAIR_SIZE

static void cross_color(lv_color_t c)
{
	lv_obj_set_style_bg_color(cal.xh, c, 0);
	lv_obj_set_style_bg_color(cal.xv, c, 0);
	lv_obj_set_style_border_color(cal.xring, c, 0);
	lv_obj_set_style_bg_color(cal.xdot, c, 0);
}

/* the instruction sits away from the target: below it for the centre, mid-screen else */
static void show_target(void)
{
	const ui_palette_t *p = ui_palette();
	bool centre = targets[cal.index].x == WT_SCREEN_W / 2;
	int i;

	lv_obj_set_pos(cal.cross, targets[cal.index].x - XHAIR / 2, targets[cal.index].y - XHAIR / 2);
	lv_label_set_text_fmt(cal.step, "STEP %d/%d", cal.index + 1, NPOINTS);
	for (i = 0; i < NPOINTS; i++)
		lv_obj_set_style_bg_color(cal.dots[i], i < cal.index ? p->accent : i == cal.index ? p->text : p->line, 0);
	lv_obj_align(cal.msg, LV_ALIGN_TOP_MID, 0, centre ? 160 : 108);
	lv_obj_align_to(cal.detail, cal.msg, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
}

static void miss(int err, double x, double y)
{
	const ui_palette_t *p = ui_palette();

	cross_color(p->warn);
	lv_obj_set_pos(cal.ghost, (int)x - 10, (int)y - 10);
	lv_obj_remove_flag(cal.ghost, LV_OBJ_FLAG_HIDDEN);
	lv_label_set_text(cal.msg, "TRY AGAIN");
	lv_label_set_text_fmt(cal.detail, "MISSED BY %d PX", err);
	lv_obj_set_style_text_color(cal.detail, p->warn, 0);
	cal.index = 0;
	show_target();
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
		lv_label_set_text(cal.msg, "TRY AGAIN");
		lv_label_set_text(cal.detail, "THE TAPS DID NOT FIT");
		lv_obj_set_style_text_color(cal.detail, ui_palette()->warn, 0);
		cal.index = 0;
		show_target();
		return;
	}
	x = c[0] * cal.raw_x[5] + c[1] * cal.raw_y[5] + c[2];
	y = c[3] * cal.raw_x[5] + c[4] * cal.raw_y[5] + c[5];
	err = hypot(x - targets[5].x, y - targets[5].y);
	if (err > MAX_ERROR) {
		miss((int)err, x, y);
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
		if (cal.index == 0) {		/* a new round: the miss goes */
			cross_color(ui_palette()->accent);
			lv_obj_add_flag(cal.ghost, LV_OBJ_FLAG_HIDDEN);
			lv_label_set_text(cal.msg, "TAP THE CENTRE OF THE CROSSHAIR");
			lv_label_set_text(cal.detail, "");
		}
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
	lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
	return o;
}

static lv_obj_t *circle(lv_obj_t *parent, int d, int x, int y)
{
	lv_obj_t *o = bar(parent, d, d, x, y, ui_palette()->bg);

	lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
	return o;
}

static lv_obj_t *text(lv_obj_t *parent, ui_role_t role, lv_color_t c, const char *s)
{
	lv_obj_t *l = lv_label_create(parent);

	lv_label_set_text(l, s);
	ui_text_role(l, role);
	lv_obj_set_style_text_color(l, c, 0);
	return l;
}

/* Full screen (SPEC.md, Screens > Touch calibration): CALIBRATE chip and STEP n/6 top
 * left, six progress dots top right, the 40 px crosshair (20 px ring, 2 px cross, 4 px dot
 * on a 12 px background disc), the instruction away from the target, ESC SKIP */
void ui_touch_calibrate(void (*done)(bool saved))
{
	const ui_palette_t *p = ui_palette();
	lv_obj_t *scr, *old, *o, *row;
	int i;

	scr = lv_obj_create(NULL);
	cal.scr = scr;
	lv_obj_remove_style_all(scr);
	lv_obj_set_style_bg_color(scr, p->bg, 0);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
	lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

	o = ui_chip(scr, UI_ROLE_NAME, "CALIBRATE");
	lv_obj_set_pos(o, 8, 6);
	cal.step = text(scr, UI_ROLE_NAME, p->text, "");
	lv_obj_align_to(cal.step, o, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
	row = lv_obj_create(scr);
	lv_obj_remove_style_all(row);
	lv_obj_set_size(row, NPOINTS * 9 - 3, 6);
	lv_obj_align(row, LV_ALIGN_TOP_RIGHT, -8, 9);
	for (i = 0; i < NPOINTS; i++) {
		cal.dots[i] = bar(row, 6, 6, i * 9, 0, p->line);
		if (ui_theme_style() == WT_STYLE_AERO)
			lv_obj_set_style_radius(cal.dots[i], LV_RADIUS_CIRCLE, 0);
	}

	cal.msg = text(scr, UI_ROLE_NAME, p->text, "TAP THE CENTRE OF THE CROSSHAIR");
	cal.detail = text(scr, UI_ROLE_DATA, p->dim, "");
	o = ui_hint_key(scr, "ESC", false);
	lv_obj_set_pos(o, 8, WT_HINT_Y + 1);
	o = text(scr, UI_ROLE_HINT, p->dim, "SKIP");
	lv_obj_set_pos(o, 40, WT_HINT_Y + 1);
	o = text(scr, UI_ROLE_HINT, p->dim, "TAP WITH THE STYLUS");
	lv_obj_align(o, LV_ALIGN_TOP_RIGHT, -8, WT_HINT_Y + 1);

	/* ghost ring where a missed tap landed: dashed, eight arcs */
	cal.ghost = lv_obj_create(scr);
	lv_obj_remove_style_all(cal.ghost);
	lv_obj_set_size(cal.ghost, 20, 20);
	lv_obj_remove_flag(cal.ghost, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
	for (i = 0; i < 8; i++) {
		o = lv_arc_create(cal.ghost);
		lv_obj_remove_style_all(o);
		lv_obj_set_size(o, 20, 20);
		lv_arc_set_bg_angles(o, i * 45, i * 45 + 25);
		lv_obj_set_style_arc_width(o, 1, LV_PART_MAIN);
		lv_obj_set_style_arc_color(o, p->warn, LV_PART_MAIN);
		lv_obj_set_style_arc_opa(o, LV_OPA_COVER, LV_PART_MAIN);
		lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
	}
	lv_obj_add_flag(cal.ghost, LV_OBJ_FLAG_HIDDEN);

	/* crosshair: 40 x 40 */
	cal.cross = lv_obj_create(scr);
	lv_obj_remove_style_all(cal.cross);
	lv_obj_set_size(cal.cross, XHAIR, XHAIR);
	lv_obj_remove_flag(cal.cross, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
	cal.xh = bar(cal.cross, XHAIR, 2, 0, XHAIR / 2 - 1, p->accent);
	cal.xv = bar(cal.cross, 2, XHAIR, XHAIR / 2 - 1, 0, p->accent);
	cal.xring = circle(cal.cross, 20, XHAIR / 2 - 10, XHAIR / 2 - 10);
	lv_obj_set_style_bg_opa(cal.xring, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(cal.xring, 1, 0);
	circle(cal.cross, 12, XHAIR / 2 - 6, XHAIR / 2 - 6);
	cal.xdot = circle(cal.cross, 4, XHAIR / 2 - 2, XHAIR / 2 - 2);
	cross_color(p->accent);

	/* Esc through the keypad group (not the previous screen's back action) */
	keys_set_back(NULL);
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
