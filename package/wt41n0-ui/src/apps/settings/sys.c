/* Brightness (backlight in sysfs) and volume (ALSA: Handsfree speaker and Headset). */
#include <stdio.h>
#include "settings.h"

#define BACKLIGHT "/sys/class/backlight/backlight/"

#ifndef WT41N0_UI_HOST
#include <alsa/asoundlib.h>

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

int sys_brightness(int *max)
{
	*max = read_int(BACKLIGHT "max_brightness");
	return read_int(BACKLIGHT "brightness");
}

void sys_set_brightness(int value)
{
	FILE *f = fopen(BACKLIGHT "brightness", "w");

	if (f) {
		fprintf(f, "%d\n", value);
		fclose(f);
	}
}

static snd_mixer_t *mixer;

static snd_mixer_elem_t *elem(const char *name)
{
	snd_mixer_selem_id_t *id;

	if (!mixer) {
		if (snd_mixer_open(&mixer, 0) || snd_mixer_attach(mixer, "default") ||
		    snd_mixer_selem_register(mixer, NULL, NULL) || snd_mixer_load(mixer)) {
			if (mixer)
				snd_mixer_close(mixer);
			mixer = NULL;
			return NULL;
		}
	}
	snd_mixer_selem_id_alloca(&id);
	snd_mixer_selem_id_set_name(id, name);
	return snd_mixer_find_selem(mixer, id);
}

int sys_volume(void)
{
	snd_mixer_elem_t *e = elem("Handsfree");
	long min, max, v;

	if (!e || snd_mixer_selem_get_playback_volume_range(e, &min, &max) ||
	    snd_mixer_selem_get_playback_volume(e, SND_MIXER_SCHN_FRONT_LEFT, &v) || max <= min)
		return -1;
	return (int)((v - min) * 100 / (max - min));
}

void sys_set_volume(int percent)
{
	static const char *const names[] = { "Handsfree", "Headset" };
	unsigned int i;

	for (i = 0; i < 2; i++) {
		snd_mixer_elem_t *e = elem(names[i]);
		long min, max;

		if (e && !snd_mixer_selem_get_playback_volume_range(e, &min, &max))
			snd_mixer_selem_set_playback_volume_all(e, min + (max - min) * percent / 100);
	}
}
#else
static int host_brightness = 14, host_volume = 28;
int sys_brightness(int *max) { *max = 17; return host_brightness; }
void sys_set_brightness(int value) { host_brightness = value; }
int sys_volume(void) { return host_volume; }
void sys_set_volume(int percent) { host_volume = percent; }
#endif
