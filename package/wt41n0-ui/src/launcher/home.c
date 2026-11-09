/*
 * Launcher home screen (SPEC.md, Screens > Home): 3 x 2 numbered tiles per page, a
 * readout strip with live data for the focused app, pages when there are more than six.
 * ↑↓ walk the tiles in reading order across pages, 1-6 open the numbered tile on the
 * current page, ENT opens the focused one.
 */
#include <stdio.h>
#include "wt41n0ui.h"
#include "apps.h"
#include "readout.h"
#include "home.h"

#define COLS		3
#define ROWS		2
#define PER_PAGE	(COLS * ROWS)
#define SEGMENTS	8
#define BORDER		1		/* panel border: child positions are inside it */

/* content-area coordinates of an absolute y (tokens give absolute positions) */
#define CY(y)		((y) - WT_CONTENT_Y)

static struct app apps[MAX_APPS];
static int napps, focus_index, page;
static lv_obj_t *content, *tiles[PER_PAGE];
static struct {
	lv_obj_t *number, *meta, *icon, *name;
	const char *text;
} tile_parts[PER_PAGE];
static struct {
	lv_obj_t *chip, *line, *seg[SEGMENTS];
} strip;
static lv_timer_t *refresh_timer;

static void build_page(int new_page, int focus_on);

static void set_tile_look(int slot, bool focused)
{
	ui_number_focus(tile_parts[slot].number, focused);
	ui_style_icon_focus(tile_parts[slot].icon, focused);
	ui_name_focus(tile_parts[slot].name, tile_parts[slot].text, focused);
}

static void update_readouts(void)
{
	const ui_palette_t *p = ui_palette();
	struct readout r;
	int slot, i;

	for (slot = 0; slot < PER_PAGE && page * PER_PAGE + slot < napps; slot++) {
		readout_get(&apps[page * PER_PAGE + slot], &r);
		lv_label_set_text(tile_parts[slot].meta, r.meta);
	}
	if (focus_index >= napps)
		return;
	readout_get(&apps[focus_index], &r);
	lv_label_set_text(strip.chip, apps[focus_index].name);
	lv_label_set_text(strip.line, r.line);
	for (i = 0; i < SEGMENTS; i++) {
		lv_obj_set_style_bg_color(strip.seg[i], i < r.level ? p->accent : p->line, 0);
		lv_obj_set_style_bg_opa(strip.seg[i], r.level < 0 ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
	}
}

static void refresh_cb(lv_timer_t *t)
{
	(void)t;
	update_readouts();
}

static void toast(const char *text)
{
	const ui_palette_t *p = ui_palette();
	lv_obj_t *t = lv_label_create(content);

	lv_label_set_text(t, text);
	ui_text_role(t, UI_ROLE_DATA);
	lv_obj_set_style_text_color(t, p->bg, 0);
	lv_obj_set_style_bg_color(t, p->warn, 0);
	lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_hor(t, 8, 0);
	lv_obj_set_style_pad_ver(t, 3, 0);
	lv_obj_align(t, LV_ALIGN_BOTTOM_MID, 0, -4);
	lv_obj_delete_delayed(t, 1500);
}

/* After an app: settings or manifests may have changed. Deferred, because the tile
 * whose click started the app is still handling that event. */
static void rebuild(void *arg)
{
	(void)arg;
	launcher_home_create();
}

static void launch(void *arg)
{
	int i = (int)(intptr_t)arg;

	if (apps_launch(&apps[i]) == 0) {
		lv_async_call(rebuild, NULL);
	} else {
		char buf[48];

		snprintf(buf, sizeof(buf), "%s // FAILED TO START", apps[i].name);
		toast(buf);
		ui_focus_ring_refresh();	/* the ring is still grown: back to the tile */
	}
}

static void open_app(int i)
{
	if (i != focus_index && tiles[i - page * PER_PAGE])
		lv_group_focus_obj(tiles[i - page * PER_PAGE]);	/* a digit: grow from its tile */
	focus_index = i;
	ui_focus_ring_grow(launch, (void *)(intptr_t)i);
}

static void tile_event(lv_event_t *e)
{
	int slot = (int)(intptr_t)lv_event_get_user_data(e);
	uint32_t key;

	switch (lv_event_get_code(e)) {
	case LV_EVENT_FOCUSED:
		focus_index = page * PER_PAGE + slot;
		set_tile_look(slot, true);
		update_readouts();
		break;
	case LV_EVENT_DEFOCUSED:
		set_tile_look(slot, false);
		break;
	case LV_EVENT_CLICKED:
		open_app(page * PER_PAGE + slot);
		break;
	case LV_EVENT_KEY:
		key = lv_event_get_key(e);
		if (key >= '1' && key < '1' + PER_PAGE && page * PER_PAGE + (int)(key - '1') < napps)
			open_app(page * PER_PAGE + (int)(key - '1'));
		break;
	default:
		break;
	}
}

/* ↑↓ past the last or first tile of a page: the next or previous page */
static void page_edge(lv_group_t *g, bool next)
{
	int pages = (napps + PER_PAGE - 1) / PER_PAGE;
	int to = (page + (next ? 1 : pages - 1)) % pages;

	(void)g;
	if (to == page) {	/* one page: wrap within it */
		lv_group_focus_obj(tiles[next ? 0 : (napps - 1) % PER_PAGE]);
		return;
	}
	build_page(to, next ? to * PER_PAGE : (to * PER_PAGE + PER_PAGE - 1 < napps ?
						to * PER_PAGE + PER_PAGE - 1 : napps - 1));
}

static lv_obj_t *label(lv_obj_t *parent, ui_role_t role, lv_color_t color, const char *s)
{
	lv_obj_t *l = lv_label_create(parent);

	ui_text_role(l, role);
	lv_obj_set_style_text_color(l, color, 0);
	lv_label_set_text(l, s);
	return l;
}

static void make_tile(int slot, struct app *a)
{
	const ui_palette_t *p = ui_palette();
	int col = slot % COLS, row = slot / COLS;
	lv_obj_t *t;
	char num[4];

	t = ui_panel_kind(content, UI_PANEL_TILE, WT_TILE_X0 + col * (WT_TILE_W + WT_TILE_GAP),
		     CY(WT_TILE_Y0 + row * (WT_TILE_H + WT_TILE_GAP)), WT_TILE_W, WT_TILE_H, true);
	tiles[slot] = t;

	snprintf(num, sizeof(num), "%d", slot + 1);
	tile_parts[slot].number = ui_number(t, num);
	lv_obj_set_pos(tile_parts[slot].number, 5 - BORDER, 5 - BORDER);
	tile_parts[slot].meta = label(t, UI_ROLE_DATA, p->dim, "");
	lv_obj_align(tile_parts[slot].meta, LV_ALIGN_TOP_RIGHT, -(6 - BORDER), 7 - BORDER - 2);
	tile_parts[slot].icon = ui_style_icon(t, a->icon, 32);
	lv_obj_set_pos(tile_parts[slot].icon, 33 - BORDER, 14 - BORDER);
	tile_parts[slot].text = a->name;
	tile_parts[slot].name = label(t, UI_ROLE_NAME, p->text, a->name);
	lv_obj_align(tile_parts[slot].name, LV_ALIGN_TOP_MID, 0, 50 - BORDER);
	set_tile_look(slot, false);
	lv_obj_add_event_cb(t, tile_event, LV_EVENT_ALL, (void *)(intptr_t)slot);
}

static void make_strip(void)
{
	const ui_palette_t *p = ui_palette();
	lv_obj_t *s = ui_panel_kind(content, UI_PANEL_STRIP, WT_INNER_X, CY(WT_READOUT_Y), WT_INNER_W,
				    WT_READOUT_H, false);
	wt_style_t style = ui_theme_style();
	int i;

	strip.chip = ui_chip(s, UI_ROLE_NAME, "");
	lv_obj_align(strip.chip, LV_ALIGN_LEFT_MID, 8, 0);
	if (style == WT_STYLE_CYBER) {	/* hairline under the label row */
		lv_obj_t *h = lv_obj_create(s);

		lv_obj_remove_style_all(h);
		lv_obj_set_size(h, 80, 1);
		lv_obj_set_style_bg_color(h, p->line, 0);
		lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);
		lv_obj_align(h, LV_ALIGN_LEFT_MID, 8, 9);
	}
	strip.line = label(s, style == WT_STYLE_DECK ? UI_ROLE_KEY_SMALL : UI_ROLE_DATA,
			   style == WT_STYLE_DECK ? p->accent : p->text, "");
	lv_obj_align(strip.line, LV_ALIGN_LEFT_MID, 96, 0);
	lv_obj_set_width(strip.line, WT_INNER_W - 96 - 8 * 4 - 14);
	lv_label_set_long_mode(strip.line, LV_LABEL_LONG_CLIP);
	/* 8-segment bar, right: 3 px segments, rising */
	for (i = 0; i < SEGMENTS; i++) {
		int h = 4 + i * 2;

		strip.seg[i] = lv_obj_create(s);
		lv_obj_remove_style_all(strip.seg[i]);
		lv_obj_set_size(strip.seg[i], 3, h);
		lv_obj_set_style_bg_opa(strip.seg[i], LV_OPA_COVER, 0);
		lv_obj_align(strip.seg[i], LV_ALIGN_RIGHT_MID, -(8 + (SEGMENTS - 1 - i) * 4), (18 - h) / 2);
	}
}

