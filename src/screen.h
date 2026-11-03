/* screen.h -- the terminal's cells onto the PicoCalc's LCD. */
#ifndef SCREEN_H
#define SCREEN_H

#include <stdbool.h>
#include <stdint.h>
#include "vt.h"

void screen_init(void);
void screen_draw(vt *t, bool sticky);		/* what's changed, and the cursor */
void screen_flash(void);			/* the visual bell */
void screen_note(const char *s, int ms);	/* a message on the bottom line for a while */
void screen_render_row(const vt *t, int row, uint16_t *buf);	/* a row as shown, 320x10, for screenshots */

/* for the menu, drawn over the terminal: text at a cell, in colours 0-15 */
void screen_text(int col, int row, const char *s, int fg, int bg);
void screen_fill(int col, int row, int w, int h, int bg);

#endif
