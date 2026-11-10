/* mpdc: the Music app's MPD calls, over libmpdclient; canned data in the host harness. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "mpdc.h"

static char error[96];

#ifndef WT41N0_UI_HOST
static void upper_copy(char *dst, size_t len, const char *src)
{
	size_t i;

	for (i = 0; src && src[i] && i + 1 < len; i++)
		dst[i] = (src[i] >= 'a' && src[i] <= 'z') ? src[i] - 32 : src[i];
	dst[i] = 0;
}
#endif

const char *mpdc_error(void)
{
	return error;
}

/* a remote playlist (.m3u, .pls, .xspf; the query string ignored): MPD loads it as a
 * playlist (Internet Archive items have NAME_64kb.m3u), it cannot play it as a stream */
bool mpdc_is_playlist_url(const char *uri)
{
	static const char *const ext[] = { ".m3u", ".pls", ".xspf" };
	size_t n = strcspn(uri, "?#"), i, e;

	for (i = 0; i < sizeof(ext) / sizeof(ext[0]); i++) {
		e = strlen(ext[i]);
		if (n >= e && !strncasecmp(uri + n - e, ext[i], e))
			return true;
	}
	return false;
}

/* the last path component */
static const char *base(const char *uri)
{
	const char *s = strrchr(uri, '/');

	return s && s[1] ? s + 1 : uri;
}

