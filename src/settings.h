/* settings.h -- what the setup menu changes, kept on the card in /calcterm.cfg. */
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

#define CFG_PATH "/calcterm.cfg"

void settings_defaults(void);
bool settings_load(void);			/* false: none on the card (the defaults stand) */
bool settings_save(const char **why);

#endif
