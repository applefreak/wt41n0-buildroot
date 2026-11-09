/*
 * Platform layer on the WT41N0: framebuffer, console, evdev input, system state.
 *
 * The panel is mounted rotated: the framebuffer is 240x320 and the landscape view
 * pixel (x, y) is at framebuffer (y, 319 - x). LVGL renders the 320x240 view in
 * partial buffers and the flush rotates each changed area into the framebuffer.
 *
 * Environment: WT41N0_UI_FB (default /dev/fb0), WT41N0_UI_TTY (default /dev/tty1,
 * "none" to leave the console alone), WT41N0_UI_NOGRAB (share the input devices
 * instead of grabbing them; set by the launcher for its apps), WT41N0_UI_NO_TOUCH
 * (ignore the touch panel), WT41N0_UI_CONF (default /etc/wt41n0/ui.conf).
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include "ui_internal.h"

#define BUF_LINES 40

static uint16_t *fb;
static unsigned int fb_w, fb_h, fb_stride;	/* pixels */
static bool rotated;
static int tty_fd = -1;
static int tty_kbmode = -1;		/* keyboard mode to restore */
static bool grab = true, suspended;

enum { DEV_KEYS, DEV_TOUCH };
static struct { int fd; int kind; } devs[8];
static int ndevs;

static struct {
	int min_x, max_x, min_y, max_y;
	int raw_x, raw_y;
	bool down;
	bool have_cal, enabled;
	double cal[6];		/* affine raw -> screen, from the calibration */
	lv_point_t point;
	bool pressed;
	lv_indev_t *indev;
} touch = { .enabled = true };

uint32_t platform_tick(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

const char *platform_conf_path(void)
{
	const char *p = getenv("WT41N0_UI_CONF");

	return p ? p : "/etc/wt41n0/ui.conf";
}

/* ---- display ------------------------------------------------------------------ */

struct bench_stats bench;

uint64_t platform_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
	const uint16_t *src = (const uint16_t *)px_map;
	int w = lv_area_get_width(area);
	int x, y;
	uint64_t t0 = platform_us();

	if (rotated) {
		/* view (x, y) -> fb (y, WT_SCREEN_W - 1 - x): write each fb row sequentially */
		for (x = area->x1; x <= area->x2; x++) {
			uint16_t *dst = fb + (size_t)(WT_SCREEN_W - 1 - x) * fb_stride + area->y1;
			const uint16_t *s = src + (x - area->x1);

			for (y = area->y1; y <= area->y2; y++, s += w)
				*dst++ = *s;
		}
	} else {
		for (y = area->y1; y <= area->y2; y++)
			memcpy(fb + (size_t)y * fb_stride + area->x1, src + (size_t)(y - area->y1) * w,
			       (size_t)w * 2);
	}
	bench.flush_us += platform_us() - t0;
	bench.pixels += (uint64_t)w * lv_area_get_height(area);
	bench.last_flush = platform_us();
	if (lv_display_flush_is_last(disp))
		bench.frames++;
	lv_display_flush_ready(disp);
}

static void restore_console(void)
{
	if (tty_fd >= 0) {
		if (tty_kbmode >= 0)
			ioctl(tty_fd, KDSKBMODE, tty_kbmode);
		ioctl(tty_fd, KDSETMODE, KD_TEXT);
		close(tty_fd);
		tty_fd = -1;
	}
}

static void on_signal(int sig)
{
	(void)sig;
	ui_quit(0);
}

