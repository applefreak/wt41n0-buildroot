/* wt41n0-launcher: the home screen on the LCD, after the boot splash. */
#include <string.h>
#include "wt41n0ui.h"
#include "home.h"
#include "../apps/settings/wpa.h"

static int step;

static void calibrated(bool saved)
{
	(void)saved;
	launcher_home_create();
}

static void home(void)
{
	/* like the stock OS: calibrate the touch panel on the first start */
	if (ui_touch_present() && !ui_touch_calibrated())
		ui_touch_calibrate(calibrated);
	else
		launcher_home_create();
}

/* a few real steps, one per frame, so the splash shows progress */
static void boot_step(lv_timer_t *t)
{
	char ssid[140], state[32], ip[48];

	switch (step++) {
	case 0:
		ui_splash_progress(30, "READING APPS");
		break;
	case 1:
		if (wpa_status(ssid, sizeof(ssid), state, sizeof(state), ip, sizeof(ip)))
			ui_splash_progress(60, "WI-FI IS OFF");
		else
			ui_splash_progress(60, strcmp(state, "COMPLETED") ? "STARTING WI-FI" : "WI-FI UP");
		break;
	case 2:
		ui_splash_progress(100, "READY");
		break;
	default:
		lv_timer_delete(t);
		ui_screen_transition(UI_SLIDE_NONE);
		home();
		break;
	}
}

int main(void)
{
	ui_init();
	ui_splash(WT41N0_UI_VERSION);
	ui_splash_progress(10, "LOADING THEME");
	lv_timer_create(boot_step, 150, NULL);
	return ui_run();
}