static int hexval(char c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 :
	       c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

/* A song's name from its file name, for songs without tags (an online song has none
 * until MPD plays it): the last part of the path without a query, %XX decoded (links
 * from online playlists are stored encoded, see url_encode), and the extension
 * dropped ("Blue%20Moon.mp3" -> "Blue Moon"). */
static void file_name(const char *uri, char *out, size_t len, bool song)
{
	const char *s = base(uri);
	char *dot;
	size_t o = 0;

	for (; *s && *s != '?' && *s != '#' && o + 1 < len; s++) {
		if (*s == '%' && hexval(s[1]) >= 0 && hexval(s[2]) >= 0) {
			out[o++] = (char)(hexval(s[1]) * 16 + hexval(s[2]));
			s += 2;
		} else {
			out[o++] = *s;
		}
	}
	out[o] = 0;
	dot = strrchr(out, '.');
	if (song && dot && dot != out && strlen(dot) <= 5)
		*dot = 0;
}

static void item_set(struct mpdc_item *it, enum mpdc_kind kind, const char *uri, const char *name,
		     const char *info)
{
	it->kind = kind;
	snprintf(it->uri, sizeof(it->uri), "%s", uri);
	if (name && *name)
		snprintf(it->name, sizeof(it->name), "%s", name);
	else
		file_name(uri, it->name, sizeof(it->name), kind == MPDC_SONG);
	snprintf(it->info, sizeof(it->info), "%s", info ? info : "");
}

#ifdef WT41N0_UI_HOST
/* ---- the harness: a fixed library, a playing song ------------------------------- */

static struct mpdc_status host = {
	.state = MPDC_PLAY, .volume = 60, .elapsed = 83, .total = 241, .song_id = 3, .song_pos = 2,
	.queue_len = 60, .repeat = true, .random = false,
	.title = "Neon Harbour", .artist = "The Static Sea", .album = "Low Tide",
};

bool mpdc_status(struct mpdc_status *s)
{
	*s = host;
	return true;
}

void mpdc_toggle(void) { host.state = host.state == MPDC_PLAY ? MPDC_PAUSE : MPDC_PLAY; }
void mpdc_next(void) { host.song_pos++; host.song_id++; }
void mpdc_prev(void) { if (host.song_pos) { host.song_pos--; host.song_id--; } }
void mpdc_volume(int delta) { host.volume = host.volume + delta < 0 ? 0 : host.volume + delta > 100 ? 100 : host.volume + delta; }

static const char *const host_songs[][3] = {
	{ "Albums/Low Tide/01 Breakwater.flac", "Breakwater", "3:58" },
	{ "Albums/Low Tide/02 Salt Lines.flac", "Salt Lines", "4:12" },
	{ "Albums/Low Tide/03 Neon Harbour.flac", "Neon Harbour", "4:01" },
	{ "Albums/Low Tide/04 Undertow.flac", "Undertow", "5:20" },
	{ "Albums/Low Tide/05 Lighthouse Keeper.flac", "Lighthouse Keeper", "3:33" },
};

/* 60 songs: more than a list once held (40), as the Internet Archive's albums can be */
int mpdc_queue(struct mpdc_item *items, int max)
{
	char name[64];
	int i;

	for (i = 0; i < 60 && i < max; i++) {
		snprintf(name, sizeof(name), "%s%s", host_songs[i % 5][1], i < 5 ? "" : " (Live)");
		if (i == 5)	/* an online song MPD has not played yet: no tags, an encoded link */
			item_set(&items[i], MPDC_SONG, "https://archive.org/download/x/%5BBonus%5D%20-%20In%20the%20Ghetto.mp3",
				 NULL, "STREAM");
		else
			item_set(&items[i], MPDC_SONG, host_songs[i % 5][0], name, host_songs[i % 5][2]);
	}
	return i;
}

void mpdc_play_pos(int pos) { host.song_pos = pos; host.state = MPDC_PLAY; }
void mpdc_delete(int pos) { (void)pos; }

int mpdc_browse(const char *path, struct mpdc_item *items, int max)
{
	int i, n = 0;

	if (!*path) {
		item_set(&items[n++], MPDC_DIR, "Albums", NULL, NULL);
		item_set(&items[n++], MPDC_DIR, "Podcasts", NULL, NULL);
		item_set(&items[n++], MPDC_SONG, "intro.mp3", "Intro", "0:42");
	} else if (!strcmp(path, "Albums")) {
		item_set(&items[n++], MPDC_DIR, "Albums/Low Tide", NULL, NULL);
		item_set(&items[n++], MPDC_DIR, "Albums/Paper Satellites", NULL, NULL);
	} else {
		for (i = 0; i < 5 && n < max; i++)
			item_set(&items[n++], MPDC_SONG, host_songs[i][0], host_songs[i][1], host_songs[i][2]);
	}
	return n;
}

void mpdc_play_dir(const char *path, int index) { (void)path; host.song_pos = index < 0 ? 0 : index; }
void mpdc_update(void) {}

int mpdc_stations(struct mpdc_item *items, int max)
{
	(void)max;
	item_set(&items[0], MPDC_SONG, "https://radio.example/jazz", "Example Jazz", "STREAM");
	item_set(&items[1], MPDC_SONG, "https://archive.org/download/example/example_64kb.m3u", NULL, "STREAM");
	return 2;
}

int mpdc_playlists(struct mpdc_item *items, int max)
{
	(void)max;
	item_set(&items[0], MPDC_PLAYLIST, "Workout", NULL, "PLAYLIST");
	return 1;
}

void mpdc_play_uri(const char *uri) { (void)uri; }
void mpdc_play_playlist(const char *name) { (void)name; }
bool mpdc_add_station(const char *url) { return url && *url; }

#else
/* ---- MPD over its Unix socket ------------------------------------------------------ */
#include <mpd/client.h>

#define TIMEOUT_MS	2000

static struct mpd_connection *conn;

static const char *socket_path(void)
{
	const char *p = getenv("WT41N0_MPD");

	return p ? p : "/run/mpd.socket";
}

static struct mpd_connection *get(void)
{
	if (conn)
		return conn;
	conn = mpd_connection_new(socket_path(), 0, TIMEOUT_MS);
	if (!conn) {
		snprintf(error, sizeof(error), "OUT OF MEMORY");
		return NULL;
	}
	if (mpd_connection_get_error(conn) != MPD_ERROR_SUCCESS) {
		upper_copy(error, sizeof(error), mpd_connection_get_error_message(conn));
		mpd_connection_free(conn);
		conn = NULL;
	}
	return conn;
}

/* after each command: keep the connection on a refused command, drop it otherwise */
static bool check(void)
{
	if (!conn)
		return false;
	if (mpd_connection_get_error(conn) == MPD_ERROR_SUCCESS)
		return true;
	upper_copy(error, sizeof(error), mpd_connection_get_error_message(conn));
	if (!mpd_connection_clear_error(conn)) {
		mpd_connection_free(conn);
		conn = NULL;
	}
	return false;
}

static void tag(const struct mpd_song *song, enum mpd_tag_type t, char *dst, size_t len)
{
	const char *v = mpd_song_get_tag(song, t, 0);

	snprintf(dst, len, "%s", v ? v : "");
}

bool mpdc_status(struct mpdc_status *s)
{
	struct mpd_status *st;
	struct mpd_song *song;

	memset(s, 0, sizeof(*s));
	s->state = MPDC_OFFLINE;
	s->volume = -1;
	if (!get())
		return false;
	st = mpd_run_status(conn);
	if (!st) {
		check();
		return false;
	}
	switch (mpd_status_get_state(st)) {
	case MPD_STATE_PLAY:	s->state = MPDC_PLAY; break;
	case MPD_STATE_PAUSE:	s->state = MPDC_PAUSE; break;
	default:		s->state = MPDC_STOP; break;
	}
	s->volume = mpd_status_get_volume(st);
	s->elapsed = mpd_status_get_elapsed_time(st);
	s->total = mpd_status_get_total_time(st);
	s->song_id = mpd_status_get_song_id(st);
	s->song_pos = mpd_status_get_song_pos(st);
	s->queue_len = (int)mpd_status_get_queue_length(st);
	s->repeat = mpd_status_get_repeat(st);
	s->random = mpd_status_get_random(st);
	mpd_status_free(st);

	song = s->song_id >= 0 ? mpd_run_current_song(conn) : NULL;
	if (song) {
		tag(song, MPD_TAG_TITLE, s->title, sizeof(s->title));
		tag(song, MPD_TAG_ARTIST, s->artist, sizeof(s->artist));
		tag(song, MPD_TAG_ALBUM, s->album, sizeof(s->album));
		if (!s->album[0])
			tag(song, MPD_TAG_NAME, s->album, sizeof(s->album));	/* a stream's station */
		if (!s->title[0])
			file_name(mpd_song_get_uri(song), s->title, sizeof(s->title), true);
		mpd_song_free(song);
	}
	return check() || s->state != MPDC_OFFLINE;
}

void mpdc_toggle(void)
{
	struct mpd_status *st;

	if (!get())
		return;
	st = mpd_run_status(conn);
	if (!st) {
		check();
		return;
	}
	if (mpd_status_get_state(st) == MPD_STATE_PLAY)
		mpd_run_pause(conn, true);
	else
		mpd_run_play(conn);
	mpd_status_free(st);
	check();
}

void mpdc_next(void)
{
	if (get()) {
		mpd_run_next(conn);
		check();
	}
}

void mpdc_prev(void)
{
	if (get()) {
		mpd_run_previous(conn);
		check();
	}
}

void mpdc_volume(int delta)
{
	struct mpd_status *st;
	int v;

	if (!get() || !(st = mpd_run_status(conn))) {
		check();
		return;
	}
	v = mpd_status_get_volume(st);
	mpd_status_free(st);
	if (v < 0)
		return;
	v += delta;
	mpd_run_set_volume(conn, (unsigned)(v < 0 ? 0 : v > 100 ? 100 : v));
	check();
}

/* a song row: its title (or station name, or file name), and its duration; the rows
 * are narrow, so no artist */
static void song_item(struct mpdc_item *it, const struct mpd_song *song)
{
	char title[128], info[16];
	unsigned d = mpd_song_get_duration(song);

	tag(song, MPD_TAG_TITLE, title, sizeof(title));
	if (!title[0])
		tag(song, MPD_TAG_NAME, title, sizeof(title));
	if (d)
		snprintf(info, sizeof(info), "%u:%02u", d / 60, d % 60);
	else
		snprintf(info, sizeof(info), "STREAM");
	item_set(it, MPDC_SONG, mpd_song_get_uri(song), title, info);
}

/* read songs (and, for a folder, entities) after a send_ */
static int recv_songs(struct mpdc_item *items, int max)
{
	struct mpd_song *song;
	int n = 0;

	while ((song = mpd_recv_song(conn))) {
		if (n < max)
			song_item(&items[n++], song);
		mpd_song_free(song);
	}
	mpd_response_finish(conn);
	return check() ? n : n;
}

int mpdc_queue(struct mpdc_item *items, int max)
{
	if (!get() || !mpd_send_list_queue_meta(conn)) {
		check();
		return 0;
	}
	return recv_songs(items, max);
}

void mpdc_play_pos(int pos)
{
	if (get()) {
		mpd_run_play_pos(conn, (unsigned)pos);
		check();
	}
}

void mpdc_delete(int pos)
{
	if (get()) {
		mpd_run_delete(conn, (unsigned)pos);
		check();
	}
}

int mpdc_browse(const char *path, struct mpdc_item *items, int max)
{
	struct mpd_entity *e;
	int n = 0;

	if (!get() || !mpd_send_list_meta(conn, path)) {
		check();
		return 0;
	}
	while ((e = mpd_recv_entity(conn))) {
		if (n < max) {
			switch (mpd_entity_get_type(e)) {
			case MPD_ENTITY_TYPE_DIRECTORY:
				item_set(&items[n++], MPDC_DIR,
					 mpd_directory_get_path(mpd_entity_get_directory(e)), NULL, NULL);
				break;
			case MPD_ENTITY_TYPE_SONG:
				song_item(&items[n++], mpd_entity_get_song(e));
				break;
			default:		/* playlist files in the library: listed under Radio */
				break;
			}
		}
		mpd_entity_free(e);
	}
	mpd_response_finish(conn);
	check();
	return n;
}

void mpdc_play_dir(const char *path, int index)
{
	static struct mpdc_item items[MPDC_MAX_ITEMS];
	int i, n, pos = 0, start = 0;

	n = index >= 0 ? mpdc_browse(path, items, MPDC_MAX_ITEMS) : 0;
	if (!get())
		return;
	mpd_run_clear(conn);
	if (index < 0) {
		mpd_run_add(conn, *path ? path : "/");	/* the folder and its subfolders */
	} else {
		for (i = 0; i < n; i++) {
			if (items[i].kind != MPDC_SONG)
				continue;
			if (i == index)
				start = pos;
			mpd_run_add(conn, items[i].uri);
			pos++;
		}
	}
	if (check()) {
		mpd_run_play_pos(conn, (unsigned)start);
		check();
	}
}

void mpdc_update(void)
{
	if (get()) {
		mpd_run_update(conn, NULL);
		check();
	}
}

int mpdc_stations(struct mpdc_item *items, int max)
{
	if (!get() || !mpd_send_list_playlist_meta(conn, MPDC_RADIO)) {
		check();
		return 0;
	}
	return recv_songs(items, max);		/* no Radio playlist yet: none */
}

int mpdc_playlists(struct mpdc_item *items, int max)
{
	struct mpd_playlist *pl;
	int n = 0;

	if (!get() || !mpd_send_list_playlists(conn)) {
		check();
		return 0;
	}
	while ((pl = mpd_recv_playlist(conn))) {
		const char *name = mpd_playlist_get_path(pl);

		if (n < max && strcmp(name, MPDC_RADIO))
			item_set(&items[n++], MPDC_PLAYLIST, name, NULL, "PLAYLIST");
		mpd_playlist_free(pl);
	}
	mpd_response_finish(conn);
	check();
	return n;
}

/* Percent-encode what a URL may not contain as it is (spaces, [ ], non-ASCII, ...).
 * Online playlists often list file names raw (the Internet Archive's do), and curl
 * rejects some of them ("URL rejected"); MPD then stops at that song. Existing %XX
 * escapes and the URL's own punctuation are kept. false: nothing to change. */
static bool url_encode(const char *in, char *out, size_t len)
{
	static const char ok[] = "-._~:/?#@!$&'()*+,;=%";
	static const char hex[] = "0123456789ABCDEF";
	bool changed = false;
	size_t o = 0;

	for (; *in && o + 4 < len; in++) {
		unsigned char c = (unsigned char)*in;

		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr(ok, c)) {
			out[o++] = (char)c;
		} else {
			out[o++] = '%';
			out[o++] = hex[c >> 4];
			out[o++] = hex[c & 15];
			changed = true;
		}
	}
	out[o] = 0;
	return changed && !*in;		/* not cut short */
}

