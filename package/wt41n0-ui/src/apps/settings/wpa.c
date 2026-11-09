/* wpa_supplicant control interface: /var/run/wpa_supplicant/wlan0 (datagram socket). */
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "wpa.h"

#define CTRL_PATH "/var/run/wpa_supplicant/wlan0"

#ifndef WT41N0_UI_HOST
static int sock = -1;
static char local_path[64];

static void wpa_close(void)
{
	if (sock >= 0)
		close(sock);
	sock = -1;
	unlink(local_path);
}

static int wpa_open(void)
{
	struct sockaddr_un addr = { .sun_family = AF_UNIX };

	sock = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (sock < 0)
		return -1;
	snprintf(local_path, sizeof(local_path), "/tmp/wt41n0-wpa-%d", (int)getpid());
	unlink(local_path);
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", local_path);
	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)))
		goto fail;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", CTRL_PATH);
	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)))
		goto fail;
	atexit(wpa_close);
	return 0;
fail:
	wpa_close();
	return -1;
}

int wpa_cmd(const char *cmd, char *reply, size_t len)
{
	struct pollfd pfd;
	ssize_t n;

	if (sock < 0 && wpa_open())
		return -1;
	if (send(sock, cmd, strlen(cmd), 0) < 0) {
		wpa_close();		/* wpa_supplicant restarted: reconnect next time */
		return -1;
	}
	for (;;) {
		pfd.fd = sock;
		pfd.events = POLLIN;
		if (poll(&pfd, 1, 3000) <= 0)
			return -1;
		n = recv(sock, reply, len - 1, 0);
		if (n < 0)
			return -1;
		reply[n] = 0;
		if (reply[0] != '<')	/* skip unsolicited event messages */
			return 0;
	}
}
#else
/* Host harness: canned replies matching the reference screens; selecting GARAGE
 * (saved) connects, anything else stays on HOMENET-5G */
int wpa_cmd(const char *cmd, char *reply, size_t len)
{
	static int selected = -1;
	const char *r = "OK\n";

	if (!strcmp(cmd, "STATUS"))
		r = selected == 1 ? "ssid=GARAGE\nwpa_state=COMPLETED\nip_address=10.0.0.57\n" :
		    "bssid=d0:21:f9:f1:a7:11\nssid=HOMENET-5G\nwpa_state=COMPLETED\nip_address=10.0.0.42\n";
	else if (!strcmp(cmd, "SCAN_RESULTS"))
		r = "bssid / frequency / signal level / flags / ssid\n"
		    "d0:21:f9:f1:a7:11\t5180\t-48\t[WPA2-PSK-CCMP][ESS]\tHOMENET-5G\n"
		    "a4:2b:b0:11:22:33\t2412\t-64\t[WPA2-PSK-CCMP][WPS][ESS]\tDOCK-07\n"
		    "a4:2b:b0:11:22:34\t2462\t-71\t[WPA2-PSK-CCMP][ESS]\tGARAGE\n"
		    "a4:2b:b0:11:22:35\t2412\t-80\t[ESS]\tCAFE_GUEST\n";
	else if (!strcmp(cmd, "LIST_NETWORKS"))
		r = selected == 1 ? "network id / ssid / bssid / flags\n0\tHOMENET-5G\tany\t\n1\tGARAGE\tany\t[CURRENT]\n" :
		    "network id / ssid / bssid / flags\n0\tHOMENET-5G\tany\t[CURRENT]\n1\tGARAGE\tany\t\n";
	else if (!strcmp(cmd, "ADD_NETWORK"))
		r = "2\n";
	else if (!strncmp(cmd, "SELECT_NETWORK ", 15))
		selected = cmd[15] - '0';
	snprintf(reply, len, "%s", r);
	return 0;
}
#endif

static const char *field(const char *text, const char *key, char *out, size_t len)
{
	size_t klen = strlen(key);
	const char *p = text;

	out[0] = 0;
	while (p && *p) {
		if (!strncmp(p, key, klen) && p[klen] == '=') {
			size_t n = strcspn(p + klen + 1, "\n");

			snprintf(out, len, "%.*s", (int)n, p + klen + 1);
			return out;
		}
		p = strchr(p, '\n');
		if (p)
			p++;
	}
	return NULL;
}

