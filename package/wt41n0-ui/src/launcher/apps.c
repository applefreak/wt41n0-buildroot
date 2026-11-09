/*
 * App manifests and launching.
 *
 * Manifests: *.app files in /usr/share/wt41n0/apps (built in) and /etc/wt41n0/apps
 * (added on the unit), or the colon-separated directories in WT41N0_UI_APPS:
 *
 *	Name=Scanner
 *	Icon=eye		an icon-font name (list below) or a 28x28 PNG path
 *	Exec=/usr/bin/wt41n0-scanner-app
 *	Type=lvgl		lvgl: full-screen graphical app; console: text app on tty2
 *	Order=20		position on the home screen (low first)
 *	Id=scanner		built-in live readout to use (default: the icon name)
 *	Status=/usr/bin/cmd	for other apps: prints "meta|line|level" (level 0-8 or -1),
 *				shown on the tile and in the readout strip
 *
 * While an app runs the launcher steps aside. Holding Esc for 1.5 s brings the
 * launcher back: a console app keeps running on tty2 (and is returned to next time),
 * a graphical app is ended (SIGTERM, then SIGKILL after 2 s).
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "wt41n0ui.h"
#include "apps.h"

#define KEY_ESC		1
#define HOLD_MS		1500
#define KILL_GRACE_MS	2000
#define CONSOLE_VT	2

static const struct { const char *name, *symbol; } icons[] = {
	{ "terminal", LV_SYMBOL_KEYBOARD }, { "keyboard", LV_SYMBOL_KEYBOARD },
	{ "power", LV_SYMBOL_POWER }, { "settings", LV_SYMBOL_SETTINGS },
	{ "wifi", LV_SYMBOL_WIFI }, { "list", LV_SYMBOL_LIST }, { "eye", LV_SYMBOL_EYE_OPEN },
	{ "home", LV_SYMBOL_HOME }, { "image", LV_SYMBOL_IMAGE }, { "audio", LV_SYMBOL_AUDIO },
	{ "video", LV_SYMBOL_VIDEO }, { "bluetooth", LV_SYMBOL_BLUETOOTH },
	{ "usb", LV_SYMBOL_USB }, { "battery", LV_SYMBOL_BATTERY_FULL },
	{ "file", LV_SYMBOL_FILE }, { "folder", LV_SYMBOL_DIRECTORY }, { "edit", LV_SYMBOL_EDIT },
	{ "bell", LV_SYMBOL_BELL }, { "call", LV_SYMBOL_CALL }, { "mail", LV_SYMBOL_ENVELOPE },
	{ "gps", LV_SYMBOL_GPS }, { "refresh", LV_SYMBOL_REFRESH }, { "play", LV_SYMBOL_PLAY },
	{ "download", LV_SYMBOL_DOWNLOAD }, { "upload", LV_SYMBOL_UPLOAD },
	{ "warning", LV_SYMBOL_WARNING }, { "ok", LV_SYMBOL_OK }, { "drive", LV_SYMBOL_DRIVE },
	{ "sd", LV_SYMBOL_SD_CARD }, { "charge", LV_SYMBOL_CHARGE }, { "tint", LV_SYMBOL_TINT },
};

const char *apps_icon_symbol(const char *icon)
{
	unsigned int i;

	for (i = 0; i < sizeof(icons) / sizeof(icons[0]); i++)
		if (!strcmp(icons[i].name, icon))
			return icons[i].symbol;
	return icon[0] == '/' ? NULL : LV_SYMBOL_FILE;
}

static void trim(char *s)
{
	size_t n = strlen(s);

	while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' '))
		s[--n] = 0;
}

static int load_one(const char *path, struct app *a)
{
	char line[256];
	FILE *f = fopen(path, "r");

	if (!f)
		return -1;
	memset(a, 0, sizeof(*a));
	a->order = 50;
	snprintf(a->icon, sizeof(a->icon), "file");
	while (fgets(line, sizeof(line), f)) {
		char *v = strchr(line, '=');

		if (line[0] == '#' || !v)
			continue;
		*v++ = 0;
		trim(v);
		if (!strcmp(line, "Name"))
			snprintf(a->name, sizeof(a->name), "%s", v);
		else if (!strcmp(line, "Icon"))
			snprintf(a->icon, sizeof(a->icon), "%s", v);
		else if (!strcmp(line, "Exec"))
			snprintf(a->exec, sizeof(a->exec), "%s", v);
		else if (!strcmp(line, "Type"))
			a->type = !strcmp(v, "console") ? APP_CONSOLE : APP_LVGL;
		else if (!strcmp(line, "Order"))
			a->order = atoi(v);
		else if (!strcmp(line, "Id"))
			snprintf(a->id, sizeof(a->id), "%s", v);
		else if (!strcmp(line, "Status"))
			snprintf(a->status_cmd, sizeof(a->status_cmd), "%s", v);
	}
	fclose(f);
	if (!a->id[0])
		snprintf(a->id, sizeof(a->id), "%s", a->icon);
	return a->name[0] && a->exec[0] ? 0 : -1;
}

static int by_order(const void *x, const void *y)
{
	const struct app *a = x, *b = y;

	return a->order != b->order ? a->order - b->order : strcmp(a->name, b->name);
}

int apps_load(struct app *apps, int max)
{
	const char *env = getenv("WT41N0_UI_APPS");
	char dirs[512], *dir, *save = NULL, path[512];
	int n = 0, i;

	snprintf(dirs, sizeof(dirs), "%s", env ? env : "/usr/share/wt41n0/apps:/etc/wt41n0/apps");
	for (dir = strtok_r(dirs, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
		DIR *d = opendir(dir);
		struct dirent *e;

		if (!d)
			continue;
		while ((e = readdir(d)) && n < max) {
			size_t len = strlen(e->d_name);

			if (len < 5 || strcmp(e->d_name + len - 4, ".app"))
				continue;
			snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
			if (load_one(path, &apps[n]))
				continue;
			/* a manifest in /etc replaces a built-in one of the same name */
			for (i = 0; i < n && strcmp(apps[i].name, apps[n].name); i++)
				;
			if (i < n)
				apps[i] = apps[n];
			else
				n++;
		}
		closedir(d);
	}
	qsort(apps, n, sizeof(apps[0]), by_order);
	return n;
}

