/* Themed building blocks shared by the apps */
#include "ui_internal.h"

static lv_style_t st_row, st_row_focus, st_row_press;
static const ui_palette_t *styled_for;
static char styled_accent[16], styled_palette[16];

static void styles(void)
{
	const ui_palette_t *p = ui_palette();

	/* rebuild after a theme change */
	if (styled_for && !strcmp(styled_accent, ui_theme_accent()) && !strcmp(styled_palette, ui_theme_palette()))
		return;
	if (styled_for) {
		lv_style_reset(&st_row);
		lv_style_reset(&st_row_focus);
		lv_style_reset(&st_row_press);
	}
	styled_for = p;
	lv_snprintf(styled_accent, sizeof(styled_accent), "%s", ui_theme_accent());
	lv_snprintf(styled_palette, sizeof(styled_palette), "%s", ui_theme_palette());

	lv_style_init(&st_row);
	lv_style_set_bg_color(&st_row, p->panel);
	lv_style_set_bg_opa(&st_row, LV_OPA_COVER);
	lv_style_set_border_color(&st_row, p->line);
	lv_style_set_border_width(&st_row, 1);
	lv_style_set_radius(&st_row, 2);
	lv_style_set_pad_hor(&st_row, 10);
	lv_style_set_pad_ver(&st_row, 6);
	lv_style_set_text_color(&st_row, p->text);
	lv_style_set_text_font(&st_row, ui_font(16));

	lv_style_init(&st_row_focus);
	lv_style_set_border_color(&st_row_focus, p->accent);
	lv_style_set_border_width(&st_row_focus, 2);
	lv_style_set_outline_color(&st_row_focus, p->accent);
	lv_style_set_outline_width(&st_row_focus, 2);
	lv_style_set_outline_opa(&st_row_focus, LV_OPA_30);
	lv_style_set_bg_color(&st_row_focus, lv_color_mix(p->accent, p->panel, 28));
	lv_style_set_text_color(&st_row_focus, p->accent);

	lv_style_init(&st_row_press);
	lv_style_set_bg_color(&st_row_press, lv_color_mix(p->accent, p->panel, 90));
}

lv_obj_t *ui_list(lv_obj_t *parent)
{
	lv_obj_t *list = lv_obj_create(parent);

	lv_obj_remove_style_all(list);
	lv_obj_set_size(list, LV_PCT(100), LV_PCT(100));
	lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_all(list, 8, 0);
	lv_obj_set_style_pad_row(list, 6, 0);
	lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
	return list;
}

lv_obj_t *ui_list_button(lv_obj_t *list, const char *symbol, const char *text)
{
	lv_obj_t *row, *l;

	styles();
	row = lv_obj_create(list);
	lv_obj_remove_style_all(row);
	lv_obj_add_style(row, &st_row, 0);
	lv_obj_add_style(row, &st_row_focus, LV_STATE_FOCUSED);
	lv_obj_add_style(row, &st_row_press, LV_STATE_PRESSED);
	lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
	lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_ON_FOCUS);
	lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
	l = lv_label_create(row);
	if (symbol)
		lv_label_set_text_fmt(l, "%s  %s", symbol, text);
	else
		lv_label_set_text(l, text);
	lv_group_add_obj(ui_group(), row);
	return row;
}

void ui_list_button_set_text(lv_obj_t *row, const char *symbol, const char *text)
{
	lv_obj_t *l = lv_obj_get_child(row, 0);

	if (symbol)
		lv_label_set_text_fmt(l, "%s  %s", symbol, text);
	else
		lv_label_set_text(l, text);
}
