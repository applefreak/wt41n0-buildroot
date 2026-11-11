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
#include "../lib/ui_internal.h"
#include "src/misc/lv_text_private.h"

int power_main(void);
int scanner_main(void);
int music_main(void);
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

/* Every icon on a grid (a check of their centring and size): "32" and "18" are plain
 * icons of that size, "ring" is ui_style_icon(32) as on a tile. The layout is printed
 * so a script can measure each cell: "icons <tag> <box> <cols> <pitch>: names..." */
static const char *const icon_names[] = {
	"wifi", "settings", "power", "list", "keyboard", "eye", "home", "image", "audio", "volume",
	"video", "bluetooth", "usb", "battery", "charge", "file", "folder", "edit", "bell", "call",
	"mail", "gps", "refresh", "play", "download", "upload", "warning", "ok", "trash", "drive",
	"sd", "tint", "check", "link", "pause", "stop", "prev", "next", "minus", "plus", "shuffle",
	"loop", "mute", "terminal", "scanner", "system", "display", "theme", "clock", "touch",
};

static void shot(const char *what);

static void icon_sheet(const char *tag, int size)
{
	int n = (int)(sizeof(icon_names) / sizeof(icon_names[0])), box = size > 0 ? size : 32;
	int pitch = box + (box >= 32 ? 3 : 8), cols = 320 / pitch, i;	/* 9 x 6 cells at 32 */
	lv_obj_t *scr = lv_obj_create(NULL), *o;
	char name[32];

	lv_obj_remove_style_all(scr);
	lv_obj_set_style_bg_color(scr, ui_palette()->bg, 0);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
	for (i = 0; i < n; i++) {
		o = size > 0 ? ui_icon_sized(scr, icon_names[i], size) : ui_style_icon(scr, icon_names[i], 32);
		if (size > 0)
			ui_icon_color(o, ui_palette()->text);
		lv_obj_set_pos(o, 4 + (i % cols) * pitch, 4 + (i / cols) * pitch);
	}
	if (size == 18) {	/* the glyph boxes of the 18 px face, as the font reports them */
		const lv_font_t *f = &lv_font_montserrat_18;

		printf("glyphs18:");
		for (i = 0; i < n; i++) {
			const char *sym = icon_symbol(icon_names[i]);
			lv_font_glyph_dsc_t g;

			uint32_t at = 0;

			if (sym && lv_font_get_glyph_dsc(f, &g, lv_text_encoded_next(sym, &at), 0))
				printf(" %s=%ux%u+%d/adv%u", icon_names[i], g.box_w, g.box_h, g.ofs_x, g.adv_w);
		}
		printf("\n");
	}
	lv_screen_load(scr);
	host_advance(50);
	snprintf(name, sizeof(name), "icons-%s", tag);
	shot(name);
	printf("icons %s %d %d %d:", tag, box, cols, pitch);
	for (i = 0; i < n; i++)
		printf(" %s", icon_names[i]);
	printf("\n");
}

static void number_sheet(void)
{
	lv_obj_t *scr = lv_obj_create(NULL), *o;
	char text[4];
	int i;

	lv_obj_remove_style_all(scr);
	lv_obj_set_style_bg_color(scr, ui_palette()->bg, 0);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
	for (i = 0; i < 12; i++) {
		snprintf(text, sizeof(text), "%02d", i + 1);
		o = ui_number(scr, text);
		lv_obj_set_pos(o, 4 + i * 26, 4);
	}
	lv_screen_load(scr);
	host_advance(50);
	shot("numbers");
	printf("icons numbers 18 12 26: 01 02 03 04 05 06 07 08 09 10 11 12\n");
}

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

	{	/* the faces' metrics, for the vertical padding of labels with a background */
		static const ui_role_t rl[] = { UI_ROLE_TITLE, UI_ROLE_NAME, UI_ROLE_DATA, UI_ROLE_KEY_SMALL };
		static const char *const rn[] = { "TITLE", "NAME", "DATA", "KEY_SMALL" };
		unsigned int k;

		printf("fonts %s:", style);
		for (k = 0; k < sizeof(rl) / sizeof(rl[0]); k++) {
			const lv_font_t *f = ui_font_role(rl[k]);
			lv_font_glyph_dsc_t g;

			lv_font_get_glyph_dsc(f, &g, 'D', 0);
			printf(" %s=line%d/base%d/D:%ux%u+%d", rn[k], (int)f->line_height, (int)f->base_line,
			       g.box_w, g.box_h, g.ofs_y);
		}
		printf("\n");
	}
	icon_sheet("32", 32);
	icon_sheet("ring", 0);
	icon_sheet("18", 18);
	number_sheet();

	settings("theme");
	shot("theme");
	host_key(KEY_DOWN);		/* previews the next style */
	host_advance(300);
	shot("theme-preview");
	keys_press_only(KEY_DOWN);	/* the third style: caught 10 ms in, the ring must already be on it */
	shot("theme-moving");
	host_advance(300);
	host_key(KEY_ESC);		/* back with the applied one */
	host_advance(300);
	ui_theme_set(st, "auto", "auto", false);

	settings("tz");
	shot("timezone");

	ui_conf_set("keylight_mode", "auto");
	ui_keylight_apply();
	settings("keylight");
	shot("keylight");
	host_key(KEY_ENTER);		/* MODE: AUTO -> ALWAYS ON */
	host_advance(200);
	shot("keylight-on");

	power_main();
	host_advance(300);
	shot("power");
	keys(KEY_DOWN, 2);
	host_key(KEY_ENTER);		/* arm POWER OFF */
	host_advance(1200);
	shot("power-armed");
	host_key(KEY_ESC);

	/* music: canned MPD state (apps/music/mpdc.c) */
	music_main();
	host_advance(300);
	shot("music");
	host_key(KEY_1 + 2);		/* 3: RADIO */
	host_advance(300);
	shot("music-radio");
	host_key(KEY_ESC);
	host_advance(300);
	host_key(KEY_1 + 1);		/* 2: LIBRARY */
	host_advance(300);
	shot("music-library");
	keys(KEY_DOWN, 2);
	host_key(KEY_ENTER);		/* ALBUMS */
	host_advance(300);
	host_key(KEY_DOWN);
	host_key(KEY_ENTER);		/* LOW TIDE */
	host_advance(300);
	shot("music-album");
	keys(KEY_ESC, 3);
	host_advance(300);
	host_key(KEY_1);		/* 1: QUEUE */
	host_advance(300);
	shot("music-queue");
	keys(KEY_DOWN, 4);		/* row 6: an online song without tags */
	host_advance(300);
	shot("music-queue-online");
	keys(KEY_UP, 8);		/* wraps to the end: rows 57-60 */
	host_advance(300);
	shot("music-queue-end");
	keys(KEY_ESC, 2);
	host_advance(300);

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

	/* touch scrolling a list (the calibration above is stored now): the row under the
	 * finger must not take the focus, and the ring stays on the focused row */
	music_main();
	host_advance(300);
	host_key(KEY_1);			/* QUEUE, focus on row 3 */
	host_advance(300);
	host_press_at(150, 190);		/* on row 5 */
	host_drag_to(150, 150, 200);
	shot("touch-scroll-dragging");
	host_drag_to(150, 90, 200);
	host_release();
	host_advance(800);			/* the scroll throw settles */
	shot("touch-scroll-after");
	host_touch(150, 120, 80);		/* a tap: focuses that row and plays it */
	host_advance(400);
	shot("touch-tap");
	return 0;
}