/* after loading an online playlist: re-add each queued URL that needs encoding, in place */
static void encode_queue(void)
{
	static struct { int pos; char uri[512]; } fix[MPDC_MAX_ITEMS];
	struct mpd_song *song;
	int n = 0, i;

	if (!mpd_send_list_queue_meta(conn))
		return;
	while ((song = mpd_recv_song(conn))) {
		const char *u = mpd_song_get_uri(song);

		if (n < MPDC_MAX_ITEMS && strstr(u, "://") && url_encode(u, fix[n].uri, sizeof(fix[n].uri)))
			fix[n++].pos = (int)mpd_song_get_pos(song);
		mpd_song_free(song);
	}
	mpd_response_finish(conn);
	for (i = 0; i < n && check(); i++) {
		mpd_run_delete(conn, (unsigned)fix[i].pos);
		mpd_run_add_id_to(conn, fix[i].uri, (unsigned)fix[i].pos);
	}
}

void mpdc_play_uri(const char *uri)
{
	char enc[512];

	if (!get())
		return;
	mpd_run_clear(conn);
	if (mpdc_is_playlist_url(uri)) {
		mpd_run_load(conn, uri);	/* its entries, not the file itself */
		if (check())
			encode_queue();
	} else {
		mpd_run_add(conn, url_encode(uri, enc, sizeof(enc)) ? enc : uri);
	}
	mpd_run_play(conn);
	check();
}

void mpdc_play_playlist(const char *name)
{
	if (!get())
		return;
	mpd_run_clear(conn);
	mpd_run_load(conn, name);
	mpd_run_play(conn);
	check();
}

bool mpdc_add_station(const char *url)
{
	if (!get())
		return false;
	mpd_run_playlist_add(conn, MPDC_RADIO, url);
	return check();
}
#endif
