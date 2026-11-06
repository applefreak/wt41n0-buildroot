// SPDX-License-Identifier: GPL-2.0
/*
 * pcmtest: play a test tone or record through the kernel's ALSA PCM interface
 * directly (raw ioctls, no ALSA library), printing how long each period takes.
 * Useful to tell driver or clocking problems from ALSA configuration problems.
 *
 *   pcmtest play <card> <device> <channels> <rate> <seconds> [tone-Hz] [channel-mask]
 *   pcmtest rec  <card> <device> <channels> <rate> <seconds> [file.raw]
 *
 * The format is always S32_LE. On the WT41N0 the speaker is "play 0 0 4 96000"
 * with channel mask 0xc (McPDM channels 3 and 4).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sound/asound.h>

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static void set_int(struct snd_pcm_hw_params *p, int n, unsigned v)
{ struct snd_interval *i = &p->intervals[n - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL]; i->min = i->max = v; i->integer = 1; i->openmin = i->openmax = 0; }
static void set_mask(struct snd_pcm_hw_params *p, int n, unsigned bit)
{ struct snd_mask *m = &p->masks[n - SNDRV_PCM_HW_PARAM_FIRST_MASK]; memset(m, 0, sizeof(*m)); m->bits[bit >> 5] = 1u << (bit & 31); }

int main(int argc, char **argv)
{
	if (argc < 7 || (strcmp(argv[1], "play") && strcmp(argv[1], "rec"))) {
		fprintf(stderr, "usage: pcmtest play <card> <device> <channels> <rate> <seconds> [tone-Hz] [channel-mask]\n"
				"       pcmtest rec  <card> <device> <channels> <rate> <seconds> [file.raw]\n");
		return 2;
	}
	int play = !strcmp(argv[1], "play");
	int card = atoi(argv[2]), dev = atoi(argv[3]), ch = atoi(argv[4]), rate = atoi(argv[5]);
	double secs = atof(argv[6]);
	double hz = (play && argc > 7) ? atof(argv[7]) : 1000;
	unsigned chmask = (play && argc > 8) ? strtoul(argv[8], 0, 0) : ~0u;
	unsigned period = 2048, periods = 4;
	char path[64]; snprintf(path, sizeof(path), "/dev/snd/pcmC%dD%d%c", card, dev, play ? 'p' : 'c');
	int fd = open(path, O_RDWR); if (fd < 0) { perror(path); return 1; }

	struct snd_pcm_hw_params hp; memset(&hp, 0, sizeof(hp));
	for (int i = 0; i <= SNDRV_PCM_HW_PARAM_LAST_MASK - SNDRV_PCM_HW_PARAM_FIRST_MASK; i++) memset(&hp.masks[i], 0xff, sizeof(hp.masks[i]));
	for (int i = 0; i <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL; i++) { hp.intervals[i].min = 0; hp.intervals[i].max = ~0u; }
	hp.rmask = ~0u; hp.cmask = 0; hp.info = ~0u;
	set_mask(&hp, SNDRV_PCM_HW_PARAM_ACCESS, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
	set_mask(&hp, SNDRV_PCM_HW_PARAM_FORMAT, SNDRV_PCM_FORMAT_S32_LE);
	set_mask(&hp, SNDRV_PCM_HW_PARAM_SUBFORMAT, SNDRV_PCM_SUBFORMAT_STD);
	set_int(&hp, SNDRV_PCM_HW_PARAM_CHANNELS, ch);
	set_int(&hp, SNDRV_PCM_HW_PARAM_RATE, rate);
	set_int(&hp, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, period);
	set_int(&hp, SNDRV_PCM_HW_PARAM_PERIODS, periods);
	if (ioctl(fd, SNDRV_PCM_IOCTL_HW_PARAMS, &hp) < 0) { perror("HW_PARAMS"); return 1; }

	struct snd_pcm_sw_params sp; memset(&sp, 0, sizeof(sp));
	sp.tstamp_mode = SNDRV_PCM_TSTAMP_NONE; sp.period_step = 1; sp.avail_min = period;
	sp.start_threshold = play ? period * 2 : 1; sp.stop_threshold = period * periods;
	sp.silence_threshold = 0; sp.silence_size = 0; sp.boundary = period * periods;
	while (sp.boundary * 2 <= 0x7fffffffUL - period * periods) sp.boundary *= 2;
	if (ioctl(fd, SNDRV_PCM_IOCTL_SW_PARAMS, &sp) < 0) { perror("SW_PARAMS"); return 1; }
	if (ioctl(fd, SNDRV_PCM_IOCTL_PREPARE) < 0) { perror("PREPARE"); return 1; }

	size_t bytes = (size_t)period * ch * 4; int32_t *buf = malloc(bytes);
	long total = (long)(secs * rate), done = 0; double t0 = now(), tl = t0; int n = 0, errs = 0;
	FILE *out = (!play && argc > 7) ? fopen(argv[7], "wb") : NULL;
	if (!play && ioctl(fd, SNDRV_PCM_IOCTL_START) < 0) perror("START");
	while (done < total) {
		if (play) for (unsigned f = 0; f < period; f++) {
			double t = (double)(done + f) / rate; int on = fmod(t, 1.0) < 0.6;
			int32_t v = on ? (int32_t)(0.3 * 2147483647.0 * sin(2 * M_PI * hz * t)) : 0;
			for (int c = 0; c < ch; c++) buf[f * ch + c] = (chmask >> c & 1) ? v : 0;
		}
		ssize_t r = play ? write(fd, buf, bytes) : read(fd, buf, bytes);
		double t = now();
		if (r < 0) {
			printf("period %d: %s error %d (%s) after %.3f s\n", n, play ? "write" : "read", errno, strerror(errno), t - tl);
			if (++errs > 5) break;
			if (ioctl(fd, SNDRV_PCM_IOCTL_PREPARE) < 0) { perror("PREPARE"); break; }
			if (!play) ioctl(fd, SNDRV_PCM_IOCTL_START);
			tl = now(); continue;
		}
		if (n < 6 || n % 20 == 0) printf("period %d: %zd bytes in %.4f s (expected %.4f)\n", n, r, t - tl, (double)period / rate);
		if (out) fwrite(buf, 1, r, out);
		tl = t; n++; done += r / (ch * 4);
	}
	if (play) ioctl(fd, SNDRV_PCM_IOCTL_DRAIN);
	printf("%s %ld frames in %.2f s wall (%.2f s of audio), %d errors\n", play ? "played" : "captured", done, now() - t0, (double)done / rate, errs);
	if (out) fclose(out);
	close(fd); return errs ? 1 : 0;
}
