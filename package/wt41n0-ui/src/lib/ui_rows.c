/*
 * Lists (SPEC.md, Components): a scrolling column of rows in the content proper with the
 * scroll indicator in its own column.
 *   row          296 x 36, gap 2, from y 26 (five visible); padding 0 10 0 6: number
 *                (14 wide), 18 px icon, name, flexible gap, value, 10 px chevron
 *   compact row  296 x 34 under a 40 px card, from y 70; padding 0 10 0 8
 *   indicator    5 x 192 at x 308: one tick per item, gap 2 (1 past 20 items); the
 *                focused one 3 px wide in accent
 * Focusing a row updates the indicator and the hint bar's "01/07" marker. Digit keys
 * move the focus to the numbered row.
 */
#include <stdio.h>
#include "ui_internal.h"

#define CY(y) ((y) - WT_CONTENT_Y)
#define MAX_ROWS 40

struct list {
	bool done;		/* ui_rows_done() ran: rows fit themselves as they change */
	lv_obj_t *col;			/* the scrolling column */
	lv_obj_t *ind;			/* the indicator column */
	lv_obj_t *ticks[MAX_ROWS];
	lv_obj_t *rows[MAX_ROWS];
	int n;
	bool compact;
	const char *marker;	/* fixed hint marker, or NULL: "01/07" */
};

/* a row's parts, kept as the panel's data */
struct row {
	lv_obj_t *check, *number, *icon, *name, *leader, *value, *chevron;
	char text[40];
};

static void list_free(lv_event_t *e)
{
	lv_free(lv_event_get_user_data(e));
}