int wpa_status(char *ssid, size_t ssid_len, char *state, size_t state_len, char *ip, size_t ip_len)
{
	char reply[1024];

	if (wpa_cmd("STATUS", reply, sizeof(reply)))
		return -1;
	field(reply, "ssid", ssid, ssid_len);
	field(reply, "wpa_state", state, state_len);
	field(reply, "ip_address", ip, ip_len);
	return 0;
}

static int by_rank(const void *x, const void *y)
{
	const struct wpa_net *a = x, *b = y;

	if (a->current != b->current)
		return b->current - a->current;
	if ((a->saved_id >= 0) != (b->saved_id >= 0))
		return (b->saved_id >= 0) - (a->saved_id >= 0);
	return b->signal - a->signal;
}

int wpa_networks(struct wpa_net *nets, int max)
{
	static char reply[8192];
	char *line, *save = NULL;
	int n = 0, i;

	if (wpa_cmd("SCAN_RESULTS", reply, sizeof(reply)))
		return -1;
	line = strtok_r(reply, "\n", &save);		/* header */
	for (line = strtok_r(NULL, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
		char bssid[20], flags[128], ssid[sizeof(nets[0].ssid)];
		int freq, signal;

		ssid[0] = 0;
		if (sscanf(line, "%19s %d %d %127s %131[^\n]", bssid, &freq, &signal, flags, ssid) < 4 || !ssid[0])
			continue;			/* hidden networks have no SSID */
		for (i = 0; i < n && strcmp(nets[i].ssid, ssid); i++)
			;
		if (i < n) {				/* same network, another access point */
			if (signal > nets[i].signal)
				nets[i].signal = signal;
			continue;
		}
		if (n == max)
			continue;
		memset(&nets[n], 0, sizeof(nets[n]));
		snprintf(nets[n].ssid, sizeof(nets[n].ssid), "%s", ssid);
		nets[n].signal = signal;
		nets[n].sec = strstr(flags, "EAP") || strstr(flags, "WEP") ? SEC_OTHER :
			      strstr(flags, "PSK") ? SEC_PSK :
			      strstr(flags, "WPA") || strstr(flags, "SAE") ? SEC_OTHER : SEC_OPEN;
		nets[n].saved_id = -1;
		n++;
	}

	if (!wpa_cmd("LIST_NETWORKS", reply, sizeof(reply))) {
		save = NULL;
		line = strtok_r(reply, "\n", &save);
		for (line = strtok_r(NULL, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
			char *id = line, *ssid = strchr(line, '\t'), *rest;

			if (!ssid)
				continue;
			*ssid++ = 0;
			rest = strchr(ssid, '\t');
			if (rest)
				*rest++ = 0;
			for (i = 0; i < n; i++) {
				if (strcmp(nets[i].ssid, ssid))
					continue;
				nets[i].saved_id = atoi(id);
				nets[i].current = rest && strstr(rest, "[CURRENT]") != NULL;
			}
		}
	}
	qsort(nets, n, sizeof(nets[0]), by_rank);
	return n;
}

/* wpa_supplicant prints SSIDs escaped (\\, \", \e, \n, \r, \t, \xHH); give the raw
 * bytes as hex, which SET_NETWORK ssid accepts for any SSID */
void wpa_ssid_hex(const char *s, char *hex, size_t len)
{
	size_t o = 0;

	while (*s && o + 3 < len) {
		unsigned int c = (unsigned char)*s++;

		if (c == '\\' && *s) {
			c = (unsigned char)*s++;
			switch (c) {
			case 'n': c = '\n'; break;
			case 'r': c = '\r'; break;
			case 't': c = '\t'; break;
			case 'e': c = 0x1b; break;
			case 'x': {
				unsigned int v = 0;

				if (sscanf(s, "%2x", &v) == 1)
					s += 2;
				c = v;
				break;
			}
			default: break;		/* \\ and \" */
			}
		}
		o += snprintf(hex + o, len - o, "%02x", c);
	}
	hex[o] = 0;
}
