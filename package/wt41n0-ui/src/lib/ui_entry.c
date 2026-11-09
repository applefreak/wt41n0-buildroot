/*
 * Text entry: a themed one-line field, typed into through the keypad layers (characters
 * go to the focused field; Up/Down leave it, Enter submits), plus an on-screen keyboard
 * that can be shown for it (tapped on touch units, or moved with Up/Down and 4/6 on the
 * keypad, Esc closes it; see ui_keys.c).
 */
#include <string.h>
#include "ui_internal.h"

static lv_obj_t *kb;		/* the keyboard of the current screen */
static lv_obj_t *kb_field;
static void (*kb_ready)(lv_obj_t *field);
static char saved_pairs[160], saved_marker[24];

/* ---- text field (SPEC.md: 304 x 32, padding-left 10, 16 px text, 2 px tracking,
 * 2 px caret, count right; WRONG PASSWORD turns the border warning) ---------------- */

static void count_update(lv_obj_t *ta)
{
	lv_obj_t *count = lv_obj_get_child(ta, -1);

	if (count && lv_obj_check_type(count, &lv_label_class))
		lv_label_set_text_fmt(count, "%d", (int)strlen(lv_textarea_get_text(ta)));
}

static void field_event(lv_event_t *e)
{
	lv_obj_t *ta = lv_event_get_target(e);

	switch (lv_event_get_code(e)) {
	case LV_EVENT_VALUE_CHANGED:
		count_update(ta);
		ui_text_field_set_error(ta, false);
		break;
	case LV_EVENT_CLICKED:
		/* a tap on a touch unit brings up the keyboard */
		if (lv_indev_get_type(lv_indev_active()) == LV_INDEV_TYPE_POINTER)
			ui_keyboard_show(ta, true);
		break;
	case LV_EVENT_READY:
		if (kb_ready)
			kb_ready(ta);
		break;
	default:
		break;
	}
}

void ui_text_field_set_error(lv_obj_t *ta, bool error)
{
	const ui_palette_t *p = ui_palette();

	wt_style_t s = ui_theme_style();
	lv_color_t idle = s == WT_STYLE_CYBER ? p->text_edge : s == WT_STYLE_DECK ? p->dark : p->line;

	/* focused: accent (CYBER: the ring does it) */
	lv_obj_set_style_border_color(ta, error ? p->warn : idle, 0);
	lv_obj_set_style_border_width(ta, error ? 2 : 1, 0);
	if (s == WT_STYLE_DECK && !error)	/* inset well: dark top and left */
		lv_obj_set_style_border_side(ta, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT, 0);
	else
		lv_obj_set_style_border_side(ta, LV_BORDER_SIDE_FULL, 0);
}

