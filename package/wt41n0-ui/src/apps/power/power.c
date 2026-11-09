/*
 * Power: SUSPEND (key 1), REBOOT (key 2) and POWER OFF (key 3) as big buttons.
 * SUSPEND acts at once: the unit sleeps in RAM and the power button wakes it,
 * back on Home. REBOOT and POWER OFF ask first: the first ENT arms (CONFIRM ...,
 * warning border, 5 s countdown), a second ENT acts, ESC or the timeout disarms.
 * The power button opens this app from anywhere; pressed here, it closes it.
 *
 * The power button cannot start the unit from POWER OFF: it is wired to the
 * PMIC's RPWRON input, which does not start it here; the cradle or a battery
 * reinsertion does, which the button says.
 */
#include <stdlib.h>
#include "wt41n0ui.h"

#define IDLE_HINT "\xE2\x86\x91\xE2\x86\x93 SELECT|ENT CHOOSE|ESC BACK"
#define BTN_H	60			/* three buttons in the 192 px content */
#define BTN_GAP	6

static lv_obj_t *buttons[3];

static void quit(void)
{
	ui_quit(0);
}

static void status(enum ui_tone tone, const char *icon, const char *msg)
{
	ui_status_line(lv_obj_get_parent(buttons[0]), tone, icon, msg, "");
	lv_refr_now(NULL);
}

static void act(void *data)
{
	int i = (int)(intptr_t)data;

	if (i == 0) {
		status(UI_TONE_ACCENT, "power", "SUSPENDING");
#ifndef WT41N0_UI_HOST
		/* hand the screen and keys back while asleep; taking them back drops the
		 * key that woke the unit, so it cannot press a button here */
		ui_suspend();
		i = system("/usr/sbin/wt41n0-suspend");
		ui_resume();
		if (i != 0) {
			status(UI_TONE_WARN, "warning", "SUSPEND FAILED");
			return;
		}
#endif
		quit();			/* woken up: back to Home */
		return;
	}
	status(UI_TONE_ACCENT, i == 1 ? "refresh" : "power", i == 1 ? "REBOOTING" : "POWERING OFF");
#ifndef WT41N0_UI_HOST
	if (system(i == 1 ? "reboot" : "poweroff") != 0)
		status(UI_TONE_WARN, "warning", i == 1 ? "REBOOT FAILED" : "POWER OFF FAILED");
#endif
}

static void digit(lv_event_t *e)
{
	uint32_t k = lv_event_get_key(e);

	if (k >= '1' && k <= '3') {
		lv_group_focus_obj(buttons[k - '1']);
		lv_obj_send_event(buttons[k - '1'], LV_EVENT_CLICKED, NULL);
	}
}

int main(void)
{
	lv_obj_t *c;
	int i, y = WT_INNER_Y;

	ui_init();
	c = ui_screen("POWER");
	ui_hint_keys(IDLE_HINT, "");
	ui_on_back(quit);
	ui_on_power(quit);
	buttons[0] = ui_big_button_ex(c, y, BTN_H, "power", "SUSPEND", "THE POWER BUTTON WAKES IT",
				      "KEY 1", IDLE_HINT, act, (void *)0, false);
	y += BTN_H + BTN_GAP;
	buttons[1] = ui_big_button_ex(c, y, BTN_H, "refresh", "REBOOT", "RESTARTS THE UNIT", "KEY 2",
				      IDLE_HINT, act, (void *)1, true);
	y += BTN_H + BTN_GAP;
	buttons[2] = ui_big_button_ex(c, y, BTN_H, "power", "POWER OFF",
				      "ON AGAIN: CRADLE, OR REINSERT THE BATTERY", "KEY 3",
				      IDLE_HINT, act, (void *)2, true);
	for (i = 0; i < 3; i++)
		lv_obj_add_event_cb(buttons[i], digit, LV_EVENT_KEY, NULL);
	lv_group_focus_obj(buttons[0]);
	return ui_run();
}
