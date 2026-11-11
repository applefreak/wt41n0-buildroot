/*
 * The fixed frame of every screen (SPEC.md, Base layout): status bar, content area,
 * hint bar, and the style's decoration in the 4 px band around the content proper.
 * All positions and sizes come from wt_tokens.h. Only the CYBER style is drawn so far.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "ui_internal.h"

#define SIGNAL_BARS	4
#define BATTERY_SEGS	4
#define ARROWS_UTF8	"\xE2\x86\x91\xE2\x86\x93"	/* "↑↓" in hint keys: drawn, not text */

static struct {
	lv_obj_t *bar;			/* the status bar of the current screen */
	lv_obj_t *layer[3];		/* G, O, B */
	lv_obj_t *signal[SIGNAL_BARS];
	lv_obj_t *bolt, *battery_seg[BATTERY_SEGS], *battery_pct;
	lv_obj_t *clock;
	lv_obj_t *hint;			/* hint bar */
	lv_obj_t *marker;		/* its right-aligned marker */
	char pairs[160], marker_text[24];	/* the current hint, for ui_hint_restore() */
} st;

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

static lv_obj_t *box(lv_obj_t *parent)
{
	lv_obj_t *o = lv_obj_create(parent);

	lv_obj_remove_style_all(o);
	lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
	return o;
}

/* A label as wide as its widest possible text, so changing values ("87%" -> "100%",
 * "18:33" -> "11:11" in the proportional faces) do not shift what sits beside them. */
static void fixed_width(lv_obj_t *label, const char *widest, lv_text_align_t align)
{
	const lv_font_t *f = lv_obj_get_style_text_font(label, 0);
	int32_t w = lv_text_get_width(widest, (uint32_t)strlen(widest), f, lv_obj_get_style_text_letter_space(label, 0));

	lv_obj_set_width(label, w);
	lv_obj_set_style_text_align(label, align, 0);
}

static lv_obj_t *text(lv_obj_t *parent, ui_role_t role, lv_color_t color, const char *s)
{
	lv_obj_t *l = lv_label_create(parent);

	ui_text_role(l, role);
	lv_obj_set_style_text_color(l, color, 0);
	lv_label_set_text(l, s);
	return l;
}

/* ↑↓, drawn as two 1 px arrows in a 13 x 10 box */
lv_obj_t *ui_arrows(lv_obj_t *parent, lv_color_t color)
{
	static const lv_point_precise_t up[] = { { 0, 3 }, { 2, 1 }, { 4, 3 }, { 2, 1 }, { 2, 9 } };
	static const lv_point_precise_t down[] = { { 10, 0 }, { 10, 8 }, { 8, 6 }, { 10, 8 }, { 12, 6 } };
	lv_obj_t *b = box(parent), *l;

	lv_obj_set_size(b, 13, 10);
	l = lv_line_create(b);
	lv_line_set_points(l, up, 5);
	lv_obj_set_style_line_color(l, color, 0);
	lv_obj_set_style_line_width(l, 1, 0);
	l = lv_line_create(b);
	lv_line_set_points(l, down, 5);
	lv_obj_set_style_line_color(l, color, 0);
	lv_obj_set_style_line_width(l, 1, 0);
	return b;
}

/* ---- status bar --------------------------------------------------------------- */

