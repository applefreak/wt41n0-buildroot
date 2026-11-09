/*
 * Controls (SPEC.md, Components; CYBER drawing): slider card, toggle row, buttons (wide,
 * small, big with arm-and-confirm), status line, card. Positions are absolute (tokens);
 * these helpers place them in the content area.
 */
#include <stdio.h>
#include <string.h>
#include "ui_internal.h"

#define CY(y)	((y) - WT_CONTENT_Y)
#define BORDER	1

static lv_obj_t *label(lv_obj_t *parent, ui_role_t role, lv_color_t color, const char *s)
{
	lv_obj_t *l = lv_label_create(parent);

	ui_text_role(l, role);
	lv_obj_set_style_text_color(l, color, 0);
	lv_label_set_text(l, s);
	return l;
}

static lv_obj_t *rect(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t color)
{
	lv_obj_t *o = lv_obj_create(parent);

	lv_obj_remove_style_all(o);
	lv_obj_set_pos(o, x, y);
	lv_obj_set_size(o, w, h);
	lv_obj_set_style_bg_color(o, color, 0);
	lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
	lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
	return o;
}

/* ---- slider card ----------------------------------------------------------------- */

struct slider_card {
	lv_obj_t *card, *value, *up, *down, *chip, *fill, *knob;
	int min, max, step, val, saved;
	const char *unit;
	void (*changed)(int value);
	bool adjusting;
};

#define TRACK_X	WT_SLIDER_TRACK_X
#define TRACK_Y	WT_SLIDER_TRACK_Y
#define TRACK_W	WT_SLIDER_TRACK_W
#define KNOB_W	12

static void card_hint(struct slider_card *c)
{
	char marker[16];

	snprintf(marker, sizeof(marker), "%d%s", c->val, c->unit);
	if (c->adjusting)
		ui_hint_keys("\xE2\x86\x91\xE2\x86\x93 ADJUST|ENT SET|ESC CANCEL", marker);
	else
		ui_hint_keys("ENT ADJUST|ESC BACK", marker);
}

