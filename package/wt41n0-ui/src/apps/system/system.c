/*
 * System: what the unit is and what it measures. A list of sections (device, processor,
 * memory, storage, network, power, sensors, software); each opens a list of values,
 * refreshed every second. ENT on a value shows it whole.
 *
 * Every value is read from this unit while the app runs (sysfs, /proc, the OMAP's fused
 * die ID, the eMMC's CID), so it is right on any unit the image is installed on.
 * WT41N0_SYSROOT prefixes the paths (the host harness points it at captured files).
 */
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#include "wt41n0ui.h"
#include "../settings/wpa.h"

#define MAX_KV		24
#define VALUE_SHOWN	22	/* longer values end in "..." in the list; ENT shows them whole */

struct kv {
	char name[24];
	char value[96];
};

typedef int (*section_fill)(struct kv *kv);

static const char *sysroot = "";
static lv_obj_t *rows[MAX_KV];
static struct kv shown[MAX_KV];
static int nshown, cur_section, list_focus;
static lv_timer_t *timer;

static void menu(int focus);
static void section_screen(int s, int focus);

/* ---- reading ---------------------------------------------------------------------- */

static const char *path(const char *p)
{
	static char buf[4][256];
	static int k;

	k = (k + 1) & 3;
	snprintf(buf[k], sizeof(buf[k]), "%s%s", sysroot, p);
	return buf[k];
}

/* the first line of a file, or NULL */
static char *line(const char *p, char *buf, size_t len)
{
	FILE *f = fopen(path(p), "r");
	char *r = NULL;

	if (f) {
		if (fgets(buf, (int)len, f)) {
			buf[strcspn(buf, "\n")] = 0;
			r = buf;
		}
		fclose(f);
	}
	return r;
}

static bool num(const char *p, long *v)
{
	char b[64];

	if (!line(p, b, sizeof(b)) || !b[0])
		return false;
	*v = strtol(b, NULL, 0);
	return true;
}

static void add(struct kv *kv, int *n, const char *name, const char *fmt, ...)
{
	va_list ap;

	if (*n >= MAX_KV)
		return;
	snprintf(kv[*n].name, sizeof(kv[*n].name), "%s", name);
	va_start(ap, fmt);
	vsnprintf(kv[*n].value, sizeof(kv[*n].value), fmt, ap);
	va_end(ap);
	for (char *c = kv[*n].value; *c; c++)
		if (*c >= 'a' && *c <= 'z')
			*c -= 32;
	(*n)++;
}

/* a meminfo field in kB */
static long meminfo(const char *key)
{
	FILE *f = fopen(path("/proc/meminfo"), "r");
	char l[128];
	long v = -1;
	size_t n = strlen(key);

	if (!f)
		return -1;
	while (fgets(l, sizeof(l), f))
		if (!strncmp(l, key, n) && l[n] == ':')
			v = atol(l + n + 1);
	fclose(f);
	return v;
}

static void size_text(char *buf, size_t len, double bytes)
{
	if (bytes >= 1e9)
		snprintf(buf, len, "%.2f GB", bytes / 1e9);
	else if (bytes >= 1e6)
		snprintf(buf, len, "%.1f MB", bytes / 1e6);
	else
		snprintf(buf, len, "%.0f KB", bytes / 1e3);
}

/* OMAP4 CONTROL_STD_FUSE_DIE_ID_0..3: 128 bits fused at the factory, unique per chip */
static bool die_id(char *buf, size_t len)
{
#ifndef WT41N0_UI_HOST
	int fd = open("/dev/mem", O_RDONLY | O_SYNC);
	volatile uint32_t *r;

	if (fd < 0)
		return false;
	r = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0x4A002000);
	close(fd);
	if (r == MAP_FAILED)
		return false;
	snprintf(buf, len, "%08X-%08X-%08X-%08X", (unsigned)r[0x210 / 4], (unsigned)r[0x20C / 4],
		 (unsigned)r[0x208 / 4], (unsigned)r[0x200 / 4]);
	munmap((void *)r, 4096);
	return true;
#else
	return line("/dieid", buf, len) != NULL;
#endif
}

