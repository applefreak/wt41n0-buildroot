/*
 * Host screenshot harness: renders every screen in every style at 320x240 into BMP
 * files named <style>-<screen>.bmp, driven by the same key handling as the unit (Linux
 * key codes), plus a check of the touch calibration maths.
 *   wt41n0-ui-shots <output directory>
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "wt41n0ui.h"
#include "platform_host.h"
#include "../launcher/home.h"

int power_main(void);
int scanner_main(void);
int system_main(int argc, char **argv);
int settings_main(int argc, char **argv);

#define KEY_ESC		1
#define KEY_ENTER	28
#define KEY_TAB		15
#define KEY_DOWN	108
#define KEY_UP		103
#define KEY_GRAY	58
#define KEY_1		2

static const char *dir;
static const char *style;

static void shot(const char *what)
{
	char path[512];

	snprintf(path, sizeof(path), "%s/%s-%s.bmp", dir, style, what);
	if (host_shot(path))
		perror(path);
}

static void keys(unsigned int code, int n)
{
	while (n-- > 0)
		host_key(code);
}

static void settings(const char *screen)
{
	char *argv[] = { "settings", (char *)screen, NULL };

	settings_main(screen ? 2 : 1, argv);
	host_advance(300);
}

/* the touch calibration round the simulated panel answers: a finger slightly off target */
static const int taps[6][2] = { { 161, 119 }, { 26, 22 }, { 294, 25 }, { 297, 214 }, { 23, 218 }, { 159, 121 } };

