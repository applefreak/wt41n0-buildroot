/* Settings app: shared between its screens */
#ifndef SETTINGS_H
#define SETTINGS_H

#include "wt41n0ui.h"

void settings_menu(int focus);		/* the main list; focus = row to select */
void wifi_open(void (*back)(void));	/* Wi-Fi networks; back() when leaving */

/* System controls (sys.c) */
int sys_brightness(int *max);
void sys_set_brightness(int value);
int sys_volume(void);			/* 0..100 */
void sys_set_volume(int percent);

/* a small text label in a list */
lv_obj_t *settings_note(lv_obj_t *list, const char *text);

#endif
