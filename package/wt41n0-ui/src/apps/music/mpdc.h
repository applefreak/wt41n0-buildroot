/* mpdc: what the Music app needs from MPD, over libmpdclient (canned data in the
 * host harness). Every call connects on demand and drops the connection on an error,
 * so the next call reconnects. */
#ifndef MPDC_H
#define MPDC_H

#include <stdbool.h>

enum mpdc_state { MPDC_OFFLINE = -1, MPDC_STOP, MPDC_PLAY, MPDC_PAUSE };

struct mpdc_status {
	enum mpdc_state state;
	int volume;			/* 0..100, -1 without a mixer */
	unsigned elapsed, total;	/* seconds; total 0 for streams */
	int song_id, song_pos, queue_len;
	bool repeat, random;
	char title[128], artist[96], album[96];	/* album: or the stream's name */
};

enum mpdc_kind { MPDC_DIR, MPDC_SONG, MPDC_PLAYLIST };

struct mpdc_item {
	enum mpdc_kind kind;
	char uri[256];
	char name[128];			/* title, or the last part of the path */
	char info[48];			/* artist, or the duration */
};

#define MPDC_MAX_ITEMS	150

bool mpdc_status(struct mpdc_status *s);	/* false: MPD not reachable */
const char *mpdc_error(void);			/* the last error, upper case */

void mpdc_toggle(void);				/* play / pause */
void mpdc_next(void);
void mpdc_prev(void);
void mpdc_volume(int delta);

int mpdc_queue(struct mpdc_item *items, int max);
void mpdc_play_pos(int pos);
void mpdc_delete(int pos);

int mpdc_browse(const char *path, struct mpdc_item *items, int max);
void mpdc_play_dir(const char *path, int index);	/* replace the queue with the folder's songs, play song index (-1: first) */
void mpdc_update(void);				/* rescan the library */

#define MPDC_RADIO	"Radio"			/* the stored playlist of stations */
int mpdc_stations(struct mpdc_item *items, int max);	/* the Radio playlist's entries */
int mpdc_playlists(struct mpdc_item *items, int max);	/* the other stored playlists */
void mpdc_play_uri(const char *uri);		/* replace the queue with it and play */
bool mpdc_is_playlist_url(const char *uri);	/* .m3u / .pls / .xspf: loaded, not streamed */
void mpdc_play_playlist(const char *name);
bool mpdc_add_station(const char *url);

#endif