static void display_init(void)
{
	const char *path = getenv("WT41N0_UI_FB");
	const char *tty = getenv("WT41N0_UI_TTY");
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	static uint16_t buf1[WT_SCREEN_W * BUF_LINES], buf2[WT_SCREEN_W * BUF_LINES];
	lv_display_t *disp;
	int fd;

	fd = open(path ? path : "/dev/fb0", O_RDWR);
	if (fd < 0 || ioctl(fd, FBIOGET_VSCREENINFO, &var) || ioctl(fd, FBIOGET_FSCREENINFO, &fix)) {
		perror("wt41n0ui: framebuffer");
		exit(1);
	}
	if (var.bits_per_pixel != 16) {
		fprintf(stderr, "wt41n0ui: framebuffer is %u bpp, need 16\n", var.bits_per_pixel);
		exit(1);
	}
	fb_w = var.xres;
	fb_h = var.yres;
	fb_stride = fix.line_length / 2;
	rotated = fb_w == WT_SCREEN_H && fb_h == WT_SCREEN_W;
	if (!rotated && !(fb_w >= WT_SCREEN_W && fb_h >= WT_SCREEN_H)) {
		fprintf(stderr, "wt41n0ui: unexpected framebuffer %ux%u\n", fb_w, fb_h);
		exit(1);
	}
	fb = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (fb == MAP_FAILED) {
		perror("wt41n0ui: mmap");
		exit(1);
	}
	close(fd);

	/* keep the kernel console from drawing over the UI */
	if (!tty || strcmp(tty, "none")) {
		tty_fd = open(tty ? tty : "/dev/tty1", O_RDWR | O_CLOEXEC);
		if (tty_fd >= 0 && ioctl(tty_fd, KDSETMODE, KD_GRAPHICS)) {
			close(tty_fd);
			tty_fd = -1;
		}
		/* and keys typed while an app runs must not pile up on this console */
		if (tty_fd >= 0 && !ioctl(tty_fd, KDGKBMODE, &tty_kbmode))
			ioctl(tty_fd, KDSKBMODE, K_OFF);
	}
	atexit(restore_console);
	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);
	signal(SIGHUP, on_signal);

	disp = lv_display_create(WT_SCREEN_W, WT_SCREEN_H);
	lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
	lv_display_set_flush_cb(disp, flush_cb);
	lv_display_set_buffers(disp, buf1, buf2, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
}

/* ---- input -------------------------------------------------------------------- */

static bool has_bit(const unsigned long *bits, int bit)
{
	return bits[bit / (8 * sizeof(long))] & (1UL << (bit % (8 * sizeof(long))));
}

/* "touch_cal=a b c d e f" in ui.conf, written by the calibration screen */
static void conf_touch(void)
{
	char buf[256];
	double c[6];

	if (conf_get("touch_cal", buf, sizeof(buf)) ||
	    sscanf(buf, "%lf %lf %lf %lf %lf %lf", &c[0], &c[1], &c[2], &c[3], &c[4], &c[5]) != 6)
		return;
	platform_touch_set_cal(c);
}

void platform_touch_set_cal(const double cal[6])
{
	memcpy(touch.cal, cal, sizeof(touch.cal));
	touch.have_cal = true;
}

bool platform_has_touch(void)
{
	return touch.max_x > 0;
}

bool platform_touch_raw(int *x, int *y)
{
	*x = touch.raw_x;
	*y = touch.raw_y;
	return touch.down;
}

