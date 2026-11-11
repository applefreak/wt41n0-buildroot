/* wt41n0ui internals: shared between the core, the theme, the keys and the platforms. */
#ifndef UI_INTERNAL_H
#define UI_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "wt41n0ui.h"

/* Platform layer: platform_linux.c on the unit, platform_host.c for the screenshot
 * harness. platform_init() creates the display and the pointer input (if any). */
void platform_init(void);
uint32_t platform_tick(void);			/* milliseconds */
void platform_wait(uint32_t ms);		/* sleep until input or timeout, then read input */
void platform_exit(void);
int platform_battery(bool *charging);
int platform_wifi(void);
const char *platform_conf_path(void);

/* Touch panel: raw access for the calibration, and the calibration itself
 * (screen = cal[0] * raw_x + cal[1] * raw_y + cal[2], cal[3..5] likewise for y) */
bool platform_has_touch(void);
bool platform_touch_raw(int *x, int *y);	/* true while pressed */
void platform_touch_enable(bool on);		/* deliver touches to LVGL */
void platform_touch_set_cal(const double cal[6]);

/* Launcher support */
void platform_suspend(void);
void platform_resume(void);
int platform_wait_key(int timeout_ms, unsigned int *code, int *value);
int platform_vt_activate(int vt);

/* Keypad backlight (ui_keylight.c): input activity and the auto-off check */
void keylight_load(void);
void keylight_activity(void);
void keylight_tick(void);

/* Benchmark counters (platform_linux.c), for WT41N0_UI_BENCH */
struct bench_stats {
	uint64_t flush_us, pixels, last_flush;
	unsigned int frames;
};
extern struct bench_stats bench;
uint64_t platform_us(void);

/* Style hooks (ui_style.c) */
void style_ring(lv_obj_t *ring, lv_obj_t *target, const lv_color_t *override);
int style_ring_inset(void);

/* Focus ring and screen transitions (ui_focus.c) */
void focus_init(lv_group_t *g);
void focus_ring_create(lv_obj_t *scr);
const char *icon_symbol(const char *name);	/* ui_icons.c: a symbol icon's glyph, NULL if drawn */
void focus_ring_follow(lv_obj_t *scroller);	/* a list scrolled: the ring stays on its row */
lv_screen_load_anim_t focus_take_transition(void);

void ui_hint_save(char *pairs, size_t plen, char *marker, size_t mlen);

/* Esc handler (ui_keys.c) */
void keys_set_back(void (*cb)(void));

/* Settings file (ui_conf.c) */
int conf_get(const char *key, char *buf, size_t len);
int conf_set(const char *key, const char *value);

/* Keys: the platform feeds raw key events (Linux input codes); ui_keys.c applies the
 * layers and queues LVGL keys for the keypad input device. value: 0 release, 1 press,
 * 2 autorepeat. */
void keys_init(lv_group_t *group);
void keys_event(unsigned int code, int value);

/* Theme and fonts */
void theme_init(void);
void fonts_init(void);

/* Status bar refresh (ui_core.c) */
void status_refresh(void);

#endif