/* Home's screen is going away: stop updating its labels */
static void content_deleted(lv_event_t *e)
{
	if (lv_event_get_target(e) != content)
		return;
	if (refresh_timer)
		lv_timer_delete(refresh_timer);
	refresh_timer = NULL;
	content = NULL;
}

static void build_page(int new_page, int focus_on)
{
	int pages = (napps + PER_PAGE - 1) / PER_PAGE, slot;
	char marker[12];

	page = new_page;
	content = ui_screen(NULL);
	lv_obj_add_event_cb(content, content_deleted, LV_EVENT_DELETE, NULL);
	snprintf(marker, sizeof(marker), "%d/%d", page + 1, pages > 0 ? pages : 1);
	ui_hint_keys("\xE2\x86\x91\xE2\x86\x93 SELECT|1-6 JUMP|ENT OPEN", marker);
	lv_group_set_wrap(ui_group(), false);
	lv_group_set_edge_cb(ui_group(), page_edge);

	if (!napps) {
		lv_obj_t *l = label(content, UI_ROLE_NAME, ui_palette()->dim,
				    "NO APPS INSTALLED\n/usr/share/wt41n0/apps");

		lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
		lv_obj_center(l);
		return;
	}
	for (slot = 0; slot < PER_PAGE; slot++) {
		tiles[slot] = NULL;
		if (page * PER_PAGE + slot < napps)
			make_tile(slot, &apps[page * PER_PAGE + slot]);
	}
	make_strip();
	focus_index = focus_on;
	lv_group_focus_obj(tiles[focus_on - page * PER_PAGE]);
	update_readouts();
	if (!refresh_timer)
		refresh_timer = lv_timer_create(refresh_cb, 2000, NULL);
}

void launcher_home_create(void)
{
	napps = apps_load(apps, MAX_APPS);
	if (focus_index >= napps)
		focus_index = 0;
	build_page(napps ? focus_index / PER_PAGE : 0, napps ? focus_index : 0);
}