void platform_touch_enable(bool on)
{
	touch.enabled = on;
	touch.pressed = false;
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
	(void)indev;
	data->point = touch.point;
	data->state = touch.enabled && touch.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static int scale(int v, int min, int max, int size)
{
	if (max <= min)
		return 0;
	v = (v - min) * size / (max - min);
	return v < 0 ? 0 : v >= size ? size - 1 : v;
}

static int clamp(double v, int size)
{
	return v < 0 ? 0 : v >= size ? size - 1 : (int)v;
}

static void touch_commit(void)
{
	if (touch.have_cal) {
		touch.point.x = clamp(touch.cal[0] * touch.raw_x + touch.cal[1] * touch.raw_y + touch.cal[2], WT_SCREEN_W);
		touch.point.y = clamp(touch.cal[3] * touch.raw_x + touch.cal[4] * touch.raw_y + touch.cal[5], WT_SCREEN_H);
	} else {	/* uncalibrated: plain scaling of the reported range */
		touch.point.x = scale(touch.raw_x, touch.min_x, touch.max_x, WT_SCREEN_W);
		touch.point.y = scale(touch.raw_y, touch.min_y, touch.max_y, WT_SCREEN_H);
	}
	touch.pressed = touch.down;
}

static void input_init(void)
{
	char path[32], name[64];
	unsigned long evbits[1], keybits[KEY_MAX / (8 * sizeof(long)) + 1];
	unsigned long absbits[ABS_MAX / (8 * sizeof(long)) + 1];
	struct input_absinfo ax, ay;
	int i, fd;

	for (i = 0; i < 16 && ndevs < (int)(sizeof(devs) / sizeof(devs[0])); i++) {
		snprintf(path, sizeof(path), "/dev/input/event%d", i);
		fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;
		memset(evbits, 0, sizeof(evbits));
		memset(keybits, 0, sizeof(keybits));
		memset(absbits, 0, sizeof(absbits));
		ioctl(fd, EVIOCGNAME(sizeof(name)), name);
		ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits);
		ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits);
		ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits);

		if (has_bit(evbits, EV_ABS) && has_bit(absbits, ABS_X)) {
			if (getenv("WT41N0_UI_NO_TOUCH") || touch.max_x ||
			    ioctl(fd, EVIOCGABS(ABS_X), &ax) || ioctl(fd, EVIOCGABS(ABS_Y), &ay)) {
				close(fd);
				continue;
			}
			touch.min_x = ax.minimum;
			touch.max_x = ax.maximum;
			touch.min_y = ay.minimum;
			touch.max_y = ay.maximum;
			devs[ndevs].kind = DEV_TOUCH;
		} else if (has_bit(evbits, EV_KEY) &&
			   (has_bit(keybits, KEY_1) || has_bit(keybits, KEY_POWER))) {
			devs[ndevs].kind = DEV_KEYS;
		} else {
			close(fd);
			continue;
		}
		if (grab)
			ioctl(fd, EVIOCGRAB, 1);	/* the console must not see these keys */
		devs[ndevs++].fd = fd;
	}

	if (touch.max_x) {
		touch.indev = lv_indev_create();
		conf_touch();
		lv_indev_set_type(touch.indev, LV_INDEV_TYPE_POINTER);
		lv_indev_set_read_cb(touch.indev, touch_read_cb);
	}
}

static void read_input(int fd, int kind)
{
	struct input_event ev[16];
	ssize_t n;
	int i;

	while ((n = read(fd, ev, sizeof(ev))) > 0) {
		for (i = 0; i < n / (ssize_t)sizeof(ev[0]); i++) {
			if (kind == DEV_KEYS) {
				if (ev[i].type == EV_KEY) {
					keylight_activity();
					keys_event(ev[i].code, ev[i].value);
				}
				continue;
			}
			if (ev[i].type == EV_KEY && ev[i].code == BTN_TOUCH)
				keylight_activity();
			if (ev[i].type == EV_ABS && ev[i].code == ABS_X)
				touch.raw_x = ev[i].value;
			else if (ev[i].type == EV_ABS && ev[i].code == ABS_Y)
				touch.raw_y = ev[i].value;
			else if (ev[i].type == EV_KEY && ev[i].code == BTN_TOUCH)
				touch.down = ev[i].value;
			else if (ev[i].type == EV_SYN && ev[i].code == SYN_REPORT)
				touch_commit();
		}
	}
}

void platform_wait(uint32_t ms)
{
	struct pollfd pfd[8];
	int i;

	for (i = 0; i < ndevs; i++) {
		pfd[i].fd = devs[i].fd;
		pfd[i].events = POLLIN;
	}
	if (ms > 1000)
		ms = 1000;
	keylight_tick();
	if (poll(pfd, ndevs, ms) <= 0)
		return;
	for (i = 0; i < ndevs; i++)
		if (pfd[i].revents & POLLIN)
			read_input(devs[i].fd, devs[i].kind);
}

void platform_init(void)
{
	grab = !getenv("WT41N0_UI_NOGRAB");
	display_init();
	input_init();
}

void platform_exit(void)
{
	int i;

	for (i = 0; i < ndevs; i++)
		close(devs[i].fd);
	ndevs = 0;
	restore_console();
}

/* ---- launcher support --------------------------------------------------------- */

static void drain(void)
{
	struct input_event ev[16];
	int i;

	for (i = 0; i < ndevs; i++)
		while (read(devs[i].fd, ev, sizeof(ev)) > 0)
			;
}

void platform_suspend(void)
{
	int i;

	for (i = 0; i < ndevs; i++)
		ioctl(devs[i].fd, EVIOCGRAB, 0);
	suspended = true;
}

