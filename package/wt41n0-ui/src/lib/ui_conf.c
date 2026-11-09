/* /etc/wt41n0/ui.conf: one "key=value" setting per line, shared by all apps. */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "ui_internal.h"

#define MAX_LINES 64

int ui_conf_get(const char *key, char *buf, size_t len) { return conf_get(key, buf, len); }
int ui_conf_set(const char *key, const char *value) { return conf_set(key, value); }

/* Copy the value of key into buf; returns 0 when found */
int conf_get(const char *key, char *buf, size_t len)
{
	char line[256];
	size_t klen = strlen(key);
	FILE *f = fopen(platform_conf_path(), "r");
	int ret = -1;

	if (!f)
		return -1;
	while (fgets(line, sizeof(line), f)) {
		char *v = line + klen;

		if (strncmp(line, key, klen) || *v != '=')
			continue;
		v++;
		v[strcspn(v, "\r\n")] = 0;
		snprintf(buf, len, "%s", v);
		ret = 0;
	}
	fclose(f);
	return ret;
}

/* Set key to value (NULL removes it), keeping the other lines; replaced atomically */
int conf_set(const char *key, const char *value)
{
	static char lines[MAX_LINES][256];
	char path[256];
	size_t klen = strlen(key);
	int n = 0, i;
	FILE *f = fopen(platform_conf_path(), "r");

	if (f) {
		while (n < MAX_LINES && fgets(lines[n], sizeof(lines[n]), f))
			if (strncmp(lines[n], key, klen) || lines[n][klen] != '=')
				n++;
		fclose(f);
	}
	/* create the directory on first use (/etc/wt41n0) */
	snprintf(path, sizeof(path), "%s", platform_conf_path());
	if (strrchr(path, '/') && strrchr(path, '/') != path) {
		*strrchr(path, '/') = 0;
		mkdir(path, 0755);
	}
	snprintf(path, sizeof(path), "%s.new", platform_conf_path());
	f = fopen(path, "w");
	if (!f)
		return -1;
	for (i = 0; i < n; i++)
		fputs(lines[i], f);
	if (value)
		fprintf(f, "%s=%s\n", key, value);
	if (fclose(f))
		return -1;
	return rename(path, platform_conf_path());
}