static long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000;
}

/* Exec line split on spaces (no quoting) */
static void exec_app(const char *cmd)
{
	char buf[192], *argv[16], *save = NULL;
	int n = 0;

	snprintf(buf, sizeof(buf), "%s", cmd);
	for (argv[n] = strtok_r(buf, " ", &save); argv[n] && n < 15; argv[++n] = strtok_r(NULL, " ", &save))
		;
	argv[n] = NULL;
	execvp(argv[0], argv);
	fprintf(stderr, "wt41n0-launcher: %s: %s\n", argv[0], strerror(errno));
	_exit(127);
}

static pid_t start_console(const char *cmd)
{
	pid_t pid = fork();
	char tty[16];
	int fd;

	if (pid)
		return pid;
	setsid();
	snprintf(tty, sizeof(tty), "/dev/tty%d", CONSOLE_VT);
	fd = open(tty, O_RDWR);
	if (fd < 0)
		_exit(127);
	ioctl(fd, TIOCSCTTY, 1);
	dup2(fd, 0);
	dup2(fd, 1);
	dup2(fd, 2);
	if (fd > 2)
		close(fd);
	/* clear the console, then a reminder of the way back */
	fputs("\033[2J\033[H(hold ESC to return to the launcher)\n", stdout);
	fflush(stdout);
	exec_app(cmd);
	return 0;
}

static pid_t start_lvgl(const char *cmd)
{
	pid_t pid = fork();

	if (pid)
		return pid;
	setenv("WT41N0_UI_TTY", "none", 1);	/* the launcher owns the console */
	setenv("WT41N0_UI_NOGRAB", "1", 1);	/* and keeps listening for the home combination */
	exec_app(cmd);
	return 0;
}

static int exit_status;		/* of the last app that ended */

static bool alive(pid_t pid)
{
	int status;
	pid_t r;

	if (pid <= 0)
		return false;
	r = waitpid(pid, &status, WNOHANG);
	if (r == pid)
		exit_status = status;
	return r == 0;
}

/* Wait while the app runs; returns when it exits or Esc is held */
static bool wait_app(pid_t pid)
{
	long esc_down = 0;
	unsigned int code;
	int value;

	for (;;) {
		if (!alive(pid))
			return false;			/* the app ended */
		if (ui_wait_key(100, &code, &value) && code == KEY_ESC)
			esc_down = value ? (esc_down ? esc_down : now_ms()) : 0;
		if (esc_down && now_ms() - esc_down >= HOLD_MS)
			return true;			/* home combination */
	}
}

static void end_app(pid_t pid)
{
	long start = now_ms();

	kill(pid, SIGTERM);
	while (alive(pid)) {
		if (now_ms() - start > KILL_GRACE_MS) {
			kill(pid, SIGKILL);
			waitpid(pid, NULL, 0);
			break;
		}
		usleep(50000);
	}
}

/* Returns -1 when the app could not be started (exec failed: exit code 127) */
int apps_launch(struct app *app)
{
	int ret = 0;

#ifdef WT41N0_UI_HOST
	printf("launch: %s (%s)\n", app->name, app->exec);	/* the harness runs nothing */
	return 0;
#endif
	signal(SIGCHLD, SIG_DFL);
	exit_status = 0;
	ui_suspend();
	if (app->type == APP_CONSOLE) {
		if (!alive(app->pid)) {
			app->pid = start_console(app->exec);
			app->started = time(NULL);
		}
		if (ui_console_activate(CONSOLE_VT) == 0)
			wait_app(app->pid);	/* the console app stays for next time */
		else
			ret = -1;
		ui_console_activate(1);
	} else {
		pid_t pid = start_lvgl(app->exec);

		if (pid > 0 && wait_app(pid))
			end_app(pid);
		else if (pid < 0 || (WIFEXITED(exit_status) && WEXITSTATUS(exit_status) == 127))
			ret = -1;
	}
	ui_resume();
	return ret;
}
