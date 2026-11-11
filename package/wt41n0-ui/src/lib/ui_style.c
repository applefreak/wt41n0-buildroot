/*
 * The four styles (SPEC.md, Style tables): how panels, the focus ring, numbers, icons,
 * names, toggles and hint keys are drawn. Positions and sizes never depend on the style.
 *
 * A panel (tile, row, card, button, chip, readout strip) is an object with the style's
 * idle / focused / pressed looks and some decoration children (CYBER top line and notch,
 * AERO gloss band, DECK bevel edges and label strip). Its record (struct panel) holds
 * the decoration and a pointer for the owner's data (ui_panel_data).
 */
#include <string.h>
#include "ui_internal.h"

#define MAX_DECO 6

struct panel {
	enum ui_panel_kind kind;
	wt_style_t style;		/* as created: the theme may change before it goes */
	lv_obj_t *deco[MAX_DECO];	/* style decoration */
	int ndeco;
	void *data;			/* the owner's */
};

static lv_color_t over(lv_color_t c, lv_opa_t opa, lv_color_t under)
{
	return lv_color_mix(c, under, opa);
}

static lv_obj_t *deco_rect(struct panel *pd, lv_obj_t *panel, int x, int y, int w, int h, lv_color_t c)
{
	lv_obj_t *o = lv_obj_create(panel);

	lv_obj_remove_style_all(o);
	lv_obj_set_pos(o, x, y);
	lv_obj_set_size(o, w, h);
	lv_obj_set_style_bg_color(o, c, 0);
	lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
	lv_obj_add_flag(o, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
	lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
	if (pd->ndeco < MAX_DECO)
		pd->deco[pd->ndeco++] = o;
	return o;
}

/* 7 x 7 CYBER notch: lower-right triangle as an alpha mask, recoloured per state */
static const uint8_t notch_px[7 * 7] = {
	0, 0, 0, 0, 0, 0, 255,   0, 0, 0, 0, 0, 255, 255,   0, 0, 0, 0, 255, 255, 255,
	0, 0, 0, 255, 255, 255, 255,   0, 0, 255, 255, 255, 255, 255,
	0, 255, 255, 255, 255, 255, 255,   255, 255, 255, 255, 255, 255, 255,
};
static const lv_image_dsc_t notch_img = {
	.header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8, .w = 7, .h = 7, .stride = 7 },
	.data_size = sizeof(notch_px),
	.data = notch_px,
};

int ui_panel_radius(enum ui_panel_kind kind)
{
	const wt_style_metrics_t *m = ui_style_metrics();

	switch (ui_theme_style()) {
	case WT_STYLE_AERO:
		return kind == UI_PANEL_TILE ? m->radius_tile : kind == UI_PANEL_ROW ? m->radius_row :
		       kind == UI_PANEL_KEY ? m->radius_key : kind == UI_PANEL_CHIP ? 8 :
		       kind == UI_PANEL_STRIP ? WT_READOUT_H / 2 : 12;
	case WT_STYLE_DECK:
		return 3;
	default:
		return 0;
	}
}

/* decoration colours for the state */
static void deco_state(lv_obj_t *panel, struct panel *pd)
{
	const ui_palette_t *p = ui_palette();
	bool focused = lv_obj_has_state(panel, LV_STATE_FOCUSED);
	bool pressed = lv_obj_has_state(panel, LV_STATE_PRESSED);

	switch (pd->style) {
	case WT_STYLE_CYBER:	/* top line, notch */
		lv_obj_set_style_bg_color(pd->deco[0], focused ? p->accent : p->text_hilight, 0);
		lv_obj_set_style_image_recolor(pd->deco[1], focused ? p->accent : p->text_tick, 0);
		break;
	case WT_STYLE_DECK:	/* bevel: light top/left, dark right/bottom; inverted pressed */
		if (pd->ndeco < 4)
			break;
		lv_obj_set_style_bg_color(pd->deco[0], pressed ? p->dark : p->highlight, 0);
		lv_obj_set_style_bg_color(pd->deco[1], pressed ? p->dark : p->highlight, 0);
		lv_obj_set_style_bg_color(pd->deco[2], pressed ? p->highlight : p->dark, 0);
		lv_obj_set_style_bg_color(pd->deco[3], pressed ? p->highlight : p->dark, 0);
		break;
	default:
		break;
	}
}

