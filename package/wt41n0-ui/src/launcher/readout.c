/*
 * Live data for the home screen. Built-in readouts by app Id (wifi, power, terminal,
 * settings, scanner, system); any other app can give a Status= command in its manifest
 * that prints "meta|line|level" (run at most every 5 s, given 1 s).
 */
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "wt41n0ui.h"
#include "readout.h"
#include "../apps/settings/wpa.h"

#define STATUS_EVERY_S	5
#define STATUS_WAIT_MS	1000

static void upper(char *s)
{
	for (; *s; s++)
		if (*s >= 'a' && *s <= 'z')
			*s -= 32;
}

#ifndef WT41N0_UI_HOST
/* signal level of wlan0 in dBm, from /proc/net/wireless */
static int wifi_dbm(void)
{
	char line[256];
	float quality, level;
	FILE *f = fopen("/proc/net/wireless", "r");
	int ret = 0;

	if (!f)
		return 0;
	while (fgets(line, sizeof(line), f))
		if (sscanf(line, " wlan0: %*x %f %f", &quality, &level) == 2)
			ret = (int)level;
	fclose(f);
	return ret;
}

static void builtin_wifi(struct readout *r)
{
	char ssid[64] = "", state[32] = "", ip[48] = "";
	int q = ui_wifi_quality();

	if (wpa_status(ssid, sizeof(ssid), state, sizeof(state), ip, sizeof(ip)) || strcmp(state, "COMPLETED")) {
		snprintf(r->line, sizeof(r->line), "NOT CONNECTED");
		snprintf(r->meta, sizeof(r->meta), "OFF");
		return;
	}
	snprintf(r->line, sizeof(r->line), "%s  %s", ssid, ip[0] ? ip : "NO ADDRESS");
	snprintf(r->meta, sizeof(r->meta), "%d", wifi_dbm());
	r->level = q < 0 ? 0 : q * 8 / 100;
}

static void builtin_system(struct readout *r)
{
	char line[128];
	double load = 0;
	long total = 0, avail = 0, v;
	FILE *f = fopen("/proc/loadavg", "r");

	if (f) {
		if (fscanf(f, "%lf", &load) != 1)
			load = 0;
		fclose(f);
	}
	f = fopen("/proc/meminfo", "r");
	if (f) {
		while (fgets(line, sizeof(line), f)) {
			if (sscanf(line, "MemTotal: %ld", &v) == 1)
				total = v;
			else if (sscanf(line, "MemAvailable: %ld", &v) == 1)
				avail = v;
		}
		fclose(f);
	}
	snprintf(r->line, sizeof(r->line), "LOAD %.2f  MEM %ld%%", load,
		 total ? (total - avail) * 100 / total : 0);
	r->level = load >= 1 ? 8 : (int)(load * 8);
}

/* Status= command: first output line, within STATUS_WAIT_MS */
static void run_status(struct app *app)
{
	char out[128] = "", *meta, *line, *level, *save = NULL;
	int fd[2], n = 0, status;
	pid_t pid;

	app->status_at = time(NULL);
	if (pipe(fd))
		return;
	pid = fork();
	if (pid == 0) {
		dup2(fd[1], 1);
		close(fd[0]);
		close(fd[1]);
		execl("/bin/sh", "sh", "-c", app->status_cmd, (char *)NULL);
		_exit(127);
	}
	close(fd[1]);
	if (pid > 0) {
		struct pollfd pfd = { .fd = fd[0], .events = POLLIN };

		if (poll(&pfd, 1, STATUS_WAIT_MS) > 0)
			n = (int)read(fd[0], out, sizeof(out) - 1);
		kill(pid, SIGKILL);
		waitpid(pid, &status, 0);
	}
	close(fd[0]);
	out[n > 0 ? n : 0] = 0;
	out[strcspn(out, "\n")] = 0;
	meta = strtok_r(out, "|", &save);
	line = strtok_r(NULL, "|", &save);
	level = strtok_r(NULL, "|", &save);
	snprintf(app->status_meta, sizeof(app->status_meta), "%s", meta ? meta : "");
	snprintf(app->status_line, sizeof(app->status_line), "%s", line ? line : "");
	app->status_level = level ? atoi(level) : -1;
}

