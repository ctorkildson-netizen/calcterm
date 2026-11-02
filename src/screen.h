/* screen.h -- the terminal's cells onto the PicoCalc's LCD. */
#ifndef SCREEN_H
#define SCREEN_H

#include <stdbool.h>
#include "vt.h"

void screen_init(void);
void screen_draw(vt *t, bool sticky);		/* what's changed, and the cursor */
void screen_flash(void);			/* the visual bell */

/* for the menu, drawn over the terminal: text at a cell, in colours 0-15 */
void screen_text(int col, int row, const char *s, int fg, int bg);
void screen_fill(int col, int row, int w, int h, int bg);

#endif