lv_obj_t *ui_rows_create(lv_obj_t *content, int y, int h, bool compact)
{
	struct list *l = lv_malloc_zeroed(sizeof(*l));
	lv_obj_t *col = lv_obj_create(content);

	lv_obj_remove_style_all(col);
	lv_obj_set_pos(col, WT_INNER_X, CY(y));
	lv_obj_set_size(col, WT_ROW_W, h);
	lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_row(col, WT_ROW_GAP, 0);
	lv_obj_set_scrollbar_mode(col, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_scroll_dir(col, LV_DIR_VER);

	l->col = col;
	l->compact = compact;
	l->ind = lv_obj_create(content);
	lv_obj_remove_style_all(l->ind);
	lv_obj_set_pos(l->ind, WT_SCROLL_X, CY(y));
	lv_obj_set_size(l->ind, WT_SCROLL_W, h);
	lv_obj_remove_flag(l->ind, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_user_data(col, l);
	lv_obj_add_event_cb(col, list_free, LV_EVENT_DELETE, l);
	return col;
}

static void fit_name(lv_obj_t *row);

static void colors(lv_obj_t *row, bool focused)
{
	const ui_palette_t *p = ui_palette();
	struct row *r = ui_panel_data(row);

	if (r->number)
		ui_number_focus(r->number, focused);
	if (r->icon)
		ui_icon_color(r->icon, focused ? p->accent : p->dim);
	ui_name_focus(r->name, r->text, focused);
	if (r->leader)
		lv_obj_set_style_line_color(r->leader, focused ? p->text : p->line, 0);
	lv_obj_set_style_text_color(r->value, focused ? p->text : p->dim, 0);
	if (r->chevron)
		lv_obj_set_style_text_color(r->chevron, focused ? p->accent : p->dim, 0);
	if (ui_theme_style() == WT_STYLE_NUKE && r->value)	/* "[ NAME ]" is wider: fit again */
		fit_name(row);
}

static void indicator(struct list *l, int focused)
{
	const ui_palette_t *p = ui_palette();
	int i;

	for (i = 0; i < l->n; i++) {
		bool f = i == focused;

		lv_obj_set_style_bg_color(l->ticks[i], f ? p->accent : p->line, 0);
		lv_obj_set_width(l->ticks[i], f ? 3 : 1);
		lv_obj_set_x(l->ticks[i], f ? 1 : 2);
	}
}

static void row_event(lv_event_t *e)
{
	lv_obj_t *row = lv_event_get_current_target(e);
	struct list *l = lv_obj_get_user_data(lv_obj_get_parent(row));
	int index = 0;
	char marker[24];
	uint32_t key;

	while (index < l->n && l->rows[index] != row)
		index++;
	switch (lv_event_get_code(e)) {
	case LV_EVENT_FOCUSED:
		colors(row, true);
		indicator(l, index);
		snprintf(marker, sizeof(marker), "%02d/%02d", index + 1, l->n);
		ui_hint_marker(l->marker ? l->marker : marker);
		break;
	case LV_EVENT_DEFOCUSED:
		colors(row, false);
		break;
	case LV_EVENT_KEY:
		key = lv_event_get_key(e);
		if (key >= '1' && key <= '9' && (int)(key - '1') < l->n && !l->marker)
			lv_group_focus_obj(l->rows[key - '1']);
		break;
	default:
		break;
	}
}

/* shorten the name by what the row overflows; the layout must be up to date */
static void fit_measure(lv_obj_t *row)
{
	struct row *r = ui_panel_data(row);
	lv_obj_t *last = r->chevron ? r->chevron : r->value;
	lv_area_t rc, lc, nc;
	int right, over;

	lv_obj_get_coords(row, &rc);
	lv_obj_get_coords(last, &lc);
	lv_obj_get_coords(r->name, &nc);
	right = rc.x2 - lv_obj_get_style_pad_right(row, 0) - lv_obj_get_style_border_width(row, 0);
	over = lc.x2 - right + 8;
	if (over > 8) {
		lv_label_set_long_mode(r->name, LV_LABEL_LONG_DOT);
		lv_obj_set_width(r->name, lv_area_get_width(&nc) - over);
	}
}

/* a name too long for the row ends in "..." so the value and the chevron stay in */
static void fit_name(lv_obj_t *row)
{
	struct row *r = ui_panel_data(row);
	struct list *l = lv_obj_get_user_data(lv_obj_get_parent(row));

	if (!l->done)
		return;			/* ui_rows_done() fits them all in one layout pass */
	lv_label_set_long_mode(r->name, LV_LABEL_LONG_CLIP);
	lv_obj_set_width(r->name, LV_SIZE_CONTENT);
	lv_obj_update_layout(row);
	fit_measure(row);
}

static void row_free(lv_event_t *e)
{
	lv_free(lv_event_get_user_data(e));
}

/* NUKE: a dotted leader (1 px every 4 px) between name and value */
static lv_obj_t *leader(lv_obj_t *row)
{
	static lv_point_precise_t pts[2] = { { 0, 0 }, { WT_ROW_W, 0 } };
	lv_obj_t *o = lv_line_create(row);

	lv_line_set_points(o, pts, 2);
	lv_obj_set_height(o, 1);
	lv_obj_set_flex_grow(o, 1);
	lv_obj_set_style_line_width(o, 1, 0);
	lv_obj_set_style_line_dash_width(o, 1, 0);
	lv_obj_set_style_line_dash_gap(o, 3, 0);
	lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
	return o;
}

/* flags (UI_ROW_*): no number (and no digit jump), no chevron, a check column first */
lv_obj_t *ui_row_ex(lv_obj_t *list, const char *icon, const char *name, const char *value, int flags)
{
	const ui_palette_t *p = ui_palette();
	struct list *l = lv_obj_get_user_data(list);
	struct row *r;
	lv_obj_t *row, *t;
	char num[12];

	if (l->n == MAX_ROWS)
		return NULL;
	r = lv_malloc_zeroed(sizeof(*r));
	snprintf(r->text, sizeof(r->text), "%s", name);
	row = ui_panel_kind(list, UI_PANEL_ROW, 0, 0, WT_ROW_W, l->compact ? WT_ROW_COMPACT_H : WT_ROW_H, true);
	ui_panel_set_data(row, r);
	lv_obj_add_event_cb(row, row_free, LV_EVENT_DELETE, r);
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_left(row, l->compact ? 8 - 1 : 6 - 1, 0);
	lv_obj_set_style_pad_right(row, 10 - 1, 0);
	lv_obj_set_style_pad_column(row, 8, 0);

	if (flags & UI_ROW_CHECK) {
		r->check = lv_obj_create(row);		/* the check column */
		lv_obj_remove_style_all(r->check);
		lv_obj_set_size(r->check, 18, 18);
		lv_obj_remove_flag(r->check, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_style_margin_right(r->check, 8, 0);
	}
	if (!(flags & UI_ROW_NO_NUMBER)) {
		snprintf(num, sizeof(num), "%02d", l->n + 1);
		r->number = ui_number(row, num);
		if (ui_theme_style() == WT_STYLE_CYBER)
			lv_obj_set_width(r->number, 14);
	}
	if (icon && *icon)
		r->icon = ui_icon_sized(row, icon, 18);
	r->name = lv_label_create(row);
	lv_label_set_text(r->name, name);
	ui_text_role(r->name, UI_ROLE_NAME);
	lv_obj_set_style_text_color(r->name, p->text, 0);

	if (ui_theme_style() == WT_STYLE_NUKE) {
		r->leader = leader(row);
	} else {
		t = lv_obj_create(row);			/* the flexible gap */
		lv_obj_remove_style_all(t);
		lv_obj_set_flex_grow(t, 1);
		lv_obj_remove_flag(t, LV_OBJ_FLAG_CLICKABLE);
	}
	r->value = lv_label_create(row);
	lv_label_set_text(r->value, value ? value : "");
	ui_text_role(r->value, UI_ROLE_DATA);
	if (!(flags & UI_ROW_NO_CHEVRON)) {
		r->chevron = lv_label_create(row);
		lv_label_set_text(r->chevron, LV_SYMBOL_RIGHT);
		lv_obj_set_style_text_font(r->chevron, &lv_font_montserrat_12, 0);
		lv_obj_set_width(r->chevron, 10);
	}
	colors(row, false);
	if (l->done)
		fit_name(row);
	lv_obj_add_event_cb(row, row_event, LV_EVENT_ALL, NULL);
	l->rows[l->n++] = row;
	return row;
}

lv_obj_t *ui_row(lv_obj_t *list, const char *icon, const char *name, const char *value)
{
	return ui_row_ex(list, icon, name, value, 0);
}

void ui_row_set_value(lv_obj_t *row, const char *value)
{
	lv_label_set_text(((struct row *)ui_panel_data(row))->value, value);
	fit_name(row);
}

/* the row's value label, for tags and colours */
lv_obj_t *ui_row_value(lv_obj_t *row)
{
	return ((struct row *)ui_panel_data(row))->value;
}

/* the check mark in a UI_ROW_CHECK row */
void ui_row_set_check(lv_obj_t *row, bool on)
{
	struct row *r = ui_panel_data(row);

	if (!r->check)
		return;
	lv_obj_clean(r->check);
	if (on)
		ui_icon_color(ui_icon_sized(r->check, "check", 18), ui_palette()->accent);
}

/* a tag before the value (IN USE), in its own colour */
lv_obj_t *ui_row_tag(lv_obj_t *row, const char *text, lv_color_t color)
{
	struct row *r = ui_panel_data(row);
	lv_obj_t *t = lv_label_create(row);

	lv_label_set_text(t, text);
	ui_text_role(t, UI_ROLE_DATA);
	lv_obj_set_style_text_color(t, color, 0);
	lv_obj_move_to_index(t, (int32_t)lv_obj_get_index(r->value));
	fit_name(row);
	return t;
}

/* a fixed hint marker for the list ("4 NETS") instead of the position; no digit jumps */
void ui_rows_marker(lv_obj_t *list, const char *marker)
{
	((struct list *)lv_obj_get_user_data(list))->marker = marker;
}

/* After the rows: the indicator ticks, and the focus on row "focus" */
void ui_rows_done(lv_obj_t *list, int focus)
{
	struct list *l = lv_obj_get_user_data(list);
	int h = lv_obj_get_height(l->ind), gap = l->n > 20 ? 1 : 2, i, y = 0;

	for (i = 0; i < l->n; i++) {
		int th = (h - (l->n - 1) * gap) * (i + 1) / l->n - (h - (l->n - 1) * gap) * i / l->n;

		l->ticks[i] = lv_obj_create(l->ind);
		lv_obj_remove_style_all(l->ticks[i]);
		lv_obj_set_style_bg_opa(l->ticks[i], LV_OPA_COVER, 0);
		lv_obj_set_pos(l->ticks[i], 2, y);
		lv_obj_set_size(l->ticks[i], 1, th);
		y += th + gap;
	}
	indicator(l, -1);
	l->done = true;
	lv_obj_update_layout(list);		/* one pass for every row */
	for (i = 0; i < l->n; i++)
		fit_measure(l->rows[i]);
	if (l->n)
		lv_group_focus_obj(l->rows[focus >= 0 && focus < l->n ? focus : 0]);
}