static void style_pass(const char *st)
{
	style = st;
	ui_theme_set(st, "auto", "auto", false);

	ui_splash(WT41N0_UI_VERSION);
	ui_splash_progress(60, "STARTING WI-FI");
	host_advance(100);
	shot("boot");

	/* home: the reference set of apps (host/apps), as in claude-design/screens */
	setenv("WT41N0_UI_APPS", WT41N0_UI_SRC "/host/apps", 1);
	launcher_home_create();
	keys(KEY_UP, 6);		/* back to TERMINAL whatever the last pass left */
	host_advance(400);
	shot("home");
	host_key(KEY_DOWN);		/* WI-FI */
	host_key(KEY_GRAY);
	host_advance(300);
	shot("home-focus2-gray");
	keys(KEY_GRAY, 2);		/* off again */
	keys(KEY_DOWN, 5);		/* past SYSTEM: page 2 */
	host_advance(400);
	shot("home-page2");
	keys_press_only(KEY_ENTER);	/* launch: the ring grows to the screen edge */
	host_advance(70);
	shot("home-launch");
	host_advance(400);
	setenv("WT41N0_UI_APPS", WT41N0_UI_SRC "/apps/terminal:" WT41N0_UI_SRC "/apps/power:"
	       WT41N0_UI_SRC "/apps/settings", 1);

	settings(NULL);
	shot("settings");
	keys_press_only(KEY_DOWN);
	host_advance(60);
	shot("settings-ring-moving");
	host_advance(300);

	/* Wi-Fi: the list after its scan, DOCK-07 focused */
	settings("wifi");
	host_advance(3500);
	keys(KEY_DOWN, 3);		/* HOMENET-5G, GARAGE (saved), DOCK-07 */
	host_advance(300);
	shot("wifi");
	host_key(KEY_ENTER);		/* DOCK-07: new, WPA2 */
	host_advance(300);
	keys(KEY_GRAY, 1); host_key(5);	/* "h" via the gray layer, then more */
	for (int i = 0; i < 7; i++) {
		keys(KEY_GRAY, 1);
		host_key(6);
	}
	keys(KEY_DOWN, 3);		/* CONNECT */
	host_advance(300);
	shot("wifi-network");
	host_key(KEY_ENTER);
	host_advance(800);
	shot("wifi-connecting");
	host_advance(21000);		/* it never comes up */
	shot("wifi-wrong-password");
	keys(KEY_UP, 3);		/* the field */
	host_key(KEY_ENTER);		/* ENT EDIT: the keyboard */
	host_key(KEY_DOWN);
	host_key(5);			/* '4': a key to the left */
	host_advance(300);
	shot("keyboard");
	keys(KEY_ESC, 2);		/* keyboard, then back to the list */
	host_advance(300);
	host_key(KEY_UP);		/* GARAGE: saved */
	host_key(KEY_ENTER);
	host_advance(300);
	host_key(KEY_ENTER);		/* CONNECT */
	host_advance(600);
	shot("wifi-connected");
	host_advance(2000);

	settings("display");
	host_key(KEY_ENTER);		/* adjusting */
	host_key(KEY_DOWN);
	host_advance(200);
	shot("display-adjusting");
	settings("sound");
	shot("sound-idle");

	settings("theme");
	shot("theme");
	host_key(KEY_DOWN);		/* previews the next style */
	host_advance(300);
	shot("theme-preview");
	host_key(KEY_ESC);		/* back with the applied one */
	host_advance(300);
	ui_theme_set(st, "auto", "auto", false);

	settings("tz");
	shot("timezone");

	power_main();
	host_advance(300);
	host_key(KEY_DOWN);
	host_key(KEY_ENTER);		/* arm POWER OFF */
	host_advance(1200);
	shot("power-armed");
	host_key(KEY_ESC);

	/* scanner: the fake reader answers and reads three codes */
	unlink(getenv("WT41N0_SCANS"));
	scanner_main();
	host_advance(300);
	shot("scanner-starting");
	usleep(300000);			/* the fake reader runs in real time */
	host_advance(300);
	shot("scanner");
	host_key(KEY_DOWN);
	host_key(KEY_DOWN);
	host_key(KEY_ENTER);		/* the second scan, whole */
	host_advance(300);
	shot("scanner-detail");
	host_key(KEY_ENTER);		/* DELETE: armed */
	host_advance(1200);
	shot("scanner-delete-armed");
	host_key(KEY_ENTER);		/* confirmed: back to the list without it */
	host_advance(400);
	shot("scanner-after-delete");
	host_key(KEY_ESC);		/* quit: stops the reader */

	/* system: the captured (anonymised) values in host/sysroot */
	{
		char *argv1[] = { "system", NULL };

		system_main(1, argv1);
		host_advance(300);
		shot("system");
		host_key(KEY_ENTER);		/* DEVICE */
		host_advance(300);
		shot("system-device");
		host_key(KEY_DOWN);		/* HARDWARE ID */
		host_key(KEY_ENTER);
		host_advance(300);
		shot("system-value");
		host_key(KEY_ESC);
		host_key(KEY_ESC);
		host_advance(300);
		keys(KEY_DOWN, 6);		/* SENSORS */
		host_key(KEY_ENTER);
		host_advance(1300);
		shot("system-sensors");
		keys(KEY_ESC, 2);
	}

	ui_touch_calibrate(NULL);
	host_advance(100);
	shot("calibrate-1");
	for (int i = 0; i < 6; i++)	/* a round whose check tap misses */
		host_touch(i == 5 ? taps[i][0] + 30 : taps[i][0], i == 5 ? taps[i][1] - 20 : taps[i][1], 120);
	host_advance(100);
	shot("calibrate-missed");
	for (int i = 0; i < 2; i++)
		host_touch(taps[i][0], taps[i][1], 120);
	host_advance(100);
	shot("calibrate-3");
	host_key(KEY_ESC);
}

int main(int argc, char **argv)
{
	static const char *const styles[] = { "cyber", "aero", "deck", "nuke" };
	int i, x, y, worst = 0;

	dir = argc > 1 ? argv[1] : ".";
	setenv("WT41N0_UI_CONF", "/dev/null", 0);
	setenv("WT41N0_UI_FONTS", WT41N0_UI_SRC "/fonts", 0);
	setenv("WT41N0_SCAN_CMD", WT41N0_UI_SRC "/host/fake-scan.sh", 0);
	setenv("WT41N0_SCANS", "/tmp/wt41n0-ui-shots-scans", 0);
	setenv("WT41N0_SYSROOT", WT41N0_UI_SRC "/host/sysroot", 0);
	setenv("WT41N0_UI_APPS", WT41N0_UI_SRC "/host/apps", 0);
	ui_init();
	for (i = 0; i < 4; i++)
		style_pass(styles[i]);

	/* touch calibration against the simulated panel; check the stored mapping */
	style = "cyber";
	ui_theme_set("cyber", "auto", "auto", false);
	ui_touch_calibrate(NULL);
	for (i = 0; i < 6; i++)
		host_touch(taps[i][0], taps[i][1], 120);
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
	return 0;
}