/* the Bluetooth address of hci0, from the HCI device info (no bluez needed) */
static bool bt_address(char *buf, size_t len)
{
#ifndef WT41N0_UI_HOST
	unsigned char di[256] = { 0 };	/* struct hci_dev_info: dev_id, name[8], bdaddr[6], ... */
	int s = socket(31 /* AF_BLUETOOTH */, SOCK_RAW | SOCK_CLOEXEC, 1 /* BTPROTO_HCI */);
	bool ok;

	if (s < 0)
		return false;
	ok = ioctl(s, _IOR('H', 211, int) /* HCIGETDEVINFO */, di) == 0;
	close(s);
	if (!ok)
		return false;
	snprintf(buf, len, "%02X:%02X:%02X:%02X:%02X:%02X", di[15], di[14], di[13], di[12], di[11], di[10]);
	return true;
#else
	return line("/btaddr", buf, len) != NULL;
#endif
}

static bool input_present(const char *name)
{
	FILE *f = fopen(path("/proc/bus/input/devices"), "r");
	char l[160];
	bool found = false;

	if (!f)
		return false;
	while (fgets(l, sizeof(l), f))
		if (!strncmp(l, "N: Name=", 8) && strstr(l, name))
			found = true;
	fclose(f);
	return found;
}

/* ---- sections ----------------------------------------------------------------------- */

static int fill_device(struct kv *kv)
{
	char b[128], c[128];
	long v;
	int n = 0;

	add(kv, &n, "MODEL", "%s", line("/proc/device-tree/model", b, sizeof(b)) ? b : "ZEBRA WT41N0");
	add(kv, &n, "HARDWARE ID", "%s", die_id(b, sizeof(b)) ? b : "--");
	add(kv, &n, "FACTORY SERIAL", "NOT STORED ON THE UNIT");
	add(kv, &n, "EMMC SERIAL", "%s", line("/sys/block/mmcblk1/device/serial", b, sizeof(b)) ? b : "--");
	add(kv, &n, "WI-FI MAC", "%s", line("/sys/class/net/wlan0/address", b, sizeof(b)) ? b : "--");
	add(kv, &n, "BLUETOOTH", "%s", bt_address(b, sizeof(b)) ? b : "OFF");
	add(kv, &n, "TOUCH SCREEN", "%s", input_present("Touchscreen") ? "SX8654 RESISTIVE" : "NONE");
	add(kv, &n, "KEYPAD", "%s", input_present("keypad") ? "OMAP4 MATRIX" : "--");
	add(kv, &n, "DISPLAY", "320 X 240");
	{	/* the first (only) backlight */
		DIR *d = opendir(path("/sys/class/backlight"));
		struct dirent *e;
		long max;

		while (d && (e = readdir(d)))
			if (e->d_name[0] != '.') {
				snprintf(c, sizeof(c), "/sys/class/backlight/%s/brightness", e->d_name);
				snprintf(b, sizeof(b), "/sys/class/backlight/%s/max_brightness", e->d_name);
				if (num(c, &v) && num(b, &max) && max > 0)
					add(kv, &n, "BACKLIGHT", "%ld OF %ld", v, max);
				break;
			}
		if (d)
			closedir(d);
	}
	return n;
}

static int fill_cpu(struct kv *kv)
{
	char b[96], m[48], r[32], t[16];
	double l1, l5, l15, up;
	long v;
	int n = 0, cores = 0;
	FILE *f;

	add(kv, &n, "PROCESSOR", "%s %s %s", line("/sys/bus/soc/devices/soc0/machine", m, sizeof(m)) ? m : "OMAP4",
	    line("/sys/bus/soc/devices/soc0/revision", r, sizeof(r)) ? r : "",
	    line("/sys/bus/soc/devices/soc0/type", t, sizeof(t)) ? t : "");
	for (cores = 0; cores < 8; cores++) {
		snprintf(b, sizeof(b), "/sys/devices/system/cpu/cpu%d", cores);
		if (access(path(b), F_OK))
			break;
	}
	add(kv, &n, "CORES", "%d X CORTEX-A9", cores);
	if (num("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", &v))
		add(kv, &n, "CLOCK", "%ld MHZ", v / 1000);
	if (num("/sys/class/thermal/thermal_zone0/temp", &v))
		add(kv, &n, "TEMPERATURE", "%.1f C", v / 1000.0);
	f = fopen(path("/proc/loadavg"), "r");
	if (f) {
		if (fscanf(f, "%lf %lf %lf", &l1, &l5, &l15) == 3)
			add(kv, &n, "LOAD 1/5/15 MIN", "%.2f %.2f %.2f", l1, l5, l15);
		fclose(f);
	}
	f = fopen(path("/proc/uptime"), "r");
	if (f) {
		if (fscanf(f, "%lf", &up) == 1)
			add(kv, &n, "UP FOR", "%ldD %02ld:%02ld:%02ld", (long)up / 86400, (long)up / 3600 % 24,
			    (long)up / 60 % 60, (long)up % 60);
		fclose(f);
	}
	return n;
}

