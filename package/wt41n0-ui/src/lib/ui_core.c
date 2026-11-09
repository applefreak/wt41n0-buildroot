/* wt41n0ui core: setup, main loop, screens with status bar and key hints. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "ui_internal.h"

static lv_group_t *group;
#ifndef WT41N0_UI_HOST
static uint64_t init_us, init_done_us;
#endif
static bool running;
static int exit_code;


/* LVGL log to stderr. TinyTTF reports every glyph it hands over to the symbol
 * fallback font as "cache not allocated"; that is not an error here. */
static void log_cb(lv_log_level_t level, const char *buf)
{
	(void)level;
	if (!strstr(buf, "cache not allocated"))
		fputs(buf, stderr);
}

/* Time zone chosen in Settings: "tz=" holds a POSIX TZ string */
static void load_tz(void)
{
	char tz[64];

	if (!conf_get("tz", tz, sizeof(tz)) && tz[0])
		setenv("TZ", tz, 1);
	else
		unsetenv("TZ");
	tzset();
}

void ui_init(void)
{
	static bool done;

	if (done)		/* the host harness runs several apps in one process */
		return;
	done = true;
#ifndef WT41N0_UI_HOST
	init_us = platform_us();
#endif
	lv_init();
	lv_log_register_print_cb(log_cb);
	lv_tick_set_cb(platform_tick);
	platform_init();
	theme_init();
	load_tz();
	fonts_init();
	group = lv_group_create();
	lv_group_set_default(group);
	lv_group_set_wrap(group, true);
	keys_init(group);
	focus_init(group);
	ui_keylight_apply();
	if (!getenv("WT41N0_UI_PERF"))
		lv_sysmon_hide_performance(NULL);
#ifndef WT41N0_UI_HOST
	init_done_us = platform_us();
#endif
}

#ifndef WT41N0_UI_HOST
/*
 * WT41N0_UI_BENCH=<keys>: play keys (D down, U up, E enter, X esc, 1-9 digits) one at
 * a time and print, per key, how long until the screen stopped changing and how the
 * time split between drawing and copying to the framebuffer; then exit.
 */
static void run_for(uint64_t us, uint64_t *busy)
{
	uint64_t end = platform_us() + us;

	while (platform_us() < end) {
		uint64_t t = platform_us();

		lv_timer_handler();
		*busy += platform_us() - t;
		usleep(1000);
	}
}

static int bench_run(const char *script)
{
	static const struct { char c; unsigned int code; } keymap[] = {
		{ 'D', 108 }, { 'U', 103 }, { 'E', 28 }, { 'X', 1 }, { '1', 2 }, { '2', 3 }, { '3', 4 },
		{ '4', 5 }, { '5', 6 }, { '6', 7 }, { '7', 8 }, { '8', 9 }, { '9', 10 },
	};
	uint64_t busy = 0, run_us = platform_us();
	unsigned int i;
	FILE *f = fopen("/proc/self/stat", "r");
	unsigned long long start_ticks = 0;
	double up = 0;

	if (f) {	/* process start, in clock ticks since boot (field 22) */
		if (fscanf(f, "%*d (%*[^)]) %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %*u %*u %*d %*d %*d %*d %*d %*d %llu",
			   &start_ticks) != 1)
			start_ticks = 0;
		fclose(f);
	}
	f = fopen("/proc/uptime", "r");
	if (f) {
		if (fscanf(f, "%lf", &up) != 1)
			up = 0;
		fclose(f);
	}
	fprintf(stderr, "bench: exec->main ~%.0f ms (uptime %.2f s, start %.2f s), ui_init %.0f ms, screen build %.0f ms\n",
		up * 1000 - start_ticks * 1000.0 / sysconf(_SC_CLK_TCK) - (run_us - init_us) / 1000.0,
		up, start_ticks / (double)sysconf(_SC_CLK_TCK),
		(init_done_us - init_us) / 1000.0, (run_us - init_done_us) / 1000.0);
	run_for(800000, &busy);
	fprintf(stderr, "bench: first frame %.0f ms after start, %u frames, busy %.0f ms (flush %.0f ms)\n",
		(bench.last_flush - init_us) / 1000.0, bench.frames, busy / 1000.0, bench.flush_us / 1000.0);
	for (; *script; script++) {
		unsigned int code = 0;
		uint64_t t0;

		for (i = 0; i < sizeof(keymap) / sizeof(keymap[0]); i++)
			if (keymap[i].c == *script)
				code = keymap[i].code;
		if (!code)
			continue;
		memset(&bench, 0, sizeof(bench));
		busy = 0;
		t0 = platform_us();
		keys_event(code, 1);
		keys_event(code, 0);
		run_for(700000, &busy);
		fprintf(stderr, "bench: %c settled %5.1f ms  frames %2u  busy %5.1f ms  flush %5.1f ms  %6llu px\n",
			*script, bench.last_flush > t0 ? (bench.last_flush - t0) / 1000.0 : 0.0, bench.frames,
			busy / 1000.0, bench.flush_us / 1000.0, (unsigned long long)bench.pixels);
		if (!running)
			break;
	}
	platform_exit();
	return 0;
}
#endif

int ui_run(void)
{
#ifdef WT41N0_UI_HOST
	return 0;		/* the harness drives the timers itself */
#else
	running = true;
	if (getenv("WT41N0_UI_BENCH"))
		return bench_run(getenv("WT41N0_UI_BENCH"));
#endif
	while (running)
		platform_wait(lv_timer_handler());
	platform_exit();
	return exit_code;
}

void ui_quit(int code)
{
	exit_code = code;
	running = false;
}

lv_group_t *ui_group(void)
{
	return group;
}

void ui_suspend(void)
{
	platform_suspend();
}

void ui_resume(void)
{
	/* an app may have changed the settings */
	theme_init();
	load_tz();
	platform_resume();
	keylight_load();
	lv_obj_invalidate(lv_screen_active());
	lv_refr_now(NULL);
}

int ui_wait_key(int timeout_ms, unsigned int *code, int *value)
{
	return platform_wait_key(timeout_ms, code, value);
}

int ui_console_activate(int vt)
{
	return platform_vt_activate(vt);
}

int ui_battery_percent(bool *charging)
{
	return platform_battery(charging);
}

int ui_wifi_quality(void)
{
	return platform_wifi();
}

