/* wt41n0-launcher: the home screen on the LCD. */
#include "wt41n0ui.h"
#include "home.h"

static void calibrated(bool saved)
{
	(void)saved;
	launcher_home_create();
}

int main(void)
{
	ui_init();
	/* like the stock OS: calibrate the touch panel on the first start */
	if (ui_touch_present() && !ui_touch_calibrated())
		ui_touch_calibrate(calibrated);
	else
		launcher_home_create();
	return ui_run();
}