static int fill_memory(struct kv *kv)
{
	long total = meminfo("MemTotal"), avail = meminfo("MemAvailable"), free_ = meminfo("MemFree");
	long buffers = meminfo("Buffers"), cached = meminfo("Cached"), swap = meminfo("SwapTotal");
	char a[24], b[24];
	int n = 0;

	if (total <= 0)
		return 0;
	size_text(a, sizeof(a), total * 1024.0);
	add(kv, &n, "TOTAL", "%s", a);
	size_text(a, sizeof(a), (total - avail) * 1024.0);
	add(kv, &n, "IN USE", "%s (%ld%%)", a, (total - avail) * 100 / total);
	size_text(a, sizeof(a), avail * 1024.0);
	add(kv, &n, "AVAILABLE", "%s", a);
	size_text(a, sizeof(a), free_ * 1024.0);
	add(kv, &n, "FREE", "%s", a);
	size_text(a, sizeof(a), (buffers + cached) * 1024.0);
	add(kv, &n, "CACHE", "%s", a);
	size_text(b, sizeof(b), swap * 1024.0);
	add(kv, &n, "SWAP", "%s", swap > 0 ? b : "NONE");
	return n;
}

static void fs_rows(struct kv *kv, int *n, const char *name, const char *mount)
{
	struct statvfs s;
	char a[24], b[24];

	if (statvfs(mount, &s))
		return;
	size_text(a, sizeof(a), (double)(s.f_blocks - s.f_bavail) * s.f_frsize);
	size_text(b, sizeof(b), (double)s.f_blocks * s.f_frsize);
	add(kv, n, name, "%s OF %s", a, b);
}

static int fill_storage(struct kv *kv)
{
	char b[64], c[32];
	long sectors;
	int n = 0;

	if (num("/sys/block/mmcblk1/size", &sectors)) {
		size_text(c, sizeof(c), sectors * 512.0);
		add(kv, &n, "EMMC", "%s %s", line("/sys/block/mmcblk1/device/name", b, sizeof(b)) ? b : "", c);
	}
	if (line("/sys/block/mmcblk1/device/manfid", b, sizeof(b)))
		add(kv, &n, "MAKER", "%s", !strcmp(b, "0x000011") ? "TOSHIBA" : b);
	if (line("/sys/block/mmcblk1/device/date", b, sizeof(b)))
		add(kv, &n, "MADE", "%s", b);
	if (line("/sys/block/mmcblk1/device/cid", b, sizeof(b)))
		add(kv, &n, "CID", "%s", b);
	fs_rows(kv, &n, "ROOT IN USE", "/");
	fs_rows(kv, &n, "/TMP IN USE", "/tmp");
	return n;
}

static int fill_network(struct kv *kv)
{
	char ssid[140], state[32], ip[48], b[160];
	float link, level;
	int n = 0;
	FILE *f;

	if (wpa_status(ssid, sizeof(ssid), state, sizeof(state), ip, sizeof(ip))) {
		add(kv, &n, "WI-FI", "OFF");
	} else {
		add(kv, &n, "WI-FI", "%s", strcmp(state, "COMPLETED") ? state : "CONNECTED");
		if (!strcmp(state, "COMPLETED")) {
			add(kv, &n, "NETWORK", "%s", ssid);
			add(kv, &n, "IP ADDRESS", "%s", ip[0] ? ip : "NONE YET");
		}
	}
	f = fopen(path("/proc/net/wireless"), "r");
	if (f) {
		while (fgets(b, sizeof(b), f))
			if (sscanf(b, " wlan0: %*x %f %f", &link, &level) == 2)
				add(kv, &n, "SIGNAL", "%.0f DBM", level);
		fclose(f);
	}
	add(kv, &n, "WI-FI MAC", "%s", line("/sys/class/net/wlan0/address", b, sizeof(b)) ? b : "--");
	add(kv, &n, "BLUETOOTH", "%s", bt_address(b, sizeof(b)) ? b : "OFF");
	if (line("/proc/sys/kernel/hostname", b, sizeof(b)))
		add(kv, &n, "HOSTNAME", "%s", b);
	return n;
}

