/*
 * Power (SPEC.md, Screens > Power): REBOOT (key 1) and POWER OFF (key 2) as big buttons.
 * The first ENT arms one (CONFIRM ..., warning border, 5 s countdown); a second ENT acts;
 * ESC or the timeout disarms. A digit focuses its button and arms it.
 */
#include <stdlib.h>
#include "wt41n0ui.h"

#define IDLE_HINT "\xE2\x86\x91\xE2\x86\x93 SELECT|ENT CHOOSE|ESC BACK"

static lv_obj_t *buttons[2];

static void quit(void)
{
	ui_quit(0);
}

static void act(void *data)
{
	int i = (int)(intptr_t)data;
	lv_obj_t *c = lv_obj_get_parent(buttons[0]);

	ui_status_line(c, UI_TONE_ACCENT, i ? "power" : "refresh", i ? "POWERING OFF" : "REBOOTING", "");
	lv_refr_now(NULL);
#ifndef WT41N0_UI_HOST
	if (system(i ? "poweroff" : "reboot") != 0)
		ui_status_line(c, UI_TONE_WARN, "warning", i ? "POWER OFF FAILED" : "REBOOT FAILED", "");
#endif
}

static void digit(lv_event_t *e)
{
	uint32_t k = lv_event_get_key(e);

	if (k == '1' || k == '2') {
		lv_group_focus_obj(buttons[k - '1']);
		lv_obj_send_event(buttons[k - '1'], LV_EVENT_CLICKED, NULL);
	}
}

int main(void)
{
	lv_obj_t *c;

	ui_init();
	c = ui_screen("POWER");
	ui_hint_keys(IDLE_HINT, "");
	ui_on_back(quit);
	buttons[0] = ui_big_button(c, 48, "refresh", "REBOOT", "RESTARTS THE LAUNCHER AND ALL APPS", "KEY 1",
				   IDLE_HINT, act, (void *)0);
	buttons[1] = ui_big_button(c, 128, "power", "POWER OFF", "SHUTS THE UNIT DOWN", "KEY 2",
				   IDLE_HINT, act, (void *)1);
	lv_obj_add_event_cb(buttons[0], digit, LV_EVENT_KEY, NULL);
	lv_obj_add_event_cb(buttons[1], digit, LV_EVENT_KEY, NULL);
	lv_group_focus_obj(buttons[0]);
	return ui_run();
}
