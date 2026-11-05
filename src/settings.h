/* settings.h -- what the setup menu changes, kept on the card in /calcterm/calcterm.cfg. */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>
#include "link.h"

typedef struct {
	link_config link;
	bool echo;		/* local echo */
	bool newline;		/* Enter sends CR LF, and LF also returns */
	bool bs_del;		/* Backspace sends DEL (else BS) */
} settings;

extern settings cfg;

#define CFG_DIR "/calcterm"
#define CFG_PATH "/calcterm/calcterm.cfg"	/* a folder of its own, as the PicoCalc's programs do */
#define CFG_OLD "/calcterm.cfg"		/* where 0.1's test builds kept it: read if it's all there is */

void settings_defaults(void);
bool settings_load(void);			/* false: none on the card (the defaults stand) */
bool settings_save(const char **why);

#endif