static void card_draw(struct slider_card *c)
{
	const ui_palette_t *p = ui_palette();
	int span = c->max - c->min, x = span ? (c->val - c->min) * TRACK_W / span : 0;

	lv_label_set_text_fmt(c->value, "%d %s", c->val, c->unit);
	lv_obj_set_width(c->fill, x > 0 ? x : 1);
	wt_style_t s = ui_theme_style();
	int kw = lv_obj_get_width(c->knob);

	lv_obj_set_style_bg_color(c->fill, c->adjusting || s == WT_STYLE_NUKE || s == WT_STYLE_DECK ? p->accent : p->text, 0);
	lv_obj_set_x(c->knob, TRACK_X - BORDER + x - kw / 2);
	switch (s) {
	case WT_STYLE_AERO:	/* disc: accent adjusting, text otherwise */
		lv_obj_set_style_bg_color(c->knob, c->adjusting ? p->accent : p->text, 0);
		break;
	case WT_STYLE_DECK:	/* bevelled block: accent adjusting, line otherwise */
		lv_obj_set_style_bg_color(c->knob, c->adjusting ? p->accent : p->line, 0);
		break;
	default:		/* outlined block, filled adjusting */
		lv_obj_set_style_bg_opa(c->knob, c->adjusting ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
		break;
	}
	lv_obj_set_style_border_width(c->card, c->adjusting ? 2 : s == WT_STYLE_DECK ? 0 : 1, 0);
	lv_obj_set_style_border_color(c->card, c->adjusting ? p->accent : s == WT_STYLE_CYBER ? p->text_edge : p->line, 0);
	if (c->adjusting) {
		lv_obj_remove_flag(c->chip, LV_OBJ_FLAG_HIDDEN);
		lv_obj_remove_flag(c->up, LV_OBJ_FLAG_HIDDEN);
		lv_obj_remove_flag(c->down, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_add_flag(c->chip, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(c->up, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(c->down, LV_OBJ_FLAG_HIDDEN);
	}
	lv_obj_align_to(c->up, c->value, LV_ALIGN_OUT_LEFT_MID, -10, 0);
	lv_obj_align_to(c->down, c->value, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
}

static void card_set(struct slider_card *c, int v)
{
	if (v < c->min)
		v = c->min;
	if (v > c->max)
		v = c->max;
	if (v == c->val)
		return;
	c->val = v;
	card_draw(c);
	card_hint(c);
	if (c->changed)
		c->changed(v);
}

static void card_event(lv_event_t *e)
{
	struct slider_card *c = lv_event_get_user_data(e);
	lv_event_code_t code = lv_event_get_code(e);

	if (code == LV_EVENT_DELETE) {
		lv_free(c);
		return;
	}
	if (code == LV_EVENT_FOCUSED) {
		card_hint(c);
	} else if (code == LV_EVENT_KEY) {
		uint32_t key = lv_event_get_key(e);

		if (key == LV_KEY_ENTER) {		/* start adjusting, or set */
			c->adjusting = !c->adjusting;
			c->saved = c->val;
			lv_group_set_editing(ui_group(), c->adjusting);
		} else if (key == LV_KEY_ESC && c->adjusting) {	/* cancel */
			c->adjusting = false;
			card_set(c, c->saved);
		} else if (c->adjusting && (key == LV_KEY_UP || key == LV_KEY_RIGHT)) {
			card_set(c, c->val + c->step);
		} else if (c->adjusting && (key == LV_KEY_DOWN || key == LV_KEY_LEFT)) {
			card_set(c, c->val - c->step);
		}
		card_draw(c);
		card_hint(c);
	} else if (code == LV_EVENT_PRESSING) {
		/* touch: the value under the finger, along the track */
		lv_indev_t *in = lv_indev_active();
		lv_point_t pt;
		lv_area_t a;

		if (!in || lv_indev_get_type(in) != LV_INDEV_TYPE_POINTER)
			return;
		lv_indev_get_point(in, &pt);
		lv_obj_get_coords(c->card, &a);
		if (pt.y < a.y1 + TRACK_Y - 20 || pt.y > a.y1 + TRACK_Y + 30)
			return;
		{
			int x = pt.x - (a.x1 + TRACK_X), span = c->max - c->min;
			int v = c->min + x * span / TRACK_W;

			card_set(c, (v + c->step / 2) / c->step * c->step);
		}
	}
}

lv_obj_t *ui_slider_card(lv_obj_t *content, const char *icon, const char *name, const char *unit,
			 int min, int max, int value, void (*changed)(int value))
{
	const ui_palette_t *p = ui_palette();
	struct slider_card *c = lv_malloc_zeroed(sizeof(*c));
	lv_obj_t *card, *i, *track, *t;
	int k;

	card = ui_panel_kind(content, UI_PANEL_CARD, WT_INNER_X, CY(WT_INNER_Y), WT_INNER_W, WT_SLIDER_CARD_H, true);
	c->card = card;
	c->min = min;
	c->max = max;
	c->step = WT_SLIDER_STEP;
	c->val = value;
	c->unit = unit;
	c->changed = changed;

	i = ui_icon_sized(card, icon, 18);		/* label row at y 10 */
	lv_obj_set_pos(i, 10 - BORDER, 10 - BORDER);
	ui_icon_color(i, p->accent);
	t = label(card, UI_ROLE_NAME, p->text, name);
	lv_obj_set_pos(t, 10 - BORDER + 18 + 8, 10 - BORDER + 1);
	c->chip = ui_chip(card, UI_ROLE_DATA, "ADJUSTING");
	lv_obj_align(c->chip, LV_ALIGN_TOP_RIGHT, -(10 - BORDER), 10 - BORDER + 2);

	c->value = label(card, UI_ROLE_BIG, p->text, "");	/* value centred at y 46 */
	lv_obj_align(c->value, LV_ALIGN_TOP_MID, 0, 46 - BORDER);
	c->up = lv_label_create(card);
	lv_label_set_text(c->up, LV_SYMBOL_UP);
	lv_obj_set_style_text_font(c->up, &lv_font_montserrat_14, 0);
	lv_obj_set_style_text_color(c->up, p->accent, 0);
	c->down = lv_label_create(card);
	lv_label_set_text(c->down, LV_SYMBOL_DOWN);
	lv_obj_set_style_text_font(c->down, &lv_font_montserrat_14, 0);
	lv_obj_set_style_text_color(c->down, p->accent, 0);

	if (ui_theme_style() == WT_STYLE_DECK) {
		track = ui_well(card, TRACK_X - BORDER, TRACK_Y - BORDER, TRACK_W, WT_SLIDER_TRACK_H);
	} else {
		track = rect(card, TRACK_X - BORDER, TRACK_Y - BORDER, TRACK_W, WT_SLIDER_TRACK_H, p->line);
		if (ui_theme_style() == WT_STYLE_AERO)
			lv_obj_set_style_radius(track, 4, 0);
	}
	c->fill = rect(card, TRACK_X - BORDER, TRACK_Y - BORDER, 1, WT_SLIDER_TRACK_H, p->text);
	switch (ui_theme_style()) {
	case WT_STYLE_AERO:	/* 24 px disc, 2 px text white ring */
		lv_obj_set_style_radius(c->fill, 4, 0);
		c->knob = rect(card, 0, 0, 24, 24, p->text);
		lv_obj_set_style_radius(c->knob, LV_RADIUS_CIRCLE, 0);
		lv_obj_set_style_border_color(c->knob, p->text_white, 0);
		lv_obj_set_style_border_width(c->knob, 2, 0);
		break;
	case WT_STYLE_DECK:	/* 14 x 24 bevelled block */
		c->knob = rect(card, 0, 0, 14, WT_SLIDER_KNOB_H, p->line);
		lv_obj_set_style_radius(c->knob, 2, 0);
		lv_obj_set_style_border_color(c->knob, p->dark, 0);
		lv_obj_set_style_border_side(c->knob, LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_RIGHT, 0);
		lv_obj_set_style_border_width(c->knob, 2, 0);
		break;
	case WT_STYLE_NUKE:	/* 12 x 24 block, 2 px text border */
		c->knob = rect(card, 0, 0, KNOB_W, WT_SLIDER_KNOB_H, p->text);
		lv_obj_set_style_border_color(c->knob, p->text, 0);
		lv_obj_set_style_border_width(c->knob, 2, 0);
		break;
	default:		/* 12 x 24, 1 px accent border */
		c->knob = rect(card, 0, 0, KNOB_W, WT_SLIDER_KNOB_H, p->accent);
		lv_obj_set_style_border_color(c->knob, p->accent, 0);
		lv_obj_set_style_border_width(c->knob, 1, 0);
		break;
	}
	lv_obj_set_y(c->knob, TRACK_Y - BORDER + WT_SLIDER_TRACK_H / 2 - lv_obj_get_style_height(c->knob, 0) / 2);

	for (k = 0; k <= 10; k++)			/* 11 ticks at y 136 */
		rect(card, TRACK_X - BORDER + k * (TRACK_W - 1) / 10, 136 - BORDER, 1, k % 5 ? 3 : 5, p->text_tick);
	t = label(card, UI_ROLE_DATA, p->dim, "0");	/* 0 / 50 / 100 at y 146 */
	lv_obj_set_pos(t, TRACK_X - BORDER, 146 - BORDER);
	t = label(card, UI_ROLE_DATA, p->dim, "50");
	lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 146 - BORDER);
	t = label(card, UI_ROLE_DATA, p->dim, "100");
	lv_obj_align(t, LV_ALIGN_TOP_RIGHT, -(TRACK_X - BORDER), 146 - BORDER);

	lv_obj_add_event_cb(card, card_event, LV_EVENT_ALL, c);
	card_draw(c);
	return card;
}

/* ---- toggle row ------------------------------------------------------------------- */

struct toggle_row {
	lv_obj_t *toggle, *state;
	void (*changed)(lv_obj_t *row, bool on);
};

static void toggle_draw(lv_obj_t *row, bool on)
{
	const ui_palette_t *p = ui_palette();
	struct toggle_row *t = ui_panel_data(row);

	ui_toggle_set(t->toggle, on);
	lv_label_set_text(t->state, on ? "ON" : "OFF");
	lv_obj_set_style_text_color(t->state, on ? p->accent : p->dim, 0);
}

static void toggle_event(lv_event_t *e)
{
	lv_obj_t *row = lv_event_get_current_target(e);
	struct toggle_row *t = lv_event_get_user_data(e);
	bool on;

	if (lv_event_get_code(e) == LV_EVENT_DELETE) {
		lv_free(t);
		return;
	}
	if (lv_event_get_code(e) != LV_EVENT_CLICKED)
		return;
	on = !lv_obj_has_state(row, LV_STATE_CHECKED);
	if (on)
		lv_obj_add_state(row, LV_STATE_CHECKED);
	else
		lv_obj_remove_state(row, LV_STATE_CHECKED);
	toggle_draw(row, on);
	if (t->changed)
		t->changed(row, on);
}

lv_obj_t *ui_toggle_row(lv_obj_t *content, int y, const char *name, bool on,
			void (*changed)(lv_obj_t *row, bool on))
{
	const ui_palette_t *p = ui_palette();
	lv_obj_t *row = ui_panel_kind(content, UI_PANEL_ROW, WT_INNER_X, CY(y), WT_INNER_W, WT_TOGGLE_ROW_H, true), *l;
	struct toggle_row *t = lv_malloc_zeroed(sizeof(*t));

	t->changed = changed;
	ui_panel_set_data(row, t);
	t->toggle = ui_toggle(row);
	lv_obj_align(t->toggle, LV_ALIGN_LEFT_MID, 8, 0);
	l = label(row, UI_ROLE_NAME, p->text, name);
	lv_obj_align_to(l, t->toggle, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
	t->state = label(row, UI_ROLE_DATA, p->dim, "");
	lv_obj_align(t->state, LV_ALIGN_RIGHT_MID, -10, 0);
	if (on)
		lv_obj_add_state(row, LV_STATE_CHECKED);
	toggle_draw(row, on);
	lv_obj_add_event_cb(row, toggle_event, LV_EVENT_ALL, t);
	return row;
}

/* ---- buttons ---------------------------------------------------------------------- */

lv_obj_t *ui_button(lv_obj_t *content, int x, int y, int w, int h, const char *icon, const char *name)
{
	const ui_palette_t *p = ui_palette();
	lv_obj_t *b = ui_panel_kind(content, UI_PANEL_BUTTON, x, CY(y), w, h, true), *in, *t;
	bool stacked = h >= 40 && w < 80;	/* small button: icon over label */

	in = lv_obj_create(b);			/* child 2: icon + label */
	lv_obj_remove_style_all(in);
	lv_obj_set_size(in, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
	lv_obj_set_flex_flow(in, stacked ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(in, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_gap(in, stacked ? 1 : 8, 0);
	lv_obj_remove_flag(in, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(in, LV_OBJ_FLAG_OVERFLOW_VISIBLE);	/* Aldrich draws above its box */
	lv_obj_center(in);
	if (icon)
		ui_icon_color(ui_icon_sized(in, icon, stacked ? 14 : 18), p->text);
	t = label(in, UI_ROLE_NAME, p->text, name);
	(void)t;
	return b;
}

void ui_button_set_disabled(lv_obj_t *b, bool disabled)
{
	/* still focusable; the caller ignores its clicks and the hint shows no ENT */
	lv_obj_set_style_opa(b, disabled ? WT_OPA_DISABLED : LV_OPA_COVER, 0);
	if (disabled)
		lv_obj_add_state(b, LV_STATE_USER_1);
	else
		lv_obj_remove_state(b, LV_STATE_USER_1);
}

bool ui_button_disabled(lv_obj_t *b)
{
	return lv_obj_has_state(b, LV_STATE_USER_1);
}

/* Big button: arm on the first ENT, act on the second within WT_POWER_CONFIRM_MS */
#define SEGS 5

struct big {
	lv_obj_t *btn, *name, *sub, *seg[SEGS];
	char label[32], confirm[48];
	void (*act)(void *data);
	void *data;
	lv_timer_t *timer;
	int left;			/* seconds */
	const char *idle_hint;
};

static void big_disarm(struct big *g)
{
	const ui_palette_t *p = ui_palette();
	int i;

	if (g->timer)
		lv_timer_delete(g->timer);
	g->timer = NULL;
	lv_label_set_text(g->name, g->label);
	lv_obj_set_style_text_color(g->name, p->text, 0);
	lv_obj_set_style_border_color(g->btn, ui_theme_style() == WT_STYLE_CYBER ? p->text_edge : p->line, 0);
	for (i = 0; i < SEGS; i++)
		lv_obj_add_flag(g->seg[i], LV_OBJ_FLAG_HIDDEN);
	lv_obj_remove_flag(g->sub, LV_OBJ_FLAG_HIDDEN);
	ui_focus_ring_color(NULL);
	if (lv_group_get_focused(ui_group()) == g->btn)
		lv_group_set_editing(ui_group(), false);
	ui_hint_keys(g->idle_hint, "");
}

static void big_tick(lv_timer_t *t)
{
	struct big *g = lv_timer_get_user_data(t);
	const ui_palette_t *p = ui_palette();
	int i;

	if (--g->left <= 0) {
		big_disarm(g);
		return;
	}
	for (i = 0; i < SEGS; i++)
		lv_obj_set_style_bg_color(g->seg[i], i < g->left ? p->warn : p->line, 0);
}

static void big_arm(struct big *g)
{
	const ui_palette_t *p = ui_palette();
	lv_color_t warn = p->warn;
	int i;

	g->left = WT_POWER_CONFIRM_MS / 1000;
	lv_label_set_text(g->name, g->confirm);
	lv_obj_set_style_text_color(g->name, p->warn, 0);
	lv_obj_set_style_border_color(g->btn, p->warn, 0);
	lv_obj_add_flag(g->sub, LV_OBJ_FLAG_HIDDEN);
	for (i = 0; i < SEGS; i++) {
		lv_obj_remove_flag(g->seg[i], LV_OBJ_FLAG_HIDDEN);
		lv_obj_set_style_bg_color(g->seg[i], p->warn, 0);
	}
	ui_focus_ring_color(&warn);
	lv_group_set_editing(ui_group(), true);	/* ESC comes to the button */
	ui_hint_keys("ENT CONFIRM|ESC CANCEL", "CONFIRM?");
	g->timer = lv_timer_create(big_tick, 1000, g);
}

static void big_event(lv_event_t *e)
{
	struct big *g = lv_event_get_user_data(e);

	switch (lv_event_get_code(e)) {
	case LV_EVENT_CLICKED:
		if (g->timer) {
			big_disarm(g);
			g->act(g->data);
		} else {
			big_arm(g);
		}
		break;
	case LV_EVENT_KEY:
		if (lv_event_get_key(e) == LV_KEY_ESC && g->timer)
			big_disarm(g);
		break;
	case LV_EVENT_DEFOCUSED:
		if (g->timer)
			big_disarm(g);
		break;
	case LV_EVENT_DELETE:
		if (g->timer)
			lv_timer_delete(g->timer);
		lv_free(g);
		break;
	default:
		break;
	}
}

lv_obj_t *ui_big_button(lv_obj_t *content, int y, const char *icon, const char *name, const char *sub,
			const char *key_label, const char *idle_hint, void (*act)(void *data), void *data)
{
	const ui_palette_t *p = ui_palette();
	struct big *g = lv_malloc_zeroed(sizeof(*g));
	lv_obj_t *b = ui_panel_kind(content, UI_PANEL_BUTTON, WT_INNER_X, CY(y), WT_INNER_W, WT_BTN_BIG_H, true), *i, *t;
	int k;

	g->btn = b;
	g->act = act;
	g->data = data;
	g->idle_hint = idle_hint;
	snprintf(g->label, sizeof(g->label), "%s", name);
	snprintf(g->confirm, sizeof(g->confirm), "CONFIRM %s", name);

	i = ui_style_icon(b, icon, 32);			/* 36 px icon box */
	lv_obj_align(i, LV_ALIGN_LEFT_MID, 12, 0);
	if (ui_theme_style() != WT_STYLE_AERO)
		ui_icon_color(i, p->text);
	g->name = label(b, UI_ROLE_NAME, p->text, name);
	lv_obj_set_pos(g->name, 64, 12);
	g->sub = label(b, UI_ROLE_DATA, p->dim, sub);
	lv_obj_set_pos(g->sub, 64, 34);
	lv_obj_set_width(g->sub, WT_INNER_W - 64 - 60);
	lv_label_set_long_mode(g->sub, LV_LABEL_LONG_WRAP);
	t = label(b, UI_ROLE_DATA, p->dim, key_label);
	lv_obj_align(t, LV_ALIGN_RIGHT_MID, -12, 0);
	for (k = 0; k < SEGS; k++) {			/* 5-segment countdown */
		g->seg[k] = rect(b, 64 + k * 25, 52, 22, 3, p->warn);
		lv_obj_add_flag(g->seg[k], LV_OBJ_FLAG_HIDDEN);
	}
	lv_obj_add_event_cb(b, big_event, LV_EVENT_ALL, g);
	return b;
}

/* ---- status line and card -------------------------------------------------------- */

lv_obj_t *ui_status_line(lv_obj_t *content, enum ui_tone tone, const char *icon, const char *msg,
			 const char *detail)
{
	const ui_palette_t *p = ui_palette();
	lv_color_t c = tone == UI_TONE_WARN ? p->warn : tone == UI_TONE_GOOD ? p->accent2 : p->accent;
	lv_obj_t *s = lv_obj_create(content), *i, *t;

	lv_obj_remove_style_all(s);
	lv_obj_set_pos(s, WT_INNER_X, CY(WT_STATUS_LINE_Y));
	lv_obj_set_size(s, WT_INNER_W, WT_STATUS_LINE_H);
	lv_obj_set_style_border_color(s, c, 0);
	lv_obj_set_style_border_width(s, 1, 0);
	lv_obj_set_style_bg_color(s, p->panel, 0);
	lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(s, ui_panel_radius(UI_PANEL_CARD), 0);
	lv_obj_remove_flag(s, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
	i = ui_icon_sized(s, icon ? icon : "warning", 14);
	ui_icon_color(i, c);
	lv_obj_align(i, LV_ALIGN_LEFT_MID, 6, 0);
	t = label(s, UI_ROLE_NAME, c, msg);
	lv_obj_align(t, LV_ALIGN_LEFT_MID, 6 + 14 + 8, 0);
	t = label(s, UI_ROLE_DATA, p->text, detail ? detail : "");
	lv_obj_align(t, LV_ALIGN_RIGHT_MID, -8, 0);
	return s;
}

lv_obj_t *ui_card(lv_obj_t *content, int x, int y, int w, int h)
{
	return ui_panel(content, x, CY(y), w, h, false);
}
