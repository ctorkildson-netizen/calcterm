/* shot.h -- screenshots of the terminal (Alt+F4). */
#ifndef SHOT_H
#define SHOT_H

#include "vt.h"

void shot_take(const vt *t);	/* to /screenshots; says on the bottom line how it went */

#endif
