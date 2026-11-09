/* Host harness controls (platform_host.c) */
#ifndef PLATFORM_HOST_H
#define PLATFORM_HOST_H
#include <stdint.h>
void host_advance(uint32_t ms);		/* run timers and animations for ms of simulated time */
void host_key(unsigned int code);
void keys_press_only(unsigned int code);	/* the same, without letting animations finish */	/* press and release a key (Linux input code) */
int host_shot(const char *path);	/* write the screen as a 24-bit BMP */
void host_touch(int x, int y, uint32_t ms);	/* press the simulated panel at a screen point */
int host_touch_map(int rx, int ry, int *x, int *y);	/* apply the stored calibration */
#endif