static int fill_power(struct kv *kv)
{
	const char *bat = "/sys/class/power_supply/wt41n0-battery/";
	char p[128], b[64];
	long v, present = 1;
	int n = 0;

#define BAT(f) (snprintf(p, sizeof(p), "%s%s", bat, f), p)
	num(BAT("present"), &present);
	if (!present) {
		add(kv, &n, "BATTERY", "NOT PRESENT");
	} else {
		if (num(BAT("capacity"), &v))
			add(kv, &n, "CHARGE", "%ld%%", v);
		if (line(BAT("status"), b, sizeof(b)))
			add(kv, &n, "STATE", "%s", b);
		if (num(BAT("voltage_now"), &v))
			add(kv, &n, "VOLTAGE", "%.3f V", v / 1e6);
		if (num(BAT("current_now"), &v))
			add(kv, &n, "CURRENT", "%.0f MA", v / 1e3);
		if (num(BAT("temp"), &v))
			add(kv, &n, "TEMPERATURE", "%.1f C", v / 10.0);
		if (line(BAT("health"), b, sizeof(b)))
			add(kv, &n, "HEALTH", "%s", b);
		if (line(BAT("technology"), b, sizeof(b)))
			add(kv, &n, "TYPE", "%s", b);
	}
	if (num("/sys/class/power_supply/wt41n0-cradle/online", &v))
		add(kv, &n, "CRADLE", "%s", v ? "CONNECTED" : "NO");
	if (num("/sys/class/power_supply/wt41n0-usb/online", &v))
		add(kv, &n, "USB POWER", "%s", v ? "CONNECTED" : "NO");
#undef BAT
	return n;
}

static int fill_sensors(struct kv *kv)
{
	static const struct { int ch; const char *name; } adc[] = {
		{ 7, "ADC MAIN BATTERY" }, { 8, "ADC BACKUP BATTERY" }, { 9, "ADC CHARGER IN" },
		{ 10, "ADC VBUS" }, { 0, "ADC CH 0" }, { 2, "ADC CH 2" }, { 14, "ADC CH 14" },
	};
	const char *acc = "/sys/bus/iio/devices/iio:device0/", *gp = "/sys/bus/iio/devices/iio:device1/";
	char p[128], b[64];
	long x, y, z, v;
	double scale = 0;
	unsigned int i;
	int n = 0;

	snprintf(p, sizeof(p), "%sin_accel_scale", acc);
	if (line(p, b, sizeof(b)))
		scale = atof(b);
	snprintf(p, sizeof(p), "%sin_accel_x_raw", acc);
	if (scale > 0 && num(p, &x)) {
		snprintf(p, sizeof(p), "%sin_accel_y_raw", acc);
		num(p, &y);
		snprintf(p, sizeof(p), "%sin_accel_z_raw", acc);
		num(p, &z);
		add(kv, &n, "ACCEL X", "%+.2f M/S2", x * scale);
		add(kv, &n, "ACCEL Y", "%+.2f M/S2", y * scale);
		add(kv, &n, "ACCEL Z", "%+.2f M/S2", z * scale);
	}
	if (num("/sys/class/thermal/thermal_zone0/temp", &v))
		add(kv, &n, "CPU TEMPERATURE", "%.1f C", v / 1000.0);
	if (num("/sys/class/power_supply/wt41n0-battery/temp", &v))
		add(kv, &n, "BATTERY TEMP", "%.1f C", v / 10.0);
	for (i = 0; i < sizeof(adc) / sizeof(adc[0]); i++) {
		snprintf(p, sizeof(p), "%sin_voltage%d_input", gp, adc[i].ch);
		if (num(p, &v) && v >= 0)
			add(kv, &n, adc[i].name, "%ld MV", v);
	}
	return n;
}

