/* Power: reboot or power off, each confirmed by pressing Enter a second time. */
#include <stdlib.h>
#include "wt41n0ui.h"

static lv_obj_t *rows[2];
static int armed = -1;			/* row waiting for its confirmation */

static void quit(void)
{
	ui_quit(0);
}

static void choose(lv_event_t *e)
{
	int i = (int)(intptr_t)lv_event_get_user_data(e);

	if (armed != i) {		/* first press: ask for confirmation */
		armed = i;
		ui_list_button_set_text(rows[0], LV_SYMBOL_REFRESH, i == 0 ? "REBOOT  // AGAIN TO CONFIRM" : "REBOOT");
		ui_list_button_set_text(rows[1], LV_SYMBOL_POWER, i == 1 ? "POWER OFF  // AGAIN TO CONFIRM" : "POWER OFF");
		return;
	}
	ui_list_button_set_text(rows[i], NULL, i == 0 ? "REBOOTING ..." : "POWERING OFF ...");
	lv_refr_now(NULL);
	if (system(i == 0 ? "reboot" : "poweroff") != 0) {
		ui_list_button_set_text(rows[i], LV_SYMBOL_WARNING, i == 0 ? "REBOOT FAILED" : "POWER OFF FAILED");
		armed = -1;
	}
}

int main(void)
{
	lv_obj_t *list;

	ui_init();
	list = ui_list(ui_screen("POWER"));
	ui_hint("ENTER TWICE TO CONFIRM   ESC BACK");
	ui_on_back(quit);
	rows[0] = ui_list_button(list, LV_SYMBOL_REFRESH, "REBOOT");
	rows[1] = ui_list_button(list, LV_SYMBOL_POWER, "POWER OFF");
	lv_obj_add_event_cb(rows[0], choose, LV_EVENT_CLICKED, (void *)0);
	lv_obj_add_event_cb(rows[1], choose, LV_EVENT_CLICKED, (void *)1);
	return ui_run();
}
