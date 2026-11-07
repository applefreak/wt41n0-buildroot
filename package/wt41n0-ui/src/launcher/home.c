/* Launcher home screen: a grid of app tiles. */
#include <stdio.h>
#include "wt41n0ui.h"
#include "apps.h"
#include "home.h"

#define COLS	3
#define TILE_W	94
#define TILE_H	86
#define GAP	9

static struct app apps[MAX_APPS];
static int napps;
static lv_obj_t *tile_obj[MAX_APPS];
static lv_obj_t *content;
static lv_style_t st_tile, st_focus, st_press;

static void styles_init(void)
{
	const ui_palette_t *p = ui_palette();
	static bool done;

	if (done) {
		lv_style_reset(&st_tile);
		lv_style_reset(&st_focus);
		lv_style_reset(&st_press);
	}
	done = true;
	lv_style_init(&st_tile);
	lv_style_set_bg_color(&st_tile, p->panel);
	lv_style_set_bg_opa(&st_tile, LV_OPA_COVER);
	lv_style_set_border_color(&st_tile, p->line);
	lv_style_set_border_width(&st_tile, 1);
	lv_style_set_radius(&st_tile, 2);

	/* the neon look: accent border plus a faint outer outline as the glow */
	lv_style_init(&st_focus);
	lv_style_set_border_color(&st_focus, p->accent);
	lv_style_set_border_width(&st_focus, 2);
	lv_style_set_outline_color(&st_focus, p->accent);
	lv_style_set_outline_width(&st_focus, 3);
	lv_style_set_outline_opa(&st_focus, LV_OPA_30);
	lv_style_set_outline_pad(&st_focus, 1);
	lv_style_set_bg_color(&st_focus, lv_color_mix(p->accent, p->panel, 28));

	lv_style_init(&st_press);
	lv_style_set_bg_color(&st_press, lv_color_mix(p->accent, p->panel, 90));
}

static void set_tile_colors(lv_obj_t *tile, bool focused)
{
	const ui_palette_t *p = ui_palette();

	lv_obj_set_style_text_color(lv_obj_get_child(tile, 0), focused ? p->accent2 : p->dim, 0);
	lv_obj_set_style_text_color(lv_obj_get_child(tile, 1), focused ? p->accent : p->dim, 0);
	lv_obj_set_style_text_color(lv_obj_get_child(tile, 2), focused ? p->accent : p->text, 0);
}

static void toast(const char *text)
{
	const ui_palette_t *p = ui_palette();
	lv_obj_t *t = lv_label_create(content);

	lv_label_set_text(t, text);
	lv_obj_set_style_text_font(t, ui_font(12), 0);
	lv_obj_set_style_text_color(t, p->bg, 0);
	lv_obj_set_style_bg_color(t, p->accent, 0);
	lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_hor(t, 8, 0);
	lv_obj_set_style_pad_ver(t, 3, 0);
	lv_obj_align(t, LV_ALIGN_BOTTOM_MID, 0, -4);
	lv_obj_delete_delayed(t, 1500);
}

static void open_tile(int i)
{
	if (apps_launch(&apps[i])) {
		char buf[48];

		snprintf(buf, sizeof(buf), "%s // FAILED TO START", apps[i].name);
		toast(buf);
	}
}

static void tile_event(lv_event_t *e)
{
	lv_obj_t *tile = lv_event_get_target(e);
	int i = (int)(intptr_t)lv_event_get_user_data(e);
	uint32_t key;

	switch (lv_event_get_code(e)) {
	case LV_EVENT_FOCUSED:
		set_tile_colors(tile, true);
		break;
	case LV_EVENT_DEFOCUSED:
		set_tile_colors(tile, false);
		break;
	case LV_EVENT_CLICKED:
		open_tile(i);
		break;
	case LV_EVENT_KEY:
		key = lv_event_get_key(e);
		if (key >= '1' && key <= '9' && (int)(key - '1') < napps) {
			lv_group_focus_obj(tile_obj[key - '1']);
			open_tile(key - '1');
		}
		break;
	default:
		break;
	}
}

void launcher_home_create(void)
{
	const ui_palette_t *p = ui_palette();
	int i, x0 = (UI_WIDTH - COLS * TILE_W - (COLS - 1) * GAP) / 2;
	int rows, y0;
	char num[4];

	napps = apps_load(apps, MAX_APPS);
	rows = (napps + COLS - 1) / COLS;
	/* centred when it fits (two rows); otherwise the grid scrolls with the focus */
	y0 = rows <= 2 ? (UI_HEIGHT - UI_STATUS_H - UI_HINT_H - rows * TILE_H - (rows - 1) * GAP) / 2 : GAP;

	styles_init();
	content = ui_screen(NULL);
	ui_hint(LV_SYMBOL_UP LV_SYMBOL_DOWN " SELECT   ENTER OPEN   HOLD ESC IN AN APP: HOME");
	if (rows > 2) {
		lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
		lv_obj_set_style_pad_bottom(content, GAP, 0);
	}
	if (!napps) {
		lv_obj_t *l = lv_label_create(content);

		lv_label_set_text(l, "NO APPS INSTALLED\n/usr/share/wt41n0/apps");
		lv_obj_set_style_text_font(l, ui_font(16), 0);
		lv_obj_set_style_text_color(l, p->dim, 0);
		lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
		lv_obj_center(l);
		return;
	}

	for (i = 0; i < napps; i++) {
		lv_obj_t *t = lv_obj_create(content), *l;

		tile_obj[i] = t;
		lv_obj_remove_style_all(t);
		lv_obj_add_style(t, &st_tile, 0);
		lv_obj_add_style(t, &st_focus, LV_STATE_FOCUSED);
		lv_obj_add_style(t, &st_press, LV_STATE_PRESSED);
		lv_obj_set_size(t, TILE_W, TILE_H);
		lv_obj_set_pos(t, x0 + (i % COLS) * (TILE_W + GAP), y0 + (i / COLS) * (TILE_H + GAP));
		lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_add_flag(t, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
		lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);

		snprintf(num, sizeof(num), "%02d", i + 1);
		l = lv_label_create(t);			/* child 0: number */
		lv_label_set_text(l, num);
		lv_obj_set_style_text_font(l, ui_font(12), 0);
		lv_obj_align(l, LV_ALIGN_TOP_LEFT, 5, 3);
		if (apps_icon_symbol(apps[i].icon)) {	/* child 1: icon */
			l = lv_label_create(t);
			lv_label_set_text(l, apps_icon_symbol(apps[i].icon));
			lv_obj_set_style_text_font(l, ui_font_icons(), 0);
		} else {
			char src[112];

			l = lv_image_create(t);
			snprintf(src, sizeof(src), "A:%s", apps[i].icon);
			lv_image_set_src(l, src);
		}
		lv_obj_align(l, LV_ALIGN_CENTER, 0, -6);
		l = lv_label_create(t);			/* child 2: name */
		lv_label_set_text(l, apps[i].name);
		lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
		lv_obj_set_style_max_width(l, TILE_W - 6, 0);
		lv_obj_set_style_text_font(l, ui_font(16), 0);
		lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -4);
		set_tile_colors(t, false);

		lv_obj_add_event_cb(t, tile_event, LV_EVENT_ALL, (void *)(intptr_t)i);
		lv_group_add_obj(ui_group(), t);
	}
	(void)p;
	lv_group_focus_obj(tile_obj[0]);
}
