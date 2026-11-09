/*
 * Keypad backlight (TWL6030 PWM1, /sys/class/leds/keypad-backlight), from the settings in
 * /etc/wt41n0/ui.conf:
 *   keylight_mode=auto|on|off   auto: on at any key or touch, off after the timeout
 *   keylight=0..100             brightness when on
 *   keylight_timeout=SECONDS    for auto
 * Every UI process drives it from the input it reads; the launcher reads all keys, also
 * while an app or the Terminal runs. The LED core turns it off while suspended.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_internal.h"

#define LED "/sys/class/leds/keypad-backlight/brightness"

static struct {
	enum ui_keylight_mode mode;
	int level, timeout_ms;
	bool on;
	uint32_t last;		/* last input, platform_tick() ms */
} kl = { UI_KEYLIGHT_AUTO, 50, 10000, false, 0 };

static void led_write(int v)
{
#ifndef WT41N0_UI_HOST
	FILE *f = fopen(LED, "w");

	if (f) {
		fprintf(f, "%d\n", v);
		fclose(f);
	}
#else
	(void)v;
#endif
}

static void set_on(bool on)
{
	kl.on = on;
	led_write(on ? kl.level : 0);
}

void keylight_load(void)
{
	char v[16];

	kl.mode = UI_KEYLIGHT_AUTO;
	kl.level = 50;
	kl.timeout_ms = 10000;
	if (!conf_get("keylight_mode", v, sizeof(v)))
		kl.mode = !strcmp(v, "on") ? UI_KEYLIGHT_ON : !strcmp(v, "off") ? UI_KEYLIGHT_OFF : UI_KEYLIGHT_AUTO;
	if (!conf_get("keylight", v, sizeof(v)))
		kl.level = atoi(v) < 0 ? 0 : atoi(v) > 100 ? 100 : atoi(v);
	if (!conf_get("keylight_timeout", v, sizeof(v)) && atoi(v) > 0)
		kl.timeout_ms = atoi(v) * 1000;
}

/* the settings as they are now, applied at once (auto counts as activity) */
void ui_keylight_apply(void)
{
	keylight_load();
	kl.last = platform_tick();
	set_on(kl.mode != UI_KEYLIGHT_OFF && kl.level > 0);
}

void keylight_activity(void)
{
	kl.last = platform_tick();
	if (kl.mode == UI_KEYLIGHT_AUTO && !kl.on && kl.level > 0)
		set_on(true);
}

void keylight_tick(void)
{
	if (kl.mode == UI_KEYLIGHT_AUTO && kl.on && platform_tick() - kl.last >= (uint32_t)kl.timeout_ms)
		set_on(false);
}

enum ui_keylight_mode ui_keylight_mode(void) { return kl.mode; }
int ui_keylight_level(void) { return kl.level; }
int ui_keylight_timeout(void) { return kl.timeout_ms / 1000; }
