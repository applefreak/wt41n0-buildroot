/*
 * Boot splash (SPEC.md, Screens > Boot): full screen; the device name with the style's
 * mark, a LAUNCHER version chip, a 200 px 20-segment progress bar with a status word and
 * the percentage, and the theme underneath. ui_splash_progress() moves it on.
 */
#include <stdio.h>
#include "ui_internal.h"

#define SEGS 20

static struct {
	lv_obj_t *seg[SEGS], *word, *pct;
} sp;

static lv_obj_t *rect(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t c)
{
	lv_obj_t *o = lv_obj_create(parent);

	lv_obj_remove_style_all(o);
	lv_obj_set_pos(o, x, y);
	lv_obj_set_size(o, w, h);
	lv_obj_set_style_bg_color(o, c, 0);
	lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
	lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
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

/* the style's frame around the whole screen */
static void decor(lv_obj_t *scr)
{
	const ui_palette_t *p = ui_palette();
	static const lv_point_t corner[] = { { 12, 12 }, { 296, 12 }, { 12, 216 }, { 296, 216 } };
	lv_obj_t *o;
	int i;

	switch (ui_theme_style()) {
	case WT_STYLE_CYBER:	/* 12 px corner brackets, 2 px */
		for (i = 0; i < 4; i++) {
			int x = corner[i].x, y = corner[i].y, r = i & 1, b = i >> 1;

			rect(scr, x, b ? y + 10 : y, 12, 2, p->text_bright);
			rect(scr, r ? x + 10 : x, y, 2, 12, p->text_bright);
		}
		break;
	case WT_STYLE_DECK:	/* screws */
		for (i = 0; i < 4; i++) {
			o = rect(scr, i & 1 ? 311 : 4, i >> 1 ? 231 : 4, 5, 5, p->dark);
			lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
			lv_obj_set_style_border_color(o, p->line, 0);
			lv_obj_set_style_border_width(o, 1, 0);
		}
		break;
	case WT_STYLE_NUKE:	/* bezel */
		o = rect(scr, 8, 8, 304, 224, p->bg);
		lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
		lv_obj_set_style_radius(o, 6, 0);
		lv_obj_set_style_border_color(o, p->line, 0);
		lv_obj_set_style_border_width(o, 1, 0);
		break;
	default:
		break;
	}
}

static void splash_deleted(lv_event_t *e)
{
	(void)e;
	sp.word = NULL;
}

void ui_splash(const char *version)
{
	const ui_palette_t *p = ui_palette();
	wt_style_t s = ui_theme_style();
	lv_obj_t *scr = lv_obj_create(NULL), *old = lv_screen_active(), *row, *o;
	char buf[64];
	int i;

	lv_obj_remove_style_all(scr);
	lv_obj_set_style_bg_color(scr, p->bg, 0);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
	lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
	decor(scr);

	/* the mark and the device name */
	row = lv_obj_create(scr);
	lv_obj_remove_style_all(row);
	lv_obj_set_size(row, LV_SIZE_CONTENT, 20);
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_column(row, 10, 0);
	lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 66);
	o = rect(row, 0, 0, s == WT_STYLE_AERO ? 20 : 12, s == WT_STYLE_AERO ? 20 : 12, p->accent);
	if (s == WT_STYLE_AERO)
		lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
	if (s == WT_STYLE_DECK) {	/* a lit LED in a well */
		lv_obj_set_style_border_color(o, p->bg, 0);
		lv_obj_set_style_border_width(o, 3, 0);
		lv_obj_set_style_outline_color(o, p->accent, 0);
		lv_obj_set_style_outline_width(o, 1, 0);
	}
	text(row, UI_ROLE_NAME, p->text, "WT41N0");

	snprintf(buf, sizeof(buf), "LAUNCHER %s", version);
	o = ui_chip(scr, s == WT_STYLE_NUKE ? UI_ROLE_BIG : UI_ROLE_TITLE, buf);
	if (s == WT_STYLE_NUKE)
		lv_obj_set_style_text_font(o, ui_font_role(UI_ROLE_NAME), 0);
	lv_obj_align(o, LV_ALIGN_TOP_MID, 0, 92);

	/* 20 segments over 200 px */
	for (i = 0; i < SEGS; i++) {
		sp.seg[i] = rect(scr, 60 + i * 10, 150, 8, 6, p->line);
		if (s == WT_STYLE_AERO)
			lv_obj_set_style_radius(sp.seg[i], LV_RADIUS_CIRCLE, 0);
	}
	sp.word = text(scr, UI_ROLE_DATA, p->dim, "");
	lv_obj_set_pos(sp.word, 60, 164);
	sp.pct = text(scr, UI_ROLE_DATA, p->dim, "");
	lv_obj_align(sp.pct, LV_ALIGN_TOP_RIGHT, -60, 164);

	snprintf(buf, sizeof(buf), "%s \xC2\xB7 %s / %s", ui_theme_style_name(), ui_theme_palette(), ui_theme_accent());
	for (char *c = buf; *c; c++)
		if (*c >= 'a' && *c <= 'z')
			*c -= 32;
	o = text(scr, UI_ROLE_HINT, p->dim, buf);
	lv_obj_align(o, LV_ALIGN_TOP_MID, 0, 216);

	lv_obj_add_event_cb(scr, splash_deleted, LV_EVENT_DELETE, NULL);
	lv_group_remove_all_objs(ui_group());
	lv_screen_load(scr);
	if (old && old != scr)
		lv_obj_delete(old);
}

void ui_splash_progress(int percent, const char *word)
{
	const ui_palette_t *p = ui_palette();
	int i;

	if (!sp.word)
		return;
	for (i = 0; i < SEGS; i++)
		lv_obj_set_style_bg_color(sp.seg[i], i < percent * SEGS / 100 ? p->accent : p->line, 0);
	lv_label_set_text(sp.word, word);
	lv_label_set_text_fmt(sp.pct, "%d%%", percent);
	lv_obj_align(sp.pct, LV_ALIGN_TOP_RIGHT, -60, 164);
}
