/* keys.h -- the PicoCalc's keyboard, read often, with held keys repeating. */
#ifndef KEYS_H
#define KEYS_H

#include <stdbool.h>
#include "vt.h"

/* keys_get() gives a character (0x00-0xFF, control characters already
   made), one of vt.h's VK_ keys, or one of these; KF_ALT is or-ed in when
   Alt was held.  -1: nothing. */
enum { K_MENU = 0x200, K_BRK };
#define KF_ALT 0x10000

void keys_init(void);
void keys_poll(void);
int keys_get(void);
bool keys_sticky(void);		/* a Ctrl tapped on its own, waiting for its letter */

#endif
