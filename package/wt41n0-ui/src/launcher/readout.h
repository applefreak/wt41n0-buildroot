/* Live data for the home screen: the tile's corner value and the readout strip */
#ifndef LAUNCHER_READOUT_H
#define LAUNCHER_READOUT_H

#include "apps.h"

struct readout {
	char meta[12];		/* tile corner: "-58", "87%", "N/C" */
	char line[48];		/* readout strip text */
	int level;		/* 8-segment bar: 0..8, -1 for none */
};

void readout_get(struct app *app, struct readout *r);

#endif
