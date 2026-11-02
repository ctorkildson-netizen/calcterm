/* menu.h -- F5's setup menu, over the terminal. */
#ifndef MENU_H
#define MENU_H

#include "vt.h"

void menu_run(vt *t);	/* returns when it's closed; the terminal's to be drawn again */
void apply_settings(vt *t);

#endif
