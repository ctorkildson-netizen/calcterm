/* menu.h -- the setup menu (Alt+F5), over the terminal. */
#ifndef MENU_H
#define MENU_H

#include "vt.h"

void menu_run(vt *t);	/* returns when it's closed; the terminal's to be drawn again */
void apply_settings(vt *t);

#endif