/* the scanner app's history (/tmp/wt41n0-scans): the count and the last code */
static void builtin_scanner(struct readout *r)
{
	FILE *f = fopen("/tmp/wt41n0-scans", "r");
	char line[320], last[320] = "";
	int n = 0;

	if (f) {
		while (fgets(line, sizeof(line), f)) {
			char *code = strchr(line, '\t');

			if (code && (code = strchr(code + 1, '\t'))) {
				snprintf(last, sizeof(last), "%s", code + 1);
				n++;
			}
		}
		fclose(f);
	}
	last[strcspn(last, "\n")] = 0;
	if (n) {
		snprintf(r->meta, sizeof(r->meta), "%d", n);
		snprintf(r->line, sizeof(r->line), "LAST %.40s", last);
	} else {
		snprintf(r->line, sizeof(r->line), "RING SCANNER  NO SCANS YET");
	}
}
#endif

void readout_get(struct app *app, struct readout *r)
{
	memset(r, 0, sizeof(*r));
	r->level = -1;
#ifdef WT41N0_UI_HOST
	/* the harness: the values of the reference design */
	if (!strcmp(app->id, "wifi")) {
		snprintf(r->meta, sizeof(r->meta), "-58");
		snprintf(r->line, sizeof(r->line), "HOMENET-5G  10.0.0.42");
		r->level = 6;
	} else if (!strcmp(app->id, "power")) {
		snprintf(r->meta, sizeof(r->meta), "87%%");
		snprintf(r->line, sizeof(r->line), "ON BATTERY  87%%");
		r->level = 7;
	} else if (!strcmp(app->id, "terminal")) {
		snprintf(r->line, sizeof(r->line), "TTY2 /BIN/SH UP 02:14");
		r->level = 2;
	} else if (!strcmp(app->id, "settings")) {
		snprintf(r->line, sizeof(r->line), "%s %s/%s", ui_theme_style_name(), ui_theme_palette(), ui_theme_accent());
		upper(r->line);
	} else if (!strcmp(app->id, "scanner")) {
		snprintf(r->meta, sizeof(r->meta), "N/C");
		snprintf(r->line, sizeof(r->line), "RING SCANNER  NOT CONNECTED");
	} else if (!strcmp(app->id, "system")) {
		snprintf(r->line, sizeof(r->line), "LOAD 0.12  MEM 23%%");
		r->level = 1;
	}
	return;
#else
	if (!strcmp(app->id, "wifi")) {
		builtin_wifi(r);
	} else if (!strcmp(app->id, "power")) {
		bool charging = false;
		int pct = ui_battery_percent(&charging);

		if (pct >= 0) {
			snprintf(r->meta, sizeof(r->meta), "%d%%", pct);
			snprintf(r->line, sizeof(r->line), "%s  %d%%", charging ? "CHARGING" : "ON BATTERY", pct);
			r->level = pct * 8 / 100;
		}
	} else if (!strcmp(app->id, "terminal")) {
		if (app->pid > 0 && kill(app->pid, 0) == 0) {
			long up = (long)(time(NULL) - app->started);

			snprintf(r->line, sizeof(r->line), "TTY2 /BIN/SH UP %02ld:%02ld", up / 3600, up / 60 % 60);
		} else {
			snprintf(r->line, sizeof(r->line), "TTY2 /BIN/SH  READY");
		}
	} else if (!strcmp(app->id, "settings")) {
		snprintf(r->line, sizeof(r->line), "%s %s/%s", ui_theme_style_name(), ui_theme_palette(), ui_theme_accent());
		upper(r->line);
	} else if (!strcmp(app->id, "scanner")) {
		builtin_scanner(r);
	} else if (!strcmp(app->id, "system")) {
		builtin_system(r);
	} else if (app->status_cmd[0]) {
		if (time(NULL) - app->status_at >= STATUS_EVERY_S)
			run_status(app);
		snprintf(r->meta, sizeof(r->meta), "%s", app->status_meta);
		snprintf(r->line, sizeof(r->line), "%s", app->status_line);
		r->level = app->status_level > 8 ? 8 : app->status_level;
	}
	upper(r->line);
#endif
}