lv_obj_t *ui_text_field(lv_obj_t *parent, const char *placeholder, bool password,
			void (*ready)(lv_obj_t *field))
{
	const ui_palette_t *p = ui_palette();
	lv_obj_t *ta = lv_textarea_create(parent), *count;

	lv_obj_remove_style_all(ta);
	lv_textarea_set_one_line(ta, true);
	lv_textarea_set_password_mode(ta, password);
	lv_textarea_set_password_show_time(ta, 800);
	lv_textarea_set_placeholder_text(ta, placeholder);
	lv_obj_set_size(ta, LV_PCT(100), WT_FIELD_H);
	lv_obj_set_style_text_font(ta, ui_font_role(UI_ROLE_KEY), 0);
	lv_obj_set_style_text_letter_space(ta, 2, 0);
	lv_obj_set_style_radius(ta, ui_theme_style() == WT_STYLE_AERO ? 8 : ui_theme_style() == WT_STYLE_DECK ? 2 : 0, 0);
	lv_obj_set_style_bg_color(ta, p->bg, 0);
	lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
	lv_obj_set_style_text_color(ta, p->text, 0);
	lv_obj_set_style_pad_left(ta, 10, 0);
	lv_obj_set_style_pad_right(ta, 30, 0);
	lv_obj_set_style_pad_ver(ta, 7, 0);
	ui_text_field_set_error(ta, false);
	lv_obj_set_style_text_color(ta, p->dim, LV_PART_TEXTAREA_PLACEHOLDER);
	/* the caret: a 2 px accent bar */
	lv_obj_set_style_bg_opa(ta, LV_OPA_TRANSP, LV_PART_CURSOR);
	lv_obj_set_style_border_color(ta, p->accent, LV_PART_CURSOR | LV_STATE_FOCUSED);
	lv_obj_set_style_border_width(ta, 2, LV_PART_CURSOR | LV_STATE_FOCUSED);
	lv_obj_set_style_border_side(ta, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR | LV_STATE_FOCUSED);
	lv_obj_set_scrollbar_mode(ta, LV_SCROLLBAR_MODE_OFF);
	count = lv_label_create(ta);			/* last child: the count */
	ui_text_role(count, UI_ROLE_DATA);
	lv_obj_set_style_text_color(count, p->dim, 0);
	lv_obj_add_flag(count, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
	lv_obj_align(count, LV_ALIGN_RIGHT_MID, 22, 0);
	count_update(ta);
	lv_obj_add_event_cb(ta, field_event, LV_EVENT_ALL, NULL);
	lv_group_add_obj(ui_group(), ta);
	kb_ready = ready;
	return ta;
}

/* ---- keyboard (SPEC.md: 320 x 120 at y 104; keys 30 x 26, gap 2, rows at 108 + 28n;
 * SHIFT / DELETE / ENTER / 123 46 wide, SPACE 126, row 2 inset 16) ---------------- */

#define K(w) (LV_BUTTONMATRIX_CTRL_NO_REPEAT * 0 | (w))	/* width units: 1 = 15 px */
#define HID (LV_BUTTONMATRIX_CTRL_HIDDEN | 1)
#define CTL(w) (LV_BUTTONMATRIX_CTRL_NO_REPEAT | LV_BUTTONMATRIX_CTRL_CHECKED | (w))

static const char *const map_lower[] = {
	"q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
	" ", "a", "s", "d", "f", "g", "h", "j", "k", "l", " ", "\n",
	LV_SYMBOL_UP, "z", "x", "c", "v", "b", "n", "m", LV_SYMBOL_BACKSPACE, "\n",
	"123", ",", "SPACE", ".", LV_SYMBOL_NEW_LINE, LV_SYMBOL_CLOSE, "",
};
static const char *const map_upper[] = {
	"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
	" ", "A", "S", "D", "F", "G", "H", "J", "K", "L", " ", "\n",
	LV_SYMBOL_UP, "Z", "X", "C", "V", "B", "N", "M", LV_SYMBOL_BACKSPACE, "\n",
	"123", ",", "SPACE", ".", LV_SYMBOL_NEW_LINE, LV_SYMBOL_CLOSE, "",
};
static const char *const map_digits[] = {
	"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
	" ", "-", "/", ":", ";", "(", ")", "$", "&", "@", " ", "\n",
	"=", "_", "+", "!", "?", "#", "%", "*", LV_SYMBOL_BACKSPACE, "\n",
	"abc", "'", "SPACE", "\"", LV_SYMBOL_NEW_LINE, LV_SYMBOL_CLOSE, "",
};
static const lv_buttonmatrix_ctrl_t ctrl_letters[] = {
	K(2), K(2), K(2), K(2), K(2), K(2), K(2), K(2), K(2), K(2),
	HID, K(2), K(2), K(2), K(2), K(2), K(2), K(2), K(2), K(2), HID,
	CTL(3), K(2), K(2), K(2), K(2), K(2), K(2), K(2), CTL(3),
	CTL(3), K(2), K(8), K(2), CTL(3), CTL(2),
};
static const lv_buttonmatrix_ctrl_t ctrl_digits[] = {
	K(2), K(2), K(2), K(2), K(2), K(2), K(2), K(2), K(2), K(2),
	HID, K(2), K(2), K(2), K(2), K(2), K(2), K(2), K(2), K(2), HID,
	K(3), K(2), K(2), K(2), K(2), K(2), K(2), K(2), CTL(3),
	CTL(3), K(2), K(8), K(2), CTL(3), CTL(2),
};

static void kb_value(lv_event_t *e)
{
	lv_obj_t *k = lv_event_get_target(e);
	uint32_t id = lv_buttonmatrix_get_selected_button(k);
	const char *t = id == LV_BUTTONMATRIX_BUTTON_NONE ? NULL : lv_buttonmatrix_get_button_text(k, id);
	lv_obj_t *ta = lv_keyboard_get_textarea(k);

	if (!t || !ta)
		return;
	if (!strcmp(t, LV_SYMBOL_UP))
		lv_keyboard_set_mode(k, lv_keyboard_get_mode(k) == LV_KEYBOARD_MODE_TEXT_UPPER ?
				     LV_KEYBOARD_MODE_TEXT_LOWER : LV_KEYBOARD_MODE_TEXT_UPPER);
	else if (!strcmp(t, "123"))
		lv_keyboard_set_mode(k, LV_KEYBOARD_MODE_NUMBER);
	else if (!strcmp(t, "abc"))
		lv_keyboard_set_mode(k, LV_KEYBOARD_MODE_TEXT_LOWER);
	else if (!strcmp(t, LV_SYMBOL_BACKSPACE))
		lv_textarea_delete_char(ta);
	else if (!strcmp(t, LV_SYMBOL_NEW_LINE))
		lv_obj_send_event(k, LV_EVENT_READY, NULL);
	else if (!strcmp(t, LV_SYMBOL_CLOSE))
		lv_obj_send_event(k, LV_EVENT_CANCEL, NULL);
	else if (!strcmp(t, "SPACE"))
		lv_textarea_add_char(ta, ' ');
	else
		lv_textarea_add_text(ta, t);
}

static void kb_event(lv_event_t *e)
{
	lv_event_code_t code = lv_event_get_code(e);

	if (code == LV_EVENT_READY && kb_field) {
		lv_obj_t *field = kb_field;

		ui_keyboard_show(field, false);
		if (kb_ready)
			kb_ready(field);
	} else if (code == LV_EVENT_CANCEL && kb_field) {
		ui_keyboard_show(kb_field, false);
	} else if (code == LV_EVENT_DELETE) {
		kb = kb_field = NULL;
	}
}

static void kb_style(lv_obj_t *k)
{
	const ui_palette_t *p = ui_palette();

	lv_obj_remove_style_all(k);
	wt_style_t s = ui_theme_style();

	lv_obj_set_style_bg_color(k, s == WT_STYLE_DECK ? p->panel : p->bg, 0);
	lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(k, s == WT_STYLE_DECK ? p->highlight : p->line, 0);
	lv_obj_set_style_border_side(k, LV_BORDER_SIDE_TOP, 0);
	lv_obj_set_style_border_width(k, 1, 0);
	lv_obj_set_style_pad_top(k, WT_KEY_ROW_Y0 - WT_KBD_Y, 0);
	lv_obj_set_style_pad_bottom(k, 2, 0);
	lv_obj_set_style_pad_hor(k, 1, 0);
	lv_obj_set_style_pad_row(k, WT_KEY_ROW_STEP - WT_KEY_H, 0);
	lv_obj_set_style_pad_column(k, WT_KEY_GAP, 0);
	/* keys (SPEC.md, Style tables > Keyboard):
	 * CYBER on background, 1 px text-edge border; focused solid accent, dark text
	 * AERO  pills on background; focused accent with a 2 px text-white ring
	 * DECK  bevelled keys on line (dark bottom edge); focused accent, 1 px outline 1 px out
	 * NUKE  on surface, 1 px line border; focused solid text, dark glyph */
	lv_obj_set_style_bg_opa(k, LV_OPA_COVER, LV_PART_ITEMS);
	lv_obj_set_style_border_width(k, 1, LV_PART_ITEMS);
	lv_obj_set_style_text_color(k, p->text, LV_PART_ITEMS);
	lv_obj_set_style_text_font(k, ui_font_role(UI_ROLE_KEY), LV_PART_ITEMS);
	lv_obj_set_style_text_font(k, ui_font_role(UI_ROLE_KEY_SMALL), LV_PART_ITEMS | LV_STATE_CHECKED);
	lv_obj_set_style_text_color(k, p->dim, LV_PART_ITEMS | LV_STATE_CHECKED);
	lv_obj_set_style_bg_color(k, p->accent, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
	lv_obj_set_style_text_color(k, p->bg, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
	lv_obj_set_style_border_color(k, p->accent, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
	lv_obj_set_style_bg_color(k, p->accent_pressed, LV_PART_ITEMS | LV_STATE_PRESSED);
	switch (s) {
	case WT_STYLE_AERO:
		lv_obj_set_style_bg_color(k, p->bg, LV_PART_ITEMS);
		lv_obj_set_style_border_color(k, p->line, LV_PART_ITEMS);
		lv_obj_set_style_radius(k, WT_KEY_H / 2, LV_PART_ITEMS);
		lv_obj_set_style_bg_color(k, p->panel, LV_PART_ITEMS | LV_STATE_CHECKED);
		lv_obj_set_style_border_color(k, p->text_white, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
		lv_obj_set_style_border_width(k, 2, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
		break;
	case WT_STYLE_DECK:
		lv_obj_set_style_bg_color(k, p->line, LV_PART_ITEMS);
		lv_obj_set_style_border_color(k, p->dark, LV_PART_ITEMS);
		lv_obj_set_style_border_side(k, LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_RIGHT, LV_PART_ITEMS);
		lv_obj_set_style_border_width(k, 2, LV_PART_ITEMS);
		lv_obj_set_style_radius(k, 3, LV_PART_ITEMS);
		lv_obj_set_style_bg_color(k, p->highlight, LV_PART_ITEMS | LV_STATE_CHECKED);
		lv_obj_set_style_text_color(k, p->text, LV_PART_ITEMS | LV_STATE_CHECKED);
		lv_obj_set_style_outline_color(k, p->accent, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
		lv_obj_set_style_outline_width(k, 1, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
		lv_obj_set_style_outline_pad(k, 1, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
		lv_obj_set_style_border_color(k, p->dark, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
		break;
	case WT_STYLE_NUKE:
		lv_obj_set_style_bg_color(k, p->panel, LV_PART_ITEMS);
		lv_obj_set_style_border_color(k, p->line, LV_PART_ITEMS);
		lv_obj_set_style_radius(k, 0, LV_PART_ITEMS);
		lv_obj_set_style_bg_color(k, p->bg, LV_PART_ITEMS | LV_STATE_CHECKED);
		lv_obj_set_style_bg_color(k, p->text, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
		lv_obj_set_style_border_color(k, p->text, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
		break;
	default:
		lv_obj_set_style_bg_color(k, p->bg, LV_PART_ITEMS);
		lv_obj_set_style_border_color(k, p->text_edge, LV_PART_ITEMS);
		lv_obj_set_style_radius(k, 0, LV_PART_ITEMS);
		lv_obj_set_style_bg_color(k, p->panel, LV_PART_ITEMS | LV_STATE_CHECKED);
		break;
	}
}

void ui_keyboard_show(lv_obj_t *field, bool show)
{
	if (!show) {
		if (kb && !lv_obj_has_flag(kb, LV_OBJ_FLAG_HIDDEN)) {
			lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
			ui_hint_keys(saved_pairs, saved_marker);
		}
		if (field)
			lv_group_focus_obj(field);
		return;
	}
	if (!kb) {
		kb = lv_keyboard_create(lv_screen_active());
		lv_obj_remove_event_cb(kb, lv_keyboard_def_event_cb);
		kb_style(kb);
		lv_obj_set_size(kb, WT_SCREEN_W, WT_KBD_H);
		lv_obj_set_pos(kb, 0, WT_KBD_Y);
		lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER, map_lower, ctrl_letters);
		lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER, map_upper, ctrl_letters);
		lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_NUMBER, map_digits, ctrl_digits);
		lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
		lv_obj_add_event_cb(kb, kb_value, LV_EVENT_VALUE_CHANGED, NULL);
		lv_obj_add_event_cb(kb, kb_event, LV_EVENT_ALL, NULL);
		lv_group_add_obj(ui_group(), kb);
	}
	if (lv_obj_has_flag(kb, LV_OBJ_FLAG_HIDDEN) || !kb_field)
		ui_hint_save(saved_pairs, sizeof(saved_pairs), saved_marker, sizeof(saved_marker));
	kb_field = field;
	lv_keyboard_set_textarea(kb, field);
	lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);
	lv_obj_move_foreground(kb);
	lv_group_focus_obj(kb);
	ui_hint_keys("\xE2\x86\x91\xE2\x86\x93 ROW|4~6 KEY|ENT TYPE|ESC CLOSE", "");
}

bool ui_keyboard_visible(void)
{
	return kb && !lv_obj_has_flag(kb, LV_OBJ_FLAG_HIDDEN);
}

/* ---- slider rows ---------------------------------------------------------------- */

static void slider_event(lv_event_t *e)
{
	lv_obj_t *slider = lv_event_get_target(e);
	lv_obj_t *value = lv_event_get_user_data(e);

	lv_label_set_text_fmt(value, "%d", (int)lv_slider_get_value(slider));
}

lv_obj_t *ui_slider_row(lv_obj_t *list, const char *text, int min, int max, int value)
{
	const ui_palette_t *p = ui_palette();
	lv_obj_t *row = lv_obj_create(list), *l, *v, *s;

	lv_obj_remove_style_all(row);
	lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_ver(row, 4, 0);
	lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

	l = lv_label_create(row);
	lv_label_set_text(l, text);
	lv_obj_set_style_text_font(l, ui_font(16), 0);
	lv_obj_set_style_text_color(l, p->text, 0);
	v = lv_label_create(row);
	lv_obj_set_style_text_font(v, ui_font(16), 0);
	lv_obj_set_style_text_color(v, p->accent, 0);
	lv_obj_align(v, LV_ALIGN_TOP_RIGHT, 0, 0);

	s = lv_slider_create(row);
	lv_slider_set_range(s, min, max);
	lv_slider_set_value(s, value, LV_ANIM_OFF);
	lv_obj_set_width(s, LV_PCT(96));
	lv_obj_set_height(s, 6);
	lv_obj_align(s, LV_ALIGN_TOP_MID, 0, 28);
	lv_obj_set_style_margin_bottom(s, 10, 0);
	lv_obj_set_style_bg_color(s, p->line, 0);
	lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(s, 1, 0);
	lv_obj_set_style_bg_color(s, p->dim, LV_PART_INDICATOR);
	lv_obj_set_style_bg_color(s, p->accent, LV_PART_INDICATOR | LV_STATE_FOCUSED);
	lv_obj_set_style_radius(s, 1, LV_PART_INDICATOR);
	lv_obj_set_style_bg_color(s, p->text, LV_PART_KNOB);
	lv_obj_set_style_pad_all(s, 5, LV_PART_KNOB);
	lv_obj_set_style_radius(s, 1, LV_PART_KNOB);
	lv_obj_set_style_bg_color(s, p->accent, LV_PART_KNOB | LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(s, p->accent, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_width(s, 1, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(s, 6, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_opa(s, LV_OPA_40, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(s, p->accent, LV_STATE_EDITED);
	lv_obj_set_style_outline_width(s, 2, LV_STATE_EDITED);
	lv_obj_set_style_outline_opa(s, LV_OPA_COVER, LV_STATE_EDITED);
	lv_obj_add_flag(s, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
	lv_obj_add_event_cb(s, slider_event, LV_EVENT_VALUE_CHANGED, v);
	lv_label_set_text_fmt(v, "%d", value);
	lv_obj_set_height(row, 46);
	lv_group_add_obj(ui_group(), s);
	return s;
}
