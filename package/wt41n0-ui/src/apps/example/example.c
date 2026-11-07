/*
 * Example app: the smallest complete wt41n0-ui app, as a template.
 *
 * Build it like the other apps (CMakeLists.txt) and install a manifest next to the
 * others (example.app), or put both on the unit: the binary anywhere, the manifest in
 * /etc/wt41n0/apps/. The launcher shows a tile for it at its next start.
 */
#include "wt41n0ui.h"

static lv_obj_t *counter;
static int clicks;

static void quit(void)
{
	ui_quit(0);
}

static void clicked(lv_event_t *e)
{
	(void)e;
	lv_label_set_text_fmt(counter, "PRESSED %d TIMES", ++clicks);
}

int main(void)
{
	lv_obj_t *content, *list, *row;

	ui_init();				/* display, keys, touch, theme */
	content = ui_screen("EXAMPLE");		/* status bar + content area + hint bar */
	ui_hint("ENTER PRESS   ESC BACK");
	ui_on_back(quit);			/* Esc ends the app: back to the launcher */

	list = ui_list(content);
	row = ui_list_button(list, LV_SYMBOL_OK, "PRESS ME");
	lv_obj_add_event_cb(row, clicked, LV_EVENT_CLICKED, NULL);

	counter = lv_label_create(list);
	lv_label_set_text(counter, "PRESSED 0 TIMES");
	lv_obj_set_style_text_font(counter, ui_font(16), 0);
	lv_obj_set_style_text_color(counter, ui_palette()->dim, 0);

	return ui_run();			/* until ui_quit() */
}