static void panel_event(lv_event_t *e)
{
	lv_obj_t *panel = lv_event_get_current_target(e);
	struct panel *pd = lv_event_get_user_data(e);

	switch (lv_event_get_code(e)) {
	case LV_EVENT_FOCUSED:
	case LV_EVENT_DEFOCUSED:
	case LV_EVENT_PRESSED:
	case LV_EVENT_RELEASED:
	case LV_EVENT_PRESS_LOST:
		deco_state(panel, pd);
		break;
	case LV_EVENT_DELETE:
		lv_free(pd);
		break;
	default:
		break;
	}
}

lv_obj_t *ui_panel_kind(lv_obj_t *parent, enum ui_panel_kind kind, int x, int y, int w, int h, bool focusable)
{
	const ui_palette_t *p = ui_palette();
	struct panel *pd = lv_malloc_zeroed(sizeof(*pd));
	wt_style_t style = ui_theme_style();
	lv_obj_t *panel = lv_obj_create(parent), *o;
	int r = ui_panel_radius(kind);

	pd->kind = kind;
	pd->style = style;
	lv_obj_remove_style_all(panel);
	lv_obj_set_pos(panel, x, y);
	lv_obj_set_size(panel, w, h);
	lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_user_data(panel, pd);
	lv_obj_set_style_radius(panel, r, 0);
	lv_obj_set_style_bg_color(panel, p->panel, 0);
	lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(panel, p->accent_pressed, LV_STATE_PRESSED);
	if (style != WT_STYLE_DECK)
		lv_obj_set_style_bg_color(panel, p->accent_fill, LV_STATE_FOCUSED);

	switch (style) {
	case WT_STYLE_CYBER:
		lv_obj_set_style_border_color(panel, p->text_edge, 0);
		lv_obj_set_style_border_width(panel, 1, 0);
		deco_rect(pd, panel, 0, 0, w - 2, 1, p->text_hilight);		/* top line */
		o = lv_image_create(panel);					/* notch */
		lv_image_set_src(o, &notch_img);
		lv_obj_set_style_image_recolor_opa(o, LV_OPA_COVER, 0);
		lv_obj_align(o, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
		lv_obj_add_flag(o, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
		lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
		pd->deco[pd->ndeco++] = o;
		break;
	case WT_STYLE_AERO:
		/* flat gloss band over the top: 28 px of a tile, 14 of a row */
		lv_obj_set_style_border_color(panel, p->line, 0);
		lv_obj_set_style_border_width(panel, 1, 0);
		lv_obj_set_style_clip_corner(panel, true, 0);
		if (kind != UI_PANEL_KEY)
			deco_rect(pd, panel, 0, 0, w - 2, kind == UI_PANEL_TILE ? 28 : h / 2 - 2 > 14 ? 14 : h / 2 - 2,
				  over(p->text, WT_OPA_TEXT_EDGE, p->panel));
		break;
	case WT_STYLE_DECK:
		if (kind == UI_PANEL_STRIP) {				/* an inset well */
			lv_obj_set_style_bg_color(panel, p->bg, 0);
			lv_obj_set_style_border_color(panel, p->dark, 0);
			lv_obj_set_style_border_side(panel, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT, 0);
			lv_obj_set_style_border_width(panel, 1, 0);
			break;
		}
		deco_rect(pd, panel, 0, 0, w, 1, p->highlight);		/* top */
		deco_rect(pd, panel, 0, 0, 1, h, p->highlight);		/* left */
		deco_rect(pd, panel, w - 2, 0, 2, h, p->dark);		/* right */
		deco_rect(pd, panel, 0, h - 2, w, 2, p->dark);		/* bottom */
		break;
	case WT_STYLE_NUKE:
		lv_obj_set_style_border_color(panel, p->line, 0);
		lv_obj_set_style_border_width(panel, 1, 0);
		break;
	}
	deco_state(panel, pd);
	lv_obj_add_event_cb(panel, panel_event, LV_EVENT_ALL, pd);
	if (focusable) {
		lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
		lv_group_add_obj(ui_group(), panel);
	} else {
		lv_obj_remove_flag(panel, LV_OBJ_FLAG_CLICKABLE);
	}
	return panel;
}

lv_obj_t *ui_panel(lv_obj_t *parent, int x, int y, int w, int h, bool focusable)
{
	return ui_panel_kind(parent, UI_PANEL_CARD, x, y, w, h, focusable);
}

void *ui_panel_data(lv_obj_t *panel)
{
	struct panel *pd = lv_obj_get_user_data(panel);

	return pd ? pd->data : NULL;
}

void ui_panel_set_data(lv_obj_t *panel, void *data)
{
	struct panel *pd = lv_obj_get_user_data(panel);

	if (pd)
		pd->data = data;
}

/* DECK inset well (readouts, fields, toggle boxes, slider track): background fill,
 * 1 px dark top and left, 1 px highlight bottom and right. Other styles: a plain box. */
lv_obj_t *ui_well(lv_obj_t *parent, int x, int y, int w, int h)
{
	const ui_palette_t *p = ui_palette();
	lv_obj_t *o = lv_obj_create(parent), *e;

	lv_obj_remove_style_all(o);
	lv_obj_set_pos(o, x, y);
	lv_obj_set_size(o, w, h);
	lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_color(o, p->bg, 0);
	lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
	if (ui_theme_style() != WT_STYLE_DECK)
		return o;
	lv_obj_set_style_radius(o, 2, 0);
	lv_obj_set_style_border_color(o, p->dark, 0);
	lv_obj_set_style_border_side(o, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT, 0);
	lv_obj_set_style_border_width(o, 1, 0);
	e = lv_obj_create(o);		/* highlight bottom and right */
	lv_obj_remove_style_all(e);
	lv_obj_set_size(e, LV_PCT(100), LV_PCT(100));
	lv_obj_add_flag(e, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
	lv_obj_remove_flag(e, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_style_border_color(e, p->highlight, 0);
	lv_obj_set_style_border_side(e, LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_RIGHT, 0);
	lv_obj_set_style_border_width(e, 1, 0);
	lv_obj_set_style_border_post(e, true, 0);
	lv_obj_align(e, LV_ALIGN_TOP_LEFT, -1, -1);
	return o;
}

/* An accent name chip (readout strip, LINK, ADJUSTING, CALIBRATE): CYBER accent text;
 * AERO an 18 px accent pill; DECK an accent tape label; NUKE inverted text */
lv_obj_t *ui_chip(lv_obj_t *parent, ui_role_t role, const char *text)
{
	const ui_palette_t *p = ui_palette();
	wt_style_t s = ui_theme_style();
	lv_obj_t *l = lv_label_create(parent);

	lv_label_set_text(l, text);
	ui_text_role(l, role);
	if (s == WT_STYLE_CYBER) {
		lv_obj_set_style_text_color(l, p->accent, 0);
		return l;
	}
	lv_obj_set_style_text_color(l, p->bg, 0);
	lv_obj_set_style_bg_color(l, p->accent, 0);
	lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_hor(l, s == WT_STYLE_AERO ? 8 : 4, 0);
	ui_text_pad_ver(l, s == WT_STYLE_AERO ? 2 : 1);
	if (s == WT_STYLE_AERO)
		lv_obj_set_style_radius(l, 9, 0);
	return l;
}

/* ---- focus ring ---------------------------------------------------------------- */

void style_ring(lv_obj_t *ring, lv_obj_t *target, const lv_color_t *override)
{
	const ui_palette_t *p = ui_palette();
	lv_color_t c = override ? *override : p->accent;
	int r = lv_obj_get_style_radius(target, 0);

	lv_obj_set_style_outline_width(ring, 0, 0);
	switch (ui_theme_style()) {
	case WT_STYLE_CYBER:
		lv_obj_set_style_border_width(ring, 1, 0);
		lv_obj_set_style_border_color(ring, c, 0);
		lv_obj_set_style_outline_width(ring, 1, 0);
		lv_obj_set_style_outline_color(ring, override ? *override : p->glow, 0);
		lv_obj_set_style_outline_pad(ring, 1, 0);
		lv_obj_set_style_radius(ring, 0, 0);
		break;
	case WT_STYLE_AERO:
		lv_obj_set_style_border_width(ring, 2, 0);
		lv_obj_set_style_border_color(ring, override ? *override : p->text_white, 0);
		lv_obj_set_style_radius(ring, r, 0);
		break;
	case WT_STYLE_DECK:
		lv_obj_set_style_border_width(ring, 2, 0);
		lv_obj_set_style_border_color(ring, c, 0);
		lv_obj_set_style_radius(ring, r > 1 ? r - 1 : 0, 0);
		break;
	case WT_STYLE_NUKE:
		lv_obj_set_style_border_width(ring, 2, 0);
		lv_obj_set_style_border_color(ring, c, 0);
		lv_obj_set_style_radius(ring, 0, 0);
		break;
	}
}

/* DECK draws its outline 1 px inside the element */
int style_ring_inset(void)
{
	return ui_theme_style() == WT_STYLE_DECK ? 1 : 0;
}

/* ---- numbers -------------------------------------------------------------------- */

lv_obj_t *ui_number(lv_obj_t *parent, const char *text)
{
	const ui_palette_t *p = ui_palette();
	lv_obj_t *n = lv_label_create(parent);

	lv_label_set_text(n, text);
	ui_text_role(n, UI_ROLE_DATA);
	lv_obj_set_style_text_align(n, LV_TEXT_ALIGN_CENTER, 0);
	switch (ui_theme_style()) {
	case WT_STYLE_AERO:	/* 18 px disc */
		lv_obj_set_size(n, 18, 18);
		lv_obj_set_style_radius(n, LV_RADIUS_CIRCLE, 0);
		lv_obj_set_style_bg_opa(n, LV_OPA_COVER, 0);
		lv_obj_set_style_pad_top(n, 2, 0);
		break;
	case WT_STYLE_DECK:	/* 18 x 16 inset well */
		lv_obj_set_size(n, 18, 16);
		lv_obj_set_style_bg_color(n, p->bg, 0);
		lv_obj_set_style_bg_opa(n, LV_OPA_COVER, 0);
		lv_obj_set_style_border_color(n, p->dark, 0);
		lv_obj_set_style_border_side(n, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT, 0);
		lv_obj_set_style_border_width(n, 1, 0);	/* no top padding: the digits sit low in the well otherwise */
		break;
	case WT_STYLE_NUKE:	/* 16 px square */
		lv_obj_set_size(n, 16, 16);
		lv_obj_set_style_border_color(n, p->line, 0);
		lv_obj_set_style_border_width(n, 1, 0);
		lv_obj_set_style_bg_opa(n, LV_OPA_COVER, 0);
		lv_obj_set_style_pad_top(n, 1, 0);
		break;
	default:
		break;
	}
	ui_number_focus(n, false);
	return n;
}

void ui_number_focus(lv_obj_t *n, bool focused)
{
	const ui_palette_t *p = ui_palette();

	switch (ui_theme_style()) {
	case WT_STYLE_AERO:
		lv_obj_set_style_bg_color(n, focused ? p->text : p->line, 0);
		lv_obj_set_style_text_color(n, focused ? p->bg : p->text, 0);
		break;
	case WT_STYLE_NUKE:
		lv_obj_set_style_bg_color(n, focused ? p->text : p->bg, 0);
		lv_obj_set_style_text_color(n, focused ? p->bg : p->dim, 0);
		break;
	default:
		lv_obj_set_style_text_color(n, focused ? p->accent : p->dim, 0);
		break;
	}
}

/* ---- icons ---------------------------------------------------------------------- */

/* An icon as the style shows it on tiles and buttons: AERO puts it in a 32 px ring */
lv_obj_t *ui_style_icon(lv_obj_t *parent, const char *name, int size)
{
	lv_obj_t *icon;

	if (ui_theme_style() == WT_STYLE_AERO) {
		/* a ring of the full size with the icon 4 px smaller inside: the drawn icons
		 * (terminal, barcode, chip) use their whole box and would cross the ring */
		icon = lv_obj_create(parent);
		lv_obj_remove_style_all(icon);
		lv_obj_set_size(icon, size, size);
		lv_obj_remove_flag(icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_style_radius(icon, LV_RADIUS_CIRCLE, 0);
		lv_obj_set_style_border_width(icon, 1, 0);
		lv_obj_set_style_bg_opa(icon, LV_OPA_COVER, 0);
		lv_obj_center(ui_icon_sized(icon, name, size - 4));
	} else {
		icon = ui_icon_sized(parent, name, size);
	}
	ui_style_icon_focus(icon, false);
	return icon;
}

void ui_style_icon_focus(lv_obj_t *icon, bool focused)
{
	const ui_palette_t *p = ui_palette();

	if (ui_theme_style() == WT_STYLE_AERO) {
		lv_obj_set_style_border_color(icon, focused ? p->accent : p->text_white, 0);
		lv_obj_set_style_bg_color(icon, focused ? p->accent : p->panel, 0);
		lv_obj_set_style_bg_opa(icon, focused ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
		ui_icon_color(icon, focused ? p->bg : p->text);
		return;
	}
	ui_icon_color(icon, focused ? p->accent : p->dim);
}

/* ---- names ---------------------------------------------------------------------- */

/* NUKE brackets and inverts the focused element's name: "[ NAME ]" */
void ui_name_focus(lv_obj_t *label, const char *name, bool focused)
{
	const ui_palette_t *p = ui_palette();

	if (ui_theme_style() == WT_STYLE_NUKE) {
		if (focused)
			lv_label_set_text_fmt(label, "[ %s ]", name);
		else
			lv_label_set_text(label, name);
		lv_obj_set_style_bg_color(label, p->accent, 0);
		lv_obj_set_style_bg_opa(label, focused ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
		lv_obj_set_style_pad_hor(label, focused ? 2 : 0, 0);
		lv_obj_set_style_text_color(label, focused ? p->bg : p->text, 0);
		return;
	}
	if (ui_theme_style() == WT_STYLE_DECK) {	/* on a 16 px label strip */
		lv_obj_set_style_text_color(label, focused ? p->bg : p->text, 0);
		lv_obj_set_style_bg_color(label, focused ? p->accent : p->bg, 0);
		lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
		lv_obj_set_style_pad_hor(label, 5, 0);
		ui_text_pad_ver(label, 0);
		return;
	}
	lv_obj_set_style_text_color(label, focused && ui_theme_style() == WT_STYLE_CYBER ? p->accent : p->text, 0);
}

/* ---- toggles ---------------------------------------------------------------------- */

lv_obj_t *ui_toggle(lv_obj_t *parent)
{
	const ui_palette_t *p = ui_palette();
	lv_obj_t *t = lv_obj_create(parent), *k;

	lv_obj_remove_style_all(t);
	lv_obj_remove_flag(t, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
	switch (ui_theme_style()) {
	case WT_STYLE_AERO:	/* 24 x 14 pill, 10 px knob */
		lv_obj_set_size(t, 24, 14);
		lv_obj_set_style_radius(t, 7, 0);
		lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
		k = lv_obj_create(t);
		lv_obj_remove_style_all(k);
		lv_obj_set_size(k, 10, 10);
		lv_obj_set_style_radius(k, 5, 0);
		lv_obj_set_style_bg_color(k, p->text, 0);
		lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
		break;
	case WT_STYLE_DECK:	/* 14 px inset well, 6 px LED */
		lv_obj_set_size(t, 14, 14);
		lv_obj_set_style_bg_color(t, p->bg, 0);
		lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
		lv_obj_set_style_border_color(t, p->dark, 0);
		lv_obj_set_style_border_side(t, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT, 0);
		lv_obj_set_style_border_width(t, 1, 0);
		k = lv_obj_create(t);
		lv_obj_remove_style_all(k);
		lv_obj_set_size(k, 6, 6);
		lv_obj_set_style_radius(k, 3, 0);
		lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
		lv_obj_center(k);
		break;
	case WT_STYLE_NUKE:	/* text [X] / [ ] */
		k = lv_label_create(t);
		ui_text_role(k, UI_ROLE_DATA);
		lv_obj_set_style_text_color(k, p->text, 0);
		lv_obj_set_size(t, 24, 14);
		break;
	default:		/* 14 x 14 box */
		lv_obj_set_size(t, 14, 14);
		lv_obj_set_style_border_color(t, p->text, 0);
		lv_obj_set_style_border_width(t, 1, 0);
		k = lv_obj_create(t);
		lv_obj_remove_style_all(k);
		lv_obj_set_size(k, 8, 8);
		lv_obj_set_style_bg_color(k, p->accent, 0);
		lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
		lv_obj_center(k);
		break;
	}
	return t;
}

void ui_toggle_set(lv_obj_t *t, bool on)
{
	const ui_palette_t *p = ui_palette();
	lv_obj_t *k = lv_obj_get_child(t, 0);

	switch (ui_theme_style()) {
	case WT_STYLE_AERO:
		lv_obj_set_style_bg_color(t, on ? p->accent : p->line, 0);
		lv_obj_set_pos(k, on ? 12 : 2, 2);
		break;
	case WT_STYLE_DECK:
		lv_obj_set_style_bg_color(k, on ? p->accent : p->line, 0);
		break;
	case WT_STYLE_NUKE:
		lv_label_set_text(k, on ? "[X]" : "[ ]");
		break;
	default:
		lv_obj_set_style_bg_opa(k, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
		break;
	}
}

/* ---- hint keys ----------------------------------------------------------------- */

/* A hint key as the style draws it: CYBER accent text; AERO 13 px pill in line; DECK
 * keycap with a 2 px dark bottom edge; NUKE plain text */
lv_obj_t *ui_hint_key(lv_obj_t *parent, const char *key, bool arrows)
{
	const ui_palette_t *p = ui_palette();
	wt_style_t s = ui_theme_style();
	lv_obj_t *cap = lv_obj_create(parent), *k;
	lv_color_t fg = s == WT_STYLE_CYBER ? p->accent : p->text;

	lv_obj_remove_style_all(cap);
	lv_obj_set_size(cap, LV_SIZE_CONTENT, 13);
	lv_obj_remove_flag(cap, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
	if (s == WT_STYLE_AERO || s == WT_STYLE_DECK) {
		lv_obj_set_style_bg_color(cap, p->line, 0);
		lv_obj_set_style_bg_opa(cap, LV_OPA_COVER, 0);
		lv_obj_set_style_pad_hor(cap, 4, 0);
		lv_obj_set_style_radius(cap, s == WT_STYLE_AERO ? 6 : 2, 0);
		if (s == WT_STYLE_DECK) {
			lv_obj_set_style_border_color(cap, p->dark, 0);
			lv_obj_set_style_border_side(cap, LV_BORDER_SIDE_BOTTOM, 0);
			lv_obj_set_style_border_width(cap, 2, 0);
		}
	}
	if (arrows) {
		k = ui_arrows(cap, fg);
	} else {
		k = lv_label_create(cap);
		lv_label_set_text(k, key);
		ui_text_role(k, UI_ROLE_HINT);
		lv_obj_set_style_text_color(k, fg, 0);
	}
	lv_obj_align(k, LV_ALIGN_LEFT_MID, 0, 0);
	return cap;
}
