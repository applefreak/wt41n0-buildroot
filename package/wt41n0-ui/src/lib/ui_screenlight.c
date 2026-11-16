/*
 * Screen backlight: dimmed, then switched off, after a time without a key or a touch;
 * the next key or touch brings it back. From the settings in /etc/wt41n0/ui.conf:
 *   brightness=N         the backlight level when in use (Settings, Display)
 *   screen_dim=SECONDS   dim after this long (0: never; default 30)
 *   screen_off=SECONDS   off after this long (0: never; default 120)
 * While the screen is off, the key or touch that wakes it does nothing else, so
 * nothing happens that cannot be seen.
 *
 * Every UI process sees only the input it reads (an app grabs the keypad and touch
 * screen; the launcher sees them only while it or the Terminal is in front), so they
 * share the time of the last input through /run/wt41n0-ui-active's mtime: whichever
 * process saw it, all of them count from it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include "ui_internal.h"

#define BACKLIGHT	"/sys/class/backlight/backlight/"
#define ACTIVE		"/run/wt41n0-ui-active"

enum state { AWAKE, DIMMED, DARK };

static struct {
	int dim_s, off_s;	/* 0: never */
	int level, max;		/* backlight levels */
	enum state state;
	time_t last, stamped;	/* the last input seen here; when the shared file was last touched */
	long long conf_mtime;	/* ui.conf as last loaded (ns): another process may change it */
} sl = { 30, 120, 0, 0, AWAKE, 0, 0, 0 };

#ifndef WT41N0_UI_HOST
static int read_int(const char *path)
{
	FILE *f = fopen(path, "r");
	int v = -1;

	if (f) {
		if (fscanf(f, "%d", &v) != 1)
			v = -1;
		fclose(f);
	}
	return v;
}

static void bl_write(int v)
{
	FILE *f = fopen(BACKLIGHT "brightness", "w");

	if (f) {
		fprintf(f, "%d\n", v);
		fclose(f);
	}
}

static long long mtime_ns(const char *path)
{
	struct stat st;

	return stat(path, &st) ? 0 : st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
}

static time_t shared_last(void)
{
	struct stat st;

	return stat(ACTIVE, &st) ? 0 : st.st_mtime;
}

static void shared_stamp(time_t now)
{
	int fd;

	if (now == sl.stamped)			/* at most once a second */
		return;
	sl.stamped = now;
	fd = open(ACTIVE, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
	if (fd >= 0) {
		futimens(fd, NULL);		/* mtime = now */
		close(fd);
	}
}
#else
static int read_int(const char *path) { (void)path; return -1; }
static void bl_write(int v) { (void)v; }
static long long mtime_ns(const char *path) { (void)path; return 0; }
static time_t shared_last(void) { return 0; }
static void shared_stamp(time_t now) { (void)now; }
#endif

static void set_state(enum state s)
{
	int dim = sl.level / 4 > 0 ? sl.level / 4 : 1;	/* a quarter, at least the lowest step */

	if (s == sl.state || sl.level <= 0)
		return;
	sl.state = s;
	bl_write(s == DARK ? 0 : s == DIMMED ? dim : sl.level);
}

void screenlight_load(void)
{
	char v[16];
	int cur;

	sl.conf_mtime = mtime_ns(platform_conf_path());
	sl.dim_s = 30;
	sl.off_s = 120;
	if (!conf_get("screen_dim", v, sizeof(v)))
		sl.dim_s = atoi(v) > 0 ? atoi(v) : 0;
	if (!conf_get("screen_off", v, sizeof(v)))
		sl.off_s = atoi(v) > 0 ? atoi(v) : 0;
	sl.max = read_int(BACKLIGHT "max_brightness");
	sl.level = !conf_get("brightness", v, sizeof(v)) ? atoi(v) : 0;
	if (sl.level <= 0) {			/* never set in Settings: what S30 or the panel has */
		cur = read_int(BACKLIGHT "brightness");
		sl.level = cur > 0 ? cur : sl.max;
	}
	if (sl.max > 0 && sl.level > sl.max)
		sl.level = sl.max;
}

/* settings changed (or a UI process starting): reload, count as input, full brightness */
void ui_screenlight_apply(void)
{
	time_t now = time(NULL);

	screenlight_load();
	sl.last = now;
	shared_stamp(now);
	sl.state = DIMMED;			/* forces the write */
	set_state(AWAKE);
}

bool screenlight_activity(void)
{
	time_t now = time(NULL);
	bool was_dark = sl.state == DARK;

	sl.last = now;
	shared_stamp(now);
	set_state(AWAKE);
	return was_dark;
}

void screenlight_tick(void)
{
	time_t now = time(NULL), last = sl.last, shared = shared_last();
	long idle;

	if (mtime_ns(platform_conf_path()) != sl.conf_mtime) {
		screenlight_load();		/* Settings changed the level or the times */
		if (sl.state == AWAKE)
			bl_write(sl.level);
	}
	if (shared > last)			/* input seen by another UI process */
		last = shared;
	idle = (long)(now - last);
	if (sl.off_s && idle >= sl.off_s)
		set_state(DARK);
	else if (sl.dim_s && idle >= sl.dim_s)
		set_state(DIMMED);
	else
		set_state(AWAKE);
}

bool ui_screen_dark(void) { return sl.state == DARK; }
int ui_screenlight_dim(void) { return sl.dim_s; }
int ui_screenlight_off(void) { return sl.off_s; }
