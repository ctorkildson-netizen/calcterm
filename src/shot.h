/* shot.h -- screenshots of the terminal (Alt+F4). */
#ifndef SHOT_H
#define SHOT_H

#include "vt.h"

void shot_take(const vt *t);	/* to /screenshots; says on the bottom line how it went */
/* what came from the host, as the terminal got it: the last 16 KB of it
   goes with each screenshot as a .raw file, for finding out why something
   drew wrongly */
void shot_record(const uint8_t *s, size_t n);

#endif
