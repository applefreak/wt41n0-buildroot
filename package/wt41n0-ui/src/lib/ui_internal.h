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