static int fill_software(struct kv *kv)
{
	char b[128], k[64];
	struct utsname u;
	time_t now = time(NULL);
	FILE *f = fopen(path("/etc/os-release"), "r");
	int n = 0;

	k[0] = 0;
	if (f) {
		while (fgets(b, sizeof(b), f)) {
			b[strcspn(b, "\n")] = 0;
			if (!strncmp(b, "VERSION_ID=", 11))
				add(kv, &n, "BUILDROOT", "%s", b + 11);
			else if (!strncmp(b, "VERSION=", 8))
				snprintf(k, sizeof(k), "%s", b + 8 + (b[8] == '-'));
		}
		fclose(f);
	}
	if (k[0])
		add(kv, &n, "BSP BUILD", "%s", k);
	if (!uname(&u)) {
		add(kv, &n, "KERNEL", "%s", u.release);
		add(kv, &n, "KERNEL BUILT", "%s", u.version);
	}
	add(kv, &n, "LAUNCHER", "%s", WT41N0_UI_VERSION);
	add(kv, &n, "THEME", "%s %s/%s", ui_theme_style_name(), ui_theme_palette(), ui_theme_accent());
	strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", localtime(&now));
	add(kv, &n, "LOCAL TIME", "%s", b);
	add(kv, &n, "TIME ZONE", "%s", getenv("TZ") ? getenv("TZ") : "UTC");
	return n;
}

static const struct {
	const char *name, *icon;
	section_fill fill;
} sections[] = {
	{ "DEVICE", "system", fill_device }, { "PROCESSOR", "terminal", fill_cpu },
	{ "MEMORY", "list", fill_memory }, { "STORAGE", "sd", fill_storage },
	{ "NETWORK", "wifi", fill_network }, { "POWER", "charge", fill_power },
	{ "SENSORS", "touch", fill_sensors }, { "SOFTWARE", "file", fill_software },
};
#define NSECTIONS (int)(sizeof(sections) / sizeof(sections[0]))

/* ---- screens ------------------------------------------------------------------------- */

static void quit(void) { ui_quit(0); }

static void short_value(char *out, size_t len, const char *v)
{
	if (strlen(v) > VALUE_SHOWN)
		snprintf(out, len, "%.*s...", VALUE_SHOWN - 3, v);
	else
		snprintf(out, len, "%s", v);
}

static void refresh(lv_timer_t *t)
{
	struct kv kv[MAX_KV];
	char v[100];
	int n = sections[cur_section].fill(kv), i;

	(void)t;
	for (i = 0; i < n && i < nshown; i++) {
		if (strcmp(kv[i].name, shown[i].name) || !strcmp(kv[i].value, shown[i].value))
			continue;
		shown[i] = kv[i];
		short_value(v, sizeof(v), kv[i].value);
		ui_row_set_value(rows[i], v);
	}
}

static void stop_refresh(lv_event_t *e)
{
	(void)e;
	if (timer)
		lv_timer_delete(timer);
	timer = NULL;
}

static void back_to_section(void) { section_screen(cur_section, list_focus); }
static void back_to_menu(void) { menu(cur_section); }

/* one value, whole */
static void value_screen(int i)
{
	const ui_palette_t *p = ui_palette();
	char title[64];
	lv_obj_t *c, *card, *l;

	snprintf(title, sizeof(title), "%s/%s", sections[cur_section].name, shown[i].name);
	c = ui_screen(title);
	ui_hint_keys("ESC BACK", "");
	ui_on_back(back_to_section);
	card = ui_card(c, WT_INNER_X, WT_INNER_Y, WT_INNER_W, WT_INNER_H);
	l = lv_label_create(card);
	lv_label_set_text(l, shown[i].value);
	ui_text_role(l, UI_ROLE_KEY);
	lv_obj_set_style_text_color(l, p->text, 0);
	lv_obj_set_style_text_letter_space(l, 1, 0);
	lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(l, WT_INNER_W - 20);
	lv_obj_set_pos(l, 9, 9);
}

