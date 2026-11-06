/* vt.h -- a VT100 terminal (with its VT52 mode), apart from any hardware.
 *
 * The host's bytes go in through vt_input(); the screen is the cells in
 * struct vt, with a dirty mark on each one that has changed since it was
 * last drawn.  What the terminal says back to the host (answers to its
 * questions, and the keys typed) goes out through the output callback.
 * Nothing here knows about the PicoCalc, so it runs on Linux for the tests.
 */
#ifndef VT_H
#define VT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VT_MAX_COLS 80		/* 320 pixels of 4-pixel glyphs; 64 of 5-pixel ones */
#define VT_ROWS 32		/* 320 pixels of 10-pixel glyphs */

/* attributes */
#define A_BOLD	0x01
#define A_UNDER	0x02
#define A_BLINK	0x04
#define A_REV	0x08

/* colours: 0-7 the ANSI ones, 8-15 their bright forms */
#define C_DEF_FG 7
#define C_DEF_BG 0

/* The DEC Special Graphics set (line drawing) comes out as these codes,
   0x5F-0x7E in the set at G_BASE + 0..31; the font has glyphs there. */
#define G_BASE 0x80

typedef struct {
	uint8_t ch;
	uint8_t attr;
	uint8_t fg, bg;
} vt_cell;

typedef void (*vt_out_fn)(const uint8_t *s, size_t n, void *ctx);
typedef void (*vt_bell_fn)(void *ctx);

typedef struct {
	int x, y;
	uint8_t attr, fg, bg;
	bool origin, wrapnext;
	int gl;			/* 0: G0, 1: G1 */
	char g[2];		/* 'B' ASCII, '0' DEC graphics, 'A' UK */
} vt_saved;

typedef struct vt {
	int cols;			/* 64 or 80 */
	vt_cell cell[VT_ROWS][VT_MAX_COLS];
	bool dirty[VT_ROWS];		/* a row with something to draw */
	uint8_t dlo[VT_ROWS], dhi[VT_ROWS];	/* the first and last of its cells to draw */
	int hw_scroll;			/* whole-screen scrolls up not yet drawn */

	int x, y;			/* the cursor */
	bool wrapnext;			/* at the right margin, with a character pending */
	uint8_t attr, fg, bg;		/* for what's written next */
	int top, bot;			/* the scrolling region, inclusive */
	bool tabs[VT_MAX_COLS];
	int gl;
	char g[2];
	vt_saved saved;

	/* modes */
	bool ansi;		/* false: VT52 */
	bool cursor_keys;	/* DECCKM: application cursor keys */
	bool keypad_app;	/* DECKPAM */
	bool origin;		/* DECOM */
	bool autowrap;		/* DECAWM */
	bool insert;		/* IRM */
	bool newline;		/* LNM: LF also returns, Enter sends CR LF */
	bool reverse_screen;	/* DECSCNM */
	bool cursor_on;		/* DECTCEM */

	/* the parser */
	int state;
	int params[16];
	int nparams;
	bool priv;		/* a '?' after CSI */
	char inter;		/* an intermediate byte */

	/* settings from outside */
	bool bs_sends_del;	/* Backspace sends DEL (0x7F) rather than BS */
	bool local_echo;

	vt_out_fn out;
	vt_bell_fn bell;
	void *ctx;
} vt;

void vt_init(vt *t, vt_out_fn out, vt_bell_fn bell, void *ctx);	/* 64 columns */
void vt_reset(vt *t);			/* RIS: the power-on state */
void vt_input(vt *t, const uint8_t *s, size_t n);
void vt_touch_all(vt *t);		/* draw it all again */

/* The keys, as the terminal sends them.  Letters, digits, punctuation and
   control characters go through vt_key_char(); the others are these. */
enum {
	VK_UP = 0x100, VK_DOWN, VK_RIGHT, VK_LEFT,
	VK_ENTER, VK_BS, VK_TAB, VK_ESC,
	VK_HOME, VK_END, VK_INS, VK_DEL, VK_PGUP, VK_PGDN,
	VK_PF1, VK_PF2, VK_PF3, VK_PF4,		/* F1-F4 */
	VK_F5, VK_F6, VK_F7, VK_F8, VK_F9, VK_F10,
};
void vt_key(vt *t, int key);
void vt_key_char(vt *t, uint8_t c, bool alt);	/* Alt: ESC first, as xterm's meta */

/* for the tests: a row as text, the line-drawing set shown as '+', '-', '|' */
void vt_row_text(const vt *t, int row, char *buf);	/* buf: VT_MAX_COLS + 1 */
void vt_set_cols(vt *t, int cols);	/* 64 or 80: what's on the screen stays where it is */

#endif
