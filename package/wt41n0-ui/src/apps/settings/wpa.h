/* A minimal wpa_supplicant control-socket client (the protocol wpa_cli uses) */
#ifndef SETTINGS_WPA_H
#define SETTINGS_WPA_H

#include <stddef.h>

#define WPA_MAX_NETS 24

struct wpa_net {
	char ssid[33 * 4];	/* as wpa_supplicant prints it (escaped) */
	int signal;		/* dBm */
	enum { SEC_OPEN, SEC_PSK, SEC_OTHER } sec;
	int saved_id;		/* network id, -1 if not saved */
	int current;		/* the network in use */
};

int wpa_cmd(const char *cmd, char *reply, size_t len);	/* 0 = got a reply */
int wpa_status(char *ssid, size_t ssid_len, char *state, size_t state_len, char *ip, size_t ip_len);
int wpa_networks(struct wpa_net *nets, int max);	/* scan results merged with saved networks */
void wpa_ssid_hex(const char *escaped, char *hex, size_t len);

#endif