void status_refresh(void)
{
	const ui_palette_t *p = ui_palette();
	static const char letters[3] = { 'G', 'O', 'B' };
	static const int layer_bits[3] = { UI_LAYERS_GRAY, UI_LAYERS_ORANGE, UI_LAYERS_BLUE };
	bool charging = false;
	int layers, q, pct, i, lit;
	char buf[8];
	time_t now;

	if (!st.bar)
		return;
	now = time(NULL);
	strftime(buf, sizeof(buf), "%H:%M", localtime(&now));
	lv_label_set_text(st.clock, buf);

	/* signal: 4 bars, lit by link quality */
	q = ui_wifi_quality();
	lit = q < 0 ? 0 : q >= 75 ? 4 : q >= 50 ? 3 : q >= 25 ? 2 : 1;
	for (i = 0; i < SIGNAL_BARS; i++)
		lv_obj_set_style_bg_color(st.signal[i], i < lit ? (ui_theme_style() == WT_STYLE_AERO ? p->accent : p->text) :
					  p->line, 0);

	/* battery: 4 segments in the box, a bolt while charging */
	pct = ui_battery_percent(&charging);
	lit = pct < 0 ? 0 : (pct + 24) / 25;
	for (i = 0; i < BATTERY_SEGS; i++)
		lv_obj_set_style_bg_opa(st.battery_seg[i], i < lit ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
	if (charging)
		lv_obj_remove_flag(st.bolt, LV_OBJ_FLAG_HIDDEN);
	else
		lv_obj_add_flag(st.bolt, LV_OBJ_FLAG_HIDDEN);
	if (pct < 0)
		lv_label_set_text(st.battery_pct, "--");
	else
		lv_label_set_text_fmt(st.battery_pct, "%d%%", pct);
	lv_obj_set_style_text_color(st.battery_pct, pct >= 0 && pct <= 10 && !charging ? p->warn :
				    ui_theme_style() == WT_STYLE_DECK ? p->accent : p->text, 0);

	/* key layers: only the active ones show; underlined when locked */
	layers = ui_key_layers();
	for (i = 0; i < 3; i++) {
		char s[2] = { letters[i], 0 };

		lv_label_set_text(st.layer[i], s);
		if (layers & layer_bits[i])
			lv_obj_remove_flag(st.layer[i], LV_OBJ_FLAG_HIDDEN);
		else
			lv_obj_add_flag(st.layer[i], LV_OBJ_FLAG_HIDDEN);
		lv_obj_set_style_text_decor(st.layer[i],
					    layers & UI_LAYERS_LOCKED ? LV_TEXT_DECOR_UNDERLINE : LV_TEXT_DECOR_NONE, 0);
	}
}

static void status_deleted(lv_event_t *e)
{
	/* only the current bar: ui_screen() deletes the old screen after building the new */
	if (lv_event_get_target(e) == st.bar)
		memset(&st, 0, sizeof(st));
}

static void status_timer(lv_timer_t *t)
{
	(void)t;
	status_refresh();
}

/* DECK's LED readout well: background fill, inverted bevel, accent bold digits */
static lv_obj_t *led_well(lv_obj_t *parent)
{
	const ui_palette_t *p = ui_palette();
	lv_obj_t *w = box(parent);

	lv_obj_set_size(w, LV_SIZE_CONTENT, 14);
	lv_obj_set_style_bg_color(w, p->bg, 0);
	lv_obj_set_style_bg_opa(w, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(w, p->dark, 0);
	lv_obj_set_style_border_side(w, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT, 0);
	lv_obj_set_style_border_width(w, 1, 0);
	lv_obj_set_style_pad_hor(w, 3, 0);
	lv_obj_set_flex_flow(w, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(w, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_column(w, 4, 0);
	return w;
}

static void status_bar(lv_obj_t *scr, const char *title)
{
	const ui_palette_t *p = ui_palette();
	wt_style_t style = ui_theme_style();
	lv_obj_t *bar, *left, *right, *sig, *batt, *l, *clock_parent;
	const char *slash = title ? strrchr(title, '/') : NULL;
	char parent[32] = "";
	int i;

	bar = box(scr);
	lv_obj_set_pos(bar, 0, WT_STATUS_Y);
	lv_obj_set_size(bar, WT_SCREEN_W, WT_STATUS_H);
	lv_obj_set_style_pad_hor(bar, WT_BAR_PAD_X, 0);
	lv_obj_add_event_cb(bar, status_deleted, LV_EVENT_DELETE, NULL);
	st.bar = bar;
	if (slash) {
		snprintf(parent, sizeof(parent), "%.*s", (int)(slash - title), title);
		title = slash + 1;
	}
	if (!title)
		title = "HOME";

	switch (style) {
	case WT_STYLE_AERO:	/* a 312 x 16 pill at 4, 2 */
		l = box(bar);
		lv_obj_add_flag(l, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
		lv_obj_set_pos(l, 4 - WT_BAR_PAD_X, 2);
		lv_obj_set_size(l, 312, 16);
		lv_obj_set_style_radius(l, 8, 0);
		lv_obj_set_style_bg_color(l, p->panel, 0);
		lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
		lv_obj_set_style_border_color(l, p->line, 0);
		lv_obj_set_style_border_width(l, 1, 0);
		break;
	case WT_STYLE_DECK:	/* surface fill, 2 px dark bottom edge */
		lv_obj_set_style_bg_color(bar, p->panel, 0);
		lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
		lv_obj_set_style_border_color(bar, p->dark, 0);
		lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
		lv_obj_set_style_border_width(bar, 2, 0);
		break;
	case WT_STYLE_NUKE:	/* 1 px line under the bar */
		lv_obj_set_style_border_color(bar, p->line, 0);
		lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
		lv_obj_set_style_border_width(bar, 1, 0);
		break;
	default:
		break;
	}

	/* left: the style's mark, breadcrumb parent, title */
	left = box(bar);
	lv_obj_set_size(left, LV_SIZE_CONTENT, LV_PCT(100));
	lv_obj_add_flag(left, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
	lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_column(left, WT_BAR_GAP, 0);
	switch (style) {
	case WT_STYLE_CYBER:
		rect(left, 0, 0, 4, 4, p->accent);
		break;
	case WT_STYLE_AERO:
		l = rect(left, 0, 0, 10, 10, p->accent);
		lv_obj_set_style_radius(l, LV_RADIUS_CIRCLE, 0);
		break;
	default:
		break;
	}
	if (parent[0]) {
		l = text(left, UI_ROLE_TITLE, p->dim, parent);
		if (style == WT_STYLE_NUKE)
			lv_obj_set_style_pad_bottom(l, 2, 0);
	}
	if (style == WT_STYLE_DECK) {		/* the title on a 16 px accent tape label */
		lv_obj_t *tape = box(left);

		lv_obj_set_size(tape, LV_SIZE_CONTENT, 16);
		lv_obj_set_style_bg_color(tape, p->accent, 0);
		lv_obj_set_style_bg_opa(tape, LV_OPA_COVER, 0);
		lv_obj_set_style_pad_hor(tape, 5, 0);
		lv_obj_add_flag(tape, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
		l = text(tape, UI_ROLE_TITLE, p->bg, title);
		lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, -1);	/* the role's padding centres the capitals; -1: 15 px label in a 16 px tape */
	} else {
		l = text(left, UI_ROLE_TITLE, p->text, title);
	}
	if (style == WT_STYLE_DECK) {
	} else if (style == WT_STYLE_NUKE) {	/* the current tab, underlined 2 px */
		lv_obj_set_style_border_color(l, p->text, 0);
		lv_obj_set_style_border_side(l, LV_BORDER_SIDE_BOTTOM, 0);
		lv_obj_set_style_border_width(l, 2, 0);
	}

	/* right: key layers, signal, battery, clock */
	right = box(bar);
	lv_obj_set_size(right, LV_SIZE_CONTENT, LV_PCT(100));
	lv_obj_align(right, LV_ALIGN_RIGHT_MID, 0, 0);
	lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
	/* START: with END the items sit left of a content-sized box and it measures too narrow */
	lv_obj_set_flex_align(right, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_column(right, WT_BAR_GAP, 0);
	st.layer[0] = text(right, UI_ROLE_DATA, p->dim, "G");
	st.layer[1] = text(right, UI_ROLE_DATA, lv_color_hex(WT_LAYER_O), "O");
	st.layer[2] = text(right, UI_ROLE_DATA, lv_color_hex(WT_LAYER_B), "B");

	/* signal: 4 bars 2 px wide, 3 + 3n px tall (AERO: four 4 px dots) */
	sig = box(right);
	if (style == WT_STYLE_AERO) {
		lv_obj_set_size(sig, SIGNAL_BARS * 6 - 2, 4);
		for (i = 0; i < SIGNAL_BARS; i++) {
			st.signal[i] = rect(sig, i * 6, 0, 4, 4, p->line);
			lv_obj_set_style_radius(st.signal[i], LV_RADIUS_CIRCLE, 0);
		}
	} else {
		lv_obj_set_size(sig, SIGNAL_BARS * 3 - 1, 3 + 3 * (SIGNAL_BARS - 1));
		for (i = 0; i < SIGNAL_BARS; i++)
			st.signal[i] = rect(sig, i * 3, 3 * (SIGNAL_BARS - 1 - i), 2, 3 + 3 * i, p->line);
	}

	/* battery: bolt (charging), 1 px box with 4 segments 3 x 6, percentage; DECK puts
	 * the percentage and the clock in LED wells */
	st.bolt = text(right, UI_ROLE_DATA, p->accent, LV_SYMBOL_CHARGE);
	batt = box(right);
	lv_obj_set_size(batt, BATTERY_SEGS * 4 + 3, 10);
	lv_obj_set_style_border_color(batt, p->text_bright, 0);
	lv_obj_set_style_border_width(batt, 1, 0);
	for (i = 0; i < BATTERY_SEGS; i++)	/* positions count from inside the 1 px border: a 1 px gap all round */
		st.battery_seg[i] = rect(batt, 1 + i * 4, 1, 3, 6, p->text);
	clock_parent = right;
	if (style == WT_STYLE_DECK) {
		lv_obj_t *w = led_well(right);

		st.battery_pct = text(w, UI_ROLE_KEY_SMALL, p->accent, "");
		clock_parent = led_well(right);
		st.clock = text(clock_parent, UI_ROLE_KEY_SMALL, p->accent, "--:--");
	} else {
		st.battery_pct = text(right, UI_ROLE_DATA, p->text, "");
		if (style == WT_STYLE_AERO) {	/* the clock in a background pill */
			clock_parent = box(right);
			lv_obj_set_size(clock_parent, LV_SIZE_CONTENT, 12);
			lv_obj_set_style_radius(clock_parent, 6, 0);
			lv_obj_set_style_bg_color(clock_parent, p->bg, 0);
			lv_obj_set_style_bg_opa(clock_parent, LV_OPA_COVER, 0);
			lv_obj_set_style_pad_hor(clock_parent, 5, 0);
		}
		st.clock = text(clock_parent, UI_ROLE_DATA, p->text, "--:--");
		if (clock_parent != right)	/* centred in the pill lands 1 px below the percentage */
			lv_obj_align(st.clock, LV_ALIGN_CENTER, 0, -1);
	}
	fixed_width(st.battery_pct, "100%", LV_TEXT_ALIGN_RIGHT);
	fixed_width(st.clock, "88:88", LV_TEXT_ALIGN_CENTER);
}

/* ---- hint bar ----------------------------------------------------------------- */

static lv_obj_t *hint_bar(lv_obj_t *scr)
{
	const ui_palette_t *p = ui_palette();
	lv_obj_t *bar = box(scr);

	lv_obj_set_pos(bar, 0, WT_HINT_Y);
	lv_obj_set_size(bar, WT_SCREEN_W, WT_HINT_H);
	if (ui_theme_style() == WT_STYLE_DECK) {	/* surface, 1 px highlight top */
		lv_obj_set_style_bg_color(bar, p->panel, 0);
		lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
		lv_obj_set_style_border_color(bar, p->highlight, 0);
		lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
		lv_obj_set_style_border_width(bar, 1, 0);
	}
	lv_obj_set_style_pad_hor(bar, WT_BAR_PAD_X, 0);
	lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_column(bar, WT_HINT_GAP, 0);
	return bar;
}

/* "KEY LABEL|KEY LABEL|..." ("~" for a space inside a key) plus a right-aligned marker */
void ui_hint_keys(const char *pairs, const char *marker)
{
	const ui_palette_t *p = ui_palette();
	char buf[160], *pair, *save = NULL;
	lv_obj_t *m;

	if (!st.hint)
		return;
	if (pairs != st.pairs)
		snprintf(st.pairs, sizeof(st.pairs), "%s", pairs ? pairs : "");
	if (marker != st.marker_text)
		snprintf(st.marker_text, sizeof(st.marker_text), "%s", marker ? marker : "");
	lv_obj_clean(st.hint);
	snprintf(buf, sizeof(buf), "%s", pairs ? pairs : "");
	for (pair = strtok_r(buf, "|", &save); pair; pair = strtok_r(NULL, "|", &save)) {
		char *label = strchr(pair, ' ');
		lv_obj_t *g = box(st.hint);

		lv_obj_set_size(g, LV_SIZE_CONTENT, LV_PCT(100));
		lv_obj_set_flex_flow(g, LV_FLEX_FLOW_ROW);
		lv_obj_set_flex_align(g, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
		lv_obj_set_style_pad_column(g, 5, 0);
		if (label)
			*label++ = 0;
		for (char *c = pair; *c; c++)	/* "~" is a space inside a key ("4~6") */
			if (*c == '~')
				*c = ' ';
		ui_hint_key(g, pair, !strcmp(pair, ARROWS_UTF8));
		if (label) {
			if (ui_theme_style() == WT_STYLE_CYBER)	/* 1 x 10 divider */
				rect(g, 0, 0, 1, 10, p->line);
			text(g, UI_ROLE_HINT, p->dim, label);
		}
	}
	m = text(st.hint, UI_ROLE_HINT, p->dim, marker ? marker : "");
	lv_obj_add_flag(m, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_align(m, LV_ALIGN_RIGHT_MID, 0, 0);
	st.marker = m;
}

void ui_hint_marker(const char *marker)
{
	if (marker != st.marker_text)
		snprintf(st.marker_text, sizeof(st.marker_text), "%s", marker ? marker : "");
	if (st.marker)
		lv_label_set_text(st.marker, st.marker_text);
}

/* A temporary hint (the keyboard's), then back to the screen's */
void ui_hint_save(char *pairs, size_t plen, char *marker, size_t mlen)
{
	snprintf(pairs, plen, "%s", st.pairs);
	snprintf(marker, mlen, "%s", st.marker_text);
}

/* Plain hint text (screens not yet redone) */
void ui_hint(const char *s)
{
	if (!st.hint)
		return;
	lv_obj_clean(st.hint);
	st.marker = NULL;
	text(st.hint, UI_ROLE_HINT, ui_palette()->dim, s);
}

/* ---- CYBER decor band ---------------------------------------------------------- */

static void cyber_decor(lv_obj_t *scr)
{
	const ui_palette_t *p = ui_palette();
	const int fx = 1, fy = 22, fw = 318, fh = 200, c = 12, t = 2;
	lv_obj_t *frame = box(scr);
	int x;

	/* 1 px frame */
	lv_obj_set_pos(frame, fx, fy);
	lv_obj_set_size(frame, fw, fh);
	lv_obj_set_style_border_color(frame, p->line, 0);
	lv_obj_set_style_border_width(frame, 1, 0);

	/* 12 x 12 corner pieces, 2 px, in text bright */
	rect(scr, fx, fy, c, t, p->text_bright);
	rect(scr, fx, fy, t, c, p->text_bright);
	rect(scr, fx + fw - c, fy, c, t, p->text_bright);
	rect(scr, fx + fw - t, fy, t, c, p->text_bright);
	rect(scr, fx, fy + fh - t, c, t, p->text_bright);
	rect(scr, fx, fy + fh - c, t, c, p->text_bright);
	rect(scr, fx + fw - c, fy + fh - t, c, t, p->text_bright);
	rect(scr, fx + fw - t, fy + fh - c, t, c, p->text_bright);

	/* accent segment under the title, tick marks at the top right */
	rect(scr, 18, 21, 40, 2, p->accent);
	for (x = 0; x < 73; x += 4)
		rect(scr, 228 + x, 23, 1, 2, p->text_tick);
}

/* DECK: four 5 px screws in the corners of the band */
static void deck_decor(lv_obj_t *scr)
{
	const ui_palette_t *p = ui_palette();
	static const lv_point_t at[] = { { 2, 22 }, { 313, 22 }, { 2, 217 }, { 313, 217 } };
	int i;

	for (i = 0; i < 4; i++) {
		lv_obj_t *s = rect(scr, at[i].x, at[i].y, 5, 5, p->dark);

		lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, 0);
		lv_obj_set_style_border_color(s, p->line, 0);
		lv_obj_set_style_border_width(s, 1, 0);
	}
}

/* NUKE: a rounded bezel and a tick scale down the left */
static void nuke_decor(lv_obj_t *scr)
{
	const ui_palette_t *p = ui_palette();
	lv_obj_t *bezel = box(scr);
	int y;

	lv_obj_set_pos(bezel, 2, 23);
	lv_obj_set_size(bezel, 316, 198);
	lv_obj_set_style_radius(bezel, 6, 0);
	lv_obj_set_style_border_color(bezel, p->line, 0);
	lv_obj_set_style_border_width(bezel, 1, 0);
	for (y = 32; y <= 212; y += 6)
		rect(scr, 4, y, 2, 1, p->line);
}

static void style_decor(lv_obj_t *scr)
{
	switch (ui_theme_style()) {
	case WT_STYLE_CYBER: cyber_decor(scr); break;
	case WT_STYLE_DECK: deck_decor(scr); break;
	case WT_STYLE_NUKE: nuke_decor(scr); break;
	default: break;		/* AERO: none */
	}
}

/* ---- screens -------------------------------------------------------------------- */

lv_obj_t *ui_screen(const char *title)
{
	const ui_palette_t *p = ui_palette();
	static lv_timer_t *timer;
	lv_obj_t *scr, *old, *content;

	scr = lv_obj_create(NULL);
	lv_obj_remove_style_all(scr);
	lv_obj_set_style_bg_color(scr, p->bg, 0);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
	lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

	style_decor(scr);
	status_bar(scr, title);

	/* content area: 320 x 204 at y 20; the content proper is WT_INNER_* (absolute) */
	content = box(scr);
	lv_obj_set_pos(content, 0, WT_CONTENT_Y);
	lv_obj_set_size(content, WT_SCREEN_W, WT_CONTENT_H);

	st.hint = hint_bar(scr);
	focus_ring_create(scr);		/* last: above the content */

	lv_group_remove_all_objs(ui_group());
	lv_group_set_wrap(ui_group(), true);
	lv_group_set_edge_cb(ui_group(), NULL);
	keys_set_back(NULL);
	old = lv_screen_active();
	{
		lv_screen_load_anim_t anim = focus_take_transition();

		/* the first screen of a program appears at once */
		if (!old || lv_obj_get_child_count(old) == 0)
			anim = LV_SCR_LOAD_ANIM_NONE;
		if (anim == LV_SCR_LOAD_ANIM_NONE) {
			lv_screen_load(scr);
			if (old && old != scr)
				lv_obj_delete(old);
		} else {
			lv_screen_load_anim(scr, anim, WT_ANIM_MS, 0, true);
		}
	}
	if (!timer)
		timer = lv_timer_create(status_timer, 1000, NULL);
	status_refresh();
	return content;
}
