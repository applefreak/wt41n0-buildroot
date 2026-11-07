/*
 * Platform layer for the host screenshot harness (not built for the unit): an
 * in-memory 320x240 display, a simulated clock, and scripted key presses.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_internal.h"
#include "platform_host.h"

static uint16_t screen[UI_HEIGHT][UI_WIDTH];
static uint32_t now_ms;

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
	const uint16_t *src = (const uint16_t *)px_map;
	int w = lv_area_get_width(area), y;

	for (y = area->y1; y <= area->y2; y++)
		memcpy(&screen[y][area->x1], src + (size_t)(y - area->y1) * w, (size_t)w * 2);
	lv_display_flush_ready(disp);
}

uint32_t platform_tick(void) { return now_ms; }

const char *platform_conf_path(void)
{
	const char *p = getenv("WT41N0_UI_CONF");

	return p ? p : "ui.conf";
}

void platform_init(void)
{
	static uint16_t buf1[UI_WIDTH * 40], buf2[UI_WIDTH * 40];
	lv_display_t *disp = lv_display_create(UI_WIDTH, UI_HEIGHT);

	lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
	lv_display_set_flush_cb(disp, flush_cb);
	lv_display_set_buffers(disp, buf1, buf2, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
}

void platform_wait(uint32_t ms) { (void)ms; }
void platform_suspend(void) {}
void platform_resume(void) {}
int platform_wait_key(int timeout_ms, unsigned int *code, int *value)
{
	(void)timeout_ms; (void)code; (void)value;
	return 0;
}
int platform_vt_activate(int vt) { (void)vt; return 0; }
void platform_exit(void) {}

int platform_battery(bool *charging)
{
	if (charging)
		*charging = false;
	return 87;
}

int platform_wifi(void) { return 72; }

/* Simulated touch panel: raw = a rotated, mirrored, offset mapping of the screen,
 * as a real resistive panel might report it. host_touch() presses at a screen point. */
static int sim_rx, sim_ry;
static bool sim_down, sim_have_cal;
static double sim_cal[6];

bool platform_has_touch(void) { return true; }
bool platform_touch_raw(int *x, int *y) { *x = sim_rx; *y = sim_ry; return sim_down; }
void platform_touch_enable(bool on) { (void)on; }
void platform_touch_set_cal(const double cal[6]) { memcpy(sim_cal, cal, sizeof(sim_cal)); sim_have_cal = true; }

void host_touch(int x, int y, uint32_t ms)
{
	sim_rx = 3500 - y * 12 + x / 40;	/* axes swapped, y mirrored, slight skew */
	sim_ry = 400 + x * 11;
	sim_down = true;
	host_advance(ms);
	sim_down = false;
	host_advance(150);
}

int host_touch_map(int rx, int ry, int *x, int *y)
{
	if (!sim_have_cal)
		return -1;
	*x = (int)(sim_cal[0] * rx + sim_cal[1] * ry + sim_cal[2] + 0.5);
	*y = (int)(sim_cal[3] * rx + sim_cal[4] * ry + sim_cal[5] + 0.5);
	return 0;
}

void host_advance(uint32_t ms)
{
	uint32_t end = now_ms + ms;

	while (now_ms < end) {
		now_ms += 5;
		lv_timer_handler();
	}
}

void host_key(unsigned int code)
{
	keys_event(code, 1);
	host_advance(30);
	keys_event(code, 0);
	host_advance(120);
}

int host_shot(const char *path)
{
	unsigned char row[UI_WIDTH * 3], hdr[54] = { 'B', 'M' };
	unsigned int size = sizeof(hdr) + sizeof(row) * UI_HEIGHT;
	FILE *f = fopen(path, "wb");
	int x, y;

	if (!f)
		return -1;
	lv_refr_now(NULL);
	hdr[2] = size; hdr[3] = size >> 8; hdr[4] = size >> 16;
	hdr[10] = 54; hdr[14] = 40;
	hdr[18] = UI_WIDTH & 0xff; hdr[19] = UI_WIDTH >> 8;
	hdr[22] = UI_HEIGHT & 0xff; hdr[23] = UI_HEIGHT >> 8;
	hdr[26] = 1; hdr[28] = 24;
	fwrite(hdr, 1, sizeof(hdr), f);
	for (y = UI_HEIGHT - 1; y >= 0; y--) {	/* BMP rows go bottom-up, BGR */
		for (x = 0; x < UI_WIDTH; x++) {
			uint16_t c = screen[y][x];

			row[x * 3 + 2] = (unsigned char)(((c >> 11) & 0x1f) * 255 / 31);
			row[x * 3 + 1] = (unsigned char)(((c >> 5) & 0x3f) * 255 / 63);
			row[x * 3 + 0] = (unsigned char)((c & 0x1f) * 255 / 31);
		}
		fwrite(row, 1, sizeof(row), f);
	}
	return fclose(f);
}