static void value_event(lv_event_t *e)
{
	int i = (int)(intptr_t)lv_event_get_user_data(e);

	if (lv_event_get_code(e) == LV_EVENT_FOCUSED)
		list_focus = i;
	else if (lv_event_get_code(e) == LV_EVENT_CLICKED)
		value_screen(i);
}

static void section_screen(int s, int focus)
{
	char title[40], v[100];
	lv_obj_t *c, *list;
	int i;

	cur_section = s;
	snprintf(title, sizeof(title), "SYSTEM/%s", sections[s].name);
	c = ui_screen(title);
	ui_hint_keys("\xE2\x86\x91\xE2\x86\x93 SELECT|ENT FULL VALUE|ESC BACK", "");
	ui_on_back(back_to_menu);
	nshown = sections[s].fill(shown);
	list = ui_rows_create(c, WT_INNER_Y, WT_INNER_H, false);
	for (i = 0; i < nshown; i++) {
		short_value(v, sizeof(v), shown[i].value);
		rows[i] = ui_row_ex(list, NULL, shown[i].name, v, UI_ROW_NO_NUMBER | UI_ROW_NO_CHEVRON);
		lv_obj_add_event_cb(rows[i], value_event, LV_EVENT_ALL, (void *)(intptr_t)i);
	}
	if (!nshown) {
		lv_obj_t *l = lv_label_create(c);

		lv_label_set_text(l, "NOTHING TO SHOW");
		ui_text_role(l, UI_ROLE_HINT);
		lv_obj_set_style_text_color(l, ui_palette()->dim, 0);
		lv_obj_center(l);
	}
	ui_rows_done(list, focus);
	lv_obj_add_event_cb(list, stop_refresh, LV_EVENT_DELETE, NULL);
	timer = lv_timer_create(refresh, 1000, NULL);
}

static void menu_event(lv_event_t *e)
{
	if (lv_event_get_code(e) == LV_EVENT_CLICKED)
		section_screen((int)(intptr_t)lv_event_get_user_data(e), 0);
}

/* the menu's value column: one line per section */
static void summary(int s, char *out, size_t len)
{
	struct kv kv[MAX_KV];
	int n = sections[s].fill(kv), i;
	const char *want = s == 0 ? "HARDWARE ID" : s == 1 ? "TEMPERATURE" : s == 2 ? "IN USE" :
			   s == 3 ? "ROOT IN USE" : s == 4 ? "IP ADDRESS" : s == 5 ? "CHARGE" :
			   s == 6 ? "CPU TEMPERATURE" : "KERNEL";

	out[0] = 0;
	for (i = 0; i < n; i++)
		if (!strcmp(kv[i].name, want))
			short_value(out, len, kv[i].value);
	if (!out[0] && n)
		short_value(out, len, kv[0].value);
	if (s == 0 && strlen(out) > 12)		/* the ID's first group is enough here */
		snprintf(out + 8, len - 8, "...");
}

static void menu(int focus)
{
	lv_obj_t *c = ui_screen("SYSTEM"), *list, *row;
	char v[100];
	int i;

	ui_hint_keys("\xE2\x86\x91\xE2\x86\x93 SELECT|ENT OPEN|ESC BACK", "");
	ui_on_back(quit);
	list = ui_rows_create(c, WT_INNER_Y, WT_INNER_H, false);
	for (i = 0; i < NSECTIONS; i++) {
		summary(i, v, sizeof(v));
		row = ui_row(list, sections[i].icon, sections[i].name, v);
		lv_obj_add_event_cb(row, menu_event, LV_EVENT_CLICKED, (void *)(intptr_t)i);
	}
	ui_rows_done(list, focus);
}

int main(int argc, char **argv)
{
	int i;

	if (getenv("WT41N0_SYSROOT"))
		sysroot = getenv("WT41N0_SYSROOT");
	ui_init();
	/* "wt41n0-system <section>" opens on that section */
	for (i = 0; argc > 1 && i < NSECTIONS; i++)
		if (!strcasecmp(argv[1], sections[i].name)) {
			section_screen(i, 0);
			return ui_run();
		}
	menu(0);
	return ui_run();
}