void platform_resume(void)
{
	int i;

	drain();
	for (i = 0; i < ndevs; i++)
		if (grab)
			ioctl(devs[i].fd, EVIOCGRAB, 1);
	if (tty_fd >= 0) {
		ioctl(tty_fd, KDSETMODE, KD_GRAPHICS);
		ioctl(tty_fd, KDSKBMODE, K_OFF);
	}
	touch.pressed = touch.down = false;
	suspended = false;
}

/* While suspended: the next key event from the keypad or power button */
int platform_wait_key(int timeout_ms, unsigned int *code, int *value)
{
	static struct input_event ev[16];
	static int have, next, from;
	struct pollfd pfd[8];
	int i;

	for (;;) {
		while (next < have) {
			struct input_event *e = &ev[next++];

			if (e->type == EV_KEY)		/* a key, or the touch screen pressed */
				keylight_activity();
			if (devs[from].kind == DEV_KEYS && e->type == EV_KEY) {
				*code = e->code;
				*value = e->value;
				return 1;
			}
		}
		have = next = 0;
		keylight_tick();
		for (i = 0; i < ndevs; i++) {
			pfd[i].fd = devs[i].fd;
			pfd[i].events = POLLIN;
		}
		if (poll(pfd, ndevs, timeout_ms) <= 0)
			return 0;
		for (i = 0; i < ndevs && !have; i++) {
			ssize_t n;

			if (!(pfd[i].revents & POLLIN))
				continue;
			n = read(devs[i].fd, ev, sizeof(ev));
			if (n > 0) {
				have = n / (ssize_t)sizeof(ev[0]);
				from = i;
			}
		}
		timeout_ms = 0;		/* events were waiting: no second sleep */
	}
}

/*
 * Switch the active virtual terminal. The kernel ignores switches away from a console
 * in graphics mode, so ours goes back to text mode first (platform_resume() restores
 * graphics mode). Waits up to 2 s for the switch instead of blocking in VT_WAITACTIVE.
 */
int platform_vt_activate(int vt)
{
	int fd = tty_fd >= 0 ? tty_fd : open("/dev/tty0", O_RDWR | O_CLOEXEC);
	struct vt_stat st;
	int i, ret = -1;

	if (fd < 0)
		return -1;
	if (tty_fd >= 0 && !ioctl(fd, VT_GETSTATE, &st) && st.v_active != vt)
		ioctl(fd, KDSETMODE, KD_TEXT);
	if (!ioctl(fd, VT_ACTIVATE, vt)) {
		for (i = 0; i < 100; i++) {
			if (!ioctl(fd, VT_GETSTATE, &st) && st.v_active == vt) {
				ret = 0;
				break;
			}
			usleep(20000);
		}
	}
	if (fd != tty_fd)
		close(fd);
	return ret;
}

/* ---- system state ------------------------------------------------------------- */

static int read_line(const char *path, char *buf, size_t len)
{
	FILE *f = fopen(path, "r");
	int ok;

	if (!f)
		return -1;
	ok = fgets(buf, (int)len, f) != NULL;
	fclose(f);
	if (!ok)
		return -1;
	buf[strcspn(buf, "\n")] = 0;
	return 0;
}

int platform_battery(bool *charging)
{
	char buf[32];

	if (charging) {
		*charging = false;
		if (!read_line("/sys/class/power_supply/wt41n0-battery/status", buf, sizeof(buf)))
			*charging = !strcmp(buf, "Charging") || !strcmp(buf, "Full");
	}
	if (read_line("/sys/class/power_supply/wt41n0-battery/capacity", buf, sizeof(buf)))
		return -1;
	return atoi(buf);
}

/* Link quality of wlan0 from /proc/net/wireless (0..70), as a percentage */
int platform_wifi(void)
{
	char line[256];
	float quality;
	FILE *f = fopen("/proc/net/wireless", "r");
	int ret = -1;

	if (!f)
		return -1;
	while (fgets(line, sizeof(line), f))
		if (sscanf(line, " wlan0: %*x %f", &quality) == 1)
			ret = (int)(quality * 100 / 70);
	fclose(f);
	return ret > 100 ? 100 : ret;
}
