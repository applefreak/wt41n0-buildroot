/*
 * Host screenshot harness: renders the launcher screens at 320x240 into BMP files,
 * driven by the same key handling as the unit (Linux key codes).
 *   wt41n0-ui-shots <output directory>
 */
#include <stdio.h>
#include <stdlib.h>
#include "wt41n0ui.h"
#include "platform_host.h"
#include "../launcher/home.h"

int power_main(void);
int example_main(void);

#define KEY_DOWN	108
#define KEY_UP		103
#define KEY_GRAY	58

static const char *dir;

static void shot(const char *name)
{
	char path[512];

	snprintf(path, sizeof(path), "%s/%s.bmp", dir, name);
	if (host_shot(path))
		perror(path);
	else
		printf("%s\n", path);
}

int main(int argc, char **argv)
{
	dir = argc > 1 ? argv[1] : ".";
	setenv("WT41N0_UI_CONF", "/dev/null", 0);
	setenv("WT41N0_UI_APPS", WT41N0_UI_SRC "/apps/terminal:" WT41N0_UI_SRC "/apps/power:"
	       WT41N0_UI_SRC "/apps/example", 0);
	ui_init();
	ui_set_theme("night", "cyan");

	launcher_home_create();
	host_advance(300);
	shot("home");
	host_key(KEY_DOWN);
	host_key(KEY_DOWN);
	host_key(KEY_DOWN);
	shot("home-focus-4");
	host_key(KEY_GRAY);
	shot("home-gray-layer");
	host_key(KEY_GRAY);
	host_key(KEY_GRAY);	/* off again */
	host_key(28);		/* Enter */
	host_advance(100);
	shot("home-open");

	ui_set_theme("dusk", "magenta");
	launcher_home_create();
	host_advance(300);
	shot("home-dusk-magenta");
	ui_set_theme("void", "amber");
	launcher_home_create();
	host_key(KEY_UP);
	host_advance(300);
	shot("home-void-amber");
	ui_set_theme("night", "green");
	launcher_home_create();
	host_advance(300);
	shot("home-night-green");

	/* apps */
	power_main();
	host_advance(300);
	shot("app-power");
	host_key(28);		/* Enter: asks for confirmation */
	shot("app-power-confirm");
	example_main();
	host_key(28);
	host_key(28);
	shot("app-example");

	/* touch calibration against the simulated panel, slightly off target like a finger */
	ui_set_theme("night", "cyan");
	ui_touch_calibrate(NULL);
	host_advance(100);
	shot("calibrate-1");
	{
		static const int pts[6][2] = { { 161, 119 }, { 26, 22 }, { 294, 25 }, { 297, 214 }, { 23, 218 }, { 159, 121 } };
		int i, x, y, worst = 0;

		for (i = 0; i < 6; i++) {
			host_touch(pts[i][0], pts[i][1], 120);
			if (i == 2)
				shot("calibrate-3");
		}
		/* check the stored mapping on a grid of screen points */
		for (y = 10; y < 240; y += 50)
			for (x = 10; x < 320; x += 50) {
				int mx, my, rx = 3500 - y * 12 + x / 40, ry = 400 + x * 11;

				if (host_touch_map(rx, ry, &mx, &my)) {
					printf("calibration: not saved\n");
					return 1;
				}
				if (abs(mx - x) > worst) worst = abs(mx - x);
				if (abs(my - y) > worst) worst = abs(my - y);
			}
		printf("calibration: worst error %d px\n", worst);
	}
	return 0;
}
