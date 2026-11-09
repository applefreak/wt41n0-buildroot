/* Launcher apps: manifests and starting them */
#ifndef LAUNCHER_APPS_H
#define LAUNCHER_APPS_H

#include <sys/types.h>
#include <time.h>

#define MAX_APPS 24

enum app_type { APP_LVGL, APP_CONSOLE };

struct app {
	char id[16];		/* Id=: selects the built-in readout (default: the icon name) */
	char name[24];
	char icon[96];		/* icon-font name (see apps.c) or a PNG path */
	char exec[192];
	enum app_type type;
	int order;
	pid_t pid;		/* console apps keep running between visits */
	time_t started;		/* when the console app was started */
	char status_cmd[160];	/* Status=: prints "meta|line|level" for the readout */
	time_t status_at;	/* last Status= run, and its result */
	char status_meta[12], status_line[48];
	int status_level;
};

int apps_load(struct app *apps, int max);
const char *apps_icon_symbol(const char *icon);	/* LV_SYMBOL_* or NULL for a file */
int apps_launch(struct app *app);	/* -1: could not be started */

#endif
