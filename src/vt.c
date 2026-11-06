/* vt.c -- a VT100 terminal (with its VT52 mode), apart from any hardware.
 *
 * What it understands is the VT100's own repertoire (DEC STD 070 and the
 * VT100 User Guide), plus the few things a Linux host sends a "vt100" or
 * "xterm" anyway: colours, insert and delete of characters and lines, the
 * alternate screen, and UTF-8 for the line-drawing characters.  Anything
 * else is read and ignored, so it never shows up as litter on the screen.
 */
#include <string.h>
#include <stdio.h>
#include "vt.h"

enum { S_GROUND, S_ESC, S_ESC_INTER, S_CSI, S_STRING, S_STRING_ESC,
       S_VT52_ESC, S_VT52_Y1, S_VT52_Y2, S_UTF8 };

/* the alternate screen (xterm's 1049): the main one is kept here */
static vt_cell alt_save[VT_ROWS][VT_MAX_COLS];

/* UTF-8 being put together */
static uint32_t u_cp;
static int u_left;
static int vt52_row;

/* ------------------------------------------------------------ the screen */

static void touch(vt *t, int row, int col)
{
	if (!t->dirty[row] || col < t->dlo[row]) t->dlo[row] = (uint8_t)col;
	if (!t->dirty[row] || col > t->dhi[row]) t->dhi[row] = (uint8_t)col;
	t->dirty[row] = true;
}

static void touch_row(vt *t, int row)
{
	t->dirty[row] = true;
	t->dlo[row] = 0;
	t->dhi[row] = (uint8_t)(t->cols - 1);
}

void vt_touch_all(vt *t)
{
	for (int r = 0; r < VT_ROWS; r++) touch_row(t, r);
	t->hw_scroll = 0;	/* it's all drawn afresh, scrolled or not */
}

static vt_cell blank(const vt *t)
{
	vt_cell c = { ' ', 0, t->fg, t->bg };	/* erased in the current background */
	c.fg = C_DEF_FG;
	return c;
}

static void clear_cells(vt *t, int row, int from, int to)	/* to: exclusive */
{
	vt_cell b = blank(t);
	for (int c = from; c < to; c++) {
		t->cell[row][c] = b;
		touch(t, row, c);
	}
}

/* rows top..bot move up n, blank rows coming in at the bottom */
static void scroll_up(vt *t, int top, int bot, int n)
{
	int h = bot - top + 1;
	if (n > h) n = h;
	if (n <= 0) return;
	memmove(&t->cell[top], &t->cell[top + n], sizeof t->cell[0] * (h - n));
	if (top == 0 && bot == VT_ROWS - 1) {
		/* the whole screen: the display can roll it, so carry the marks
		   of what still has to be drawn along with the rows */
		memmove(&t->dirty[0], &t->dirty[n], sizeof t->dirty[0] * (h - n));
		memmove(&t->dlo[0], &t->dlo[n], sizeof t->dlo[0] * (h - n));
		memmove(&t->dhi[0], &t->dhi[n], sizeof t->dhi[0] * (h - n));
		t->hw_scroll += n;
		if (t->hw_scroll >= VT_ROWS) vt_touch_all(t);
	} else {
		for (int r = top; r <= bot - n; r++) touch_row(t, r);
	}
	for (int r = bot - n + 1; r <= bot; r++) {
		clear_cells(t, r, 0, t->cols);
		touch_row(t, r);
	}
}

static void scroll_down(vt *t, int top, int bot, int n)
{
	int h = bot - top + 1;
	if (n > h) n = h;
	if (n <= 0) return;
	memmove(&t->cell[top + n], &t->cell[top], sizeof t->cell[0] * (h - n));
	for (int r = top + n; r <= bot; r++) touch_row(t, r);
	for (int r = top; r < top + n; r++) clear_cells(t, r, 0, t->cols);
}

static int clamp(int v, int lo, int hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

static void move_to(vt *t, int x, int y)
{
	t->x = clamp(x, 0, t->cols - 1);
	t->y = clamp(y, 0, VT_ROWS - 1);
	t->wrapnext = false;
}

/* CUP's row: within the scrolling region when origin mode is on */
static void move_to_origin(vt *t, int x, int y)
{
	if (t->origin) move_to(t, x, clamp(y + t->top, t->top, t->bot));
	else move_to(t, x, y);
}

static void index_down(vt *t)		/* IND, and LF */
{
	if (t->y == t->bot) scroll_up(t, t->top, t->bot, 1);
	else if (t->y < VT_ROWS - 1) t->y++;
}

static void index_up(vt *t)		/* RI */
{
	if (t->y == t->top) scroll_down(t, t->top, t->bot, 1);
	else if (t->y > 0) t->y--;
}

/* ------------------------------------------------------------ writing */

/* the DEC Special Graphics set, from Unicode, for hosts that send UTF-8 */
static int from_unicode(uint32_t u)
{
	if (u < 0x80) return (int)u;
	switch (u) {
	case 0xA0: return ' ';
	case 0xA3: return G_BASE + 0x7D - 0x5F;		/* £ */
	case 0xB0: return G_BASE + 0x66 - 0x5F;		/* ° */
	case 0xB1: return G_BASE + 0x67 - 0x5F;		/* ± */
	case 0xB7: case 0x2022: case 0x22C5: return G_BASE + 0x7E - 0x5F;	/* · */
	case 0x3C0: return G_BASE + 0x7B - 0x5F;	/* π */
	case 0x2260: return G_BASE + 0x7C - 0x5F;	/* ≠ */
	case 0x2264: return G_BASE + 0x79 - 0x5F;	/* ≤ */
	case 0x2265: return G_BASE + 0x7A - 0x5F;	/* ≥ */
	case 0x25C6: case 0x2666: return G_BASE + 0x60 - 0x5F;	/* ◆ */
	case 0x2591: case 0x2592: case 0x2593: return G_BASE + 0x61 - 0x5F;	/* ▒ */
	case 0x2018: case 0x2019: case 0x201A: case 0x2032: return '\'';
	case 0x201C: case 0x201D: case 0x201E: case 0x2033: return '"';
	case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2212: return '-';
	case 0x2026: return '.';
	case 0x2039: case 0x25C0: case 0x25C1: case 0x25C2: case 0x25C3: case 0x2190: return '<';
	case 0x203A: case 0x25B6: case 0x25B7: case 0x25B8: case 0x25B9: case 0x2192: return '>';
	case 0x25B2: case 0x25B3: case 0x25B4: case 0x25B5: case 0x2191: return '^';
	case 0x25BC: case 0x25BD: case 0x25BE: case 0x25BF: case 0x2193: return 'v';
	case 0x2713: case 0x2714: return '*';
	}
	if (u >= 0x2500 && u <= 0x257F) {
		/* box drawing: light, heavy, double and rounded all as the one line */
		static const struct { uint16_t lo, hi; uint8_t dec; } box[] = {
			{ 0x2500, 0x2501, 0x71 }, { 0x2502, 0x2503, 0x78 },
			{ 0x2504, 0x2505, 0x71 }, { 0x2506, 0x2507, 0x78 },
			{ 0x2508, 0x2509, 0x71 }, { 0x250A, 0x250B, 0x78 },
			{ 0x250C, 0x250F, 0x6C }, { 0x2510, 0x2513, 0x6B },
			{ 0x2514, 0x2517, 0x6D }, { 0x2518, 0x251B, 0x6A },
			{ 0x251C, 0x2523, 0x74 }, { 0x2524, 0x252B, 0x75 },
			{ 0x252C, 0x2533, 0x77 }, { 0x2534, 0x253B, 0x76 },
			{ 0x253C, 0x254B, 0x6E }, { 0x254C, 0x254D, 0x71 },
			{ 0x254E, 0x254F, 0x78 }, { 0x2550, 0x2550, 0x71 },
			{ 0x2551, 0x2551, 0x78 }, { 0x2552, 0x2554, 0x6C },
			{ 0x2555, 0x2557, 0x6B }, { 0x2558, 0x255A, 0x6D },
			{ 0x255B, 0x255D, 0x6A }, { 0x255E, 0x2560, 0x74 },
			{ 0x2561, 0x2563, 0x75 }, { 0x2564, 0x2566, 0x77 },
			{ 0x2567, 0x2569, 0x76 }, { 0x256A, 0x256C, 0x6E },
			{ 0x256D, 0x256D, 0x6C }, { 0x256E, 0x256E, 0x6B },
			{ 0x256F, 0x256F, 0x6A }, { 0x2570, 0x2570, 0x6D },
			{ 0x2574, 0x2574, 0x71 }, { 0x2575, 0x2575, 0x78 },
			{ 0x2576, 0x2576, 0x71 }, { 0x2577, 0x2577, 0x78 },
			{ 0x2578, 0x2578, 0x71 }, { 0x2579, 0x2579, 0x78 },
			{ 0x257A, 0x257A, 0x71 }, { 0x257B, 0x257B, 0x78 },
			{ 0x257C, 0x257C, 0x71 }, { 0x257D, 0x257D, 0x78 },
			{ 0x257E, 0x257E, 0x71 }, { 0x257F, 0x257F, 0x78 },
		};
		for (size_t i = 0; i < sizeof box / sizeof box[0]; i++)
			if (u >= box[i].lo && u <= box[i].hi) return G_BASE + box[i].dec - 0x5F;
	}
	return '?';
}

static void put_glyph(vt *t, uint8_t ch)
{
	if (t->wrapnext && t->autowrap) {
		t->x = 0;
		index_down(t);
	}
	t->wrapnext = false;
	vt_cell *line = t->cell[t->y];
	if (t->insert) {
		memmove(&line[t->x + 1], &line[t->x], sizeof line[0] * (t->cols - 1 - t->x));
		for (int c = t->x; c < t->cols; c++) touch(t, t->y, c);
	}
	vt_cell c = { ch, t->attr, t->fg, t->bg };
	line[t->x] = c;
	touch(t, t->y, t->x);
	if (t->x == t->cols - 1) t->wrapnext = t->autowrap;
	else t->x++;
}

static void put_char(vt *t, uint8_t c)	/* c: 0x20-0x7E */
{
	char set = t->g[t->gl];
	if (set == '0' && c >= 0x5F && c <= 0x7E) put_glyph(t, G_BASE + c - 0x5F);
	else if (set == 'A' && c == '#') put_glyph(t, G_BASE + 0x7D - 0x5F);	/* the UK set's £ */
	else put_glyph(t, c);
}

/* ------------------------------------------------------------ answering */

static void say(vt *t, const char *s)
{
	if (t->out) t->out((const uint8_t *)s, strlen(s), t->ctx);
}

/* ------------------------------------------------------------ the parts */

static void save_cursor(vt *t)
{
	vt_saved *s = &t->saved;
	s->x = t->x; s->y = t->y;
	s->attr = t->attr; s->fg = t->fg; s->bg = t->bg;
	s->origin = t->origin; s->wrapnext = t->wrapnext;
	s->gl = t->gl; s->g[0] = t->g[0]; s->g[1] = t->g[1];
}

static void restore_cursor(vt *t)
{
	vt_saved *s = &t->saved;
	t->attr = s->attr; t->fg = s->fg; t->bg = s->bg;
	t->origin = s->origin;
	t->gl = s->gl; t->g[0] = s->g[0]; t->g[1] = s->g[1];
	move_to(t, s->x, s->y);
	t->wrapnext = s->wrapnext;
}

static void reset_tabs(vt *t)
{
	for (int c = 0; c < t->cols; c++) t->tabs[c] = c % 8 == 0 && c > 0;
}

static void soft_reset(vt *t)		/* DECSTR, and part of RIS */
{
	t->cursor_on = true;
	t->insert = false;
	t->origin = false;
	t->autowrap = true;
	t->keypad_app = false;
	t->cursor_keys = false;
	t->top = 0;
	t->bot = VT_ROWS - 1;
	t->attr = 0;
	t->fg = C_DEF_FG;
	t->bg = C_DEF_BG;
	t->gl = 0;
	t->g[0] = t->g[1] = 'B';
	t->wrapnext = false;
	save_cursor(t);
	t->saved.x = t->saved.y = 0;
}

static bool on_alt;

static void alt_screen(vt *t, bool on, bool keep_cursor)
{
	if (on == on_alt) return;
	if (on) {
		if (keep_cursor) save_cursor(t);
		memcpy(alt_save, t->cell, sizeof alt_save);
		for (int r = 0; r < VT_ROWS; r++) clear_cells(t, r, 0, t->cols);
	} else {
		memcpy(t->cell, alt_save, sizeof alt_save);
		if (keep_cursor) restore_cursor(t);
	}
	on_alt = on;
	vt_touch_all(t);
}

void vt_reset(vt *t)
{
	t->state = S_GROUND;
	t->ansi = true;
	t->newline = false;
	t->reverse_screen = false;
	on_alt = false;
	soft_reset(t);
	reset_tabs(t);
	for (int r = 0; r < VT_ROWS; r++) clear_cells(t, r, 0, t->cols);
	move_to(t, 0, 0);
	vt_touch_all(t);
}

void vt_init(vt *t, vt_out_fn out, vt_bell_fn bell, void *ctx)
{
	memset(t, 0, sizeof *t);
	t->cols = 64;
	t->out = out;
	t->bell = bell;
	t->ctx = ctx;
	t->bs_sends_del = true;		/* Linux's erase character is DEL */
	vt_reset(t);
}

static int param(const vt *t, int i, int def)
{
	if (i >= t->nparams || t->params[i] <= 0) return def;
	return t->params[i];
}

static uint8_t colour_256(int n)	/* the nearest of the sixteen */
{
	if (n < 16) return (uint8_t)n;
	int r, g, b;
	if (n >= 232) {				/* the greys */
		int v = (n - 232) * 10 + 8;
		return v < 64 ? 0 : v < 160 ? 8 : v < 216 ? 7 : 15;
	}
	n -= 16;
	r = n / 36; g = (n / 6) % 6; b = n % 6;
	int hi = r > 3 || g > 3 || b > 3;
	uint8_t c = (r >= 2 ? 1 : 0) | (g >= 2 ? 2 : 0) | (b >= 2 ? 4 : 0);
	if (c == 0 && (r || g || b)) c = 0, hi = 1;	/* dark: grey */
	return c | (hi ? 8 : 0);
}

static uint8_t colour_rgb(int r, int g, int b)
{
	int hi = r > 191 || g > 191 || b > 191;
	uint8_t c = (r > 96 ? 1 : 0) | (g > 96 ? 2 : 0) | (b > 96 ? 4 : 0);
	return c | (hi ? 8 : 0);
}

static void sgr(vt *t)
{
	if (t->nparams == 0) t->params[t->nparams++] = 0;
	for (int i = 0; i < t->nparams; i++) {
		int p = t->params[i];
		if (p < 0) p = 0;
		if (p == 0) { t->attr = 0; t->fg = C_DEF_FG; t->bg = C_DEF_BG; }
		else if (p == 1) t->attr |= A_BOLD;
		else if (p == 4) t->attr |= A_UNDER;
		else if (p == 5 || p == 6) t->attr |= A_BLINK;
		else if (p == 7) t->attr |= A_REV;
		else if (p == 22) t->attr &= ~A_BOLD;
		else if (p == 24) t->attr &= ~A_UNDER;
		else if (p == 25) t->attr &= ~A_BLINK;
		else if (p == 27) t->attr &= ~A_REV;
		else if (p >= 30 && p <= 37) t->fg = p - 30;
		else if (p == 39) t->fg = C_DEF_FG;
		else if (p >= 40 && p <= 47) t->bg = p - 40;
		else if (p == 49) t->bg = C_DEF_BG;
		else if (p >= 90 && p <= 97) t->fg = p - 90 + 8;
		else if (p >= 100 && p <= 107) t->bg = p - 100 + 8;
		else if (p == 38 || p == 48) {
			uint8_t c;
			if (i + 2 < t->nparams && t->params[i + 1] == 5) {
				c = colour_256(t->params[i + 2] < 0 ? 0 : t->params[i + 2] & 0xFF);
				i += 2;
			} else if (i + 4 < t->nparams && t->params[i + 1] == 2) {
				c = colour_rgb(t->params[i + 2], t->params[i + 3], t->params[i + 4]);
				i += 4;
			} else {
				break;
			}
			if (p == 38) t->fg = c; else t->bg = c;
		}
	}
}

static void set_mode(vt *t, bool on)
{
	for (int i = 0; i < t->nparams; i++) {
		int p = t->params[i];
		if (!t->priv) {
			if (p == 4) t->insert = on;
			else if (p == 20) t->newline = on;
			continue;
		}
		switch (p) {
		case 1: t->cursor_keys = on; break;
		case 2: if (!on) { t->ansi = false; t->state = S_GROUND; } break;	/* to VT52 */
		case 3:		/* 80/132 columns: can't, but clear as the VT100 does */
			for (int r = 0; r < VT_ROWS; r++) clear_cells(t, r, 0, t->cols);
			t->top = 0; t->bot = VT_ROWS - 1;
			move_to(t, 0, 0);
			break;
		case 5:
			if (t->reverse_screen != on) { t->reverse_screen = on; vt_touch_all(t); }
			break;
		case 6: t->origin = on; move_to_origin(t, 0, 0); break;
		case 7: t->autowrap = on; if (!on) t->wrapnext = false; break;
		case 25: t->cursor_on = on; break;
		case 47: case 1047: alt_screen(t, on, false); break;
		case 1048: if (on) save_cursor(t); else restore_cursor(t); break;
		case 1049: alt_screen(t, on, true); break;
		}
	}
}

static void csi_dispatch(vt *t, uint8_t f)
{
	int n = param(t, 0, 1);
	if (t->inter == '!' && f == 'p') { soft_reset(t); return; }	/* DECSTR */
	if (t->inter) return;			/* nothing else with an intermediate */
	if (t->priv && f != 'h' && f != 'l') {
		/* ?-private: only the modes; '>' and '=' (DA2, xterm's
		   questions) a VT100 doesn't answer */
		return;
	}
	switch (f) {
	case 'A': move_to(t, t->x, t->y >= t->top ? clamp(t->y - n, t->top, VT_ROWS) : t->y - n); break;
	case 'B': move_to(t, t->x, t->y <= t->bot ? clamp(t->y + n, 0, t->bot) : t->y + n); break;
	case 'C': case 'a': move_to(t, t->x + n, t->y); break;
	case 'D': move_to(t, t->x - n, t->y); break;
	case 'E': move_to(t, 0, t->y <= t->bot ? clamp(t->y + n, 0, t->bot) : t->y + n); break;
	case 'F': move_to(t, 0, t->y >= t->top ? clamp(t->y - n, t->top, VT_ROWS) : t->y - n); break;
	case 'G': case '`': move_to(t, n - 1, t->y); break;
	case 'H': case 'f': move_to_origin(t, param(t, 1, 1) - 1, n - 1); break;
	case 'd': move_to_origin(t, t->x, n - 1); break;
	case 'e': move_to(t, t->x, t->y + n); break;
	case 'J': {
		int p = param(t, 0, 0);
		if (p == 0) {
			clear_cells(t, t->y, t->x, t->cols);
			for (int r = t->y + 1; r < VT_ROWS; r++) clear_cells(t, r, 0, t->cols);
		} else if (p == 1) {
			for (int r = 0; r < t->y; r++) clear_cells(t, r, 0, t->cols);
			clear_cells(t, t->y, 0, t->x + 1);
		} else if (p == 2 || p == 3) {
			for (int r = 0; r < VT_ROWS; r++) clear_cells(t, r, 0, t->cols);
		}
		t->wrapnext = false;
		break;
	}
	case 'K': {
		int p = param(t, 0, 0);
		if (p == 0) clear_cells(t, t->y, t->x, t->cols);
		else if (p == 1) clear_cells(t, t->y, 0, t->x + 1);
		else if (p == 2) clear_cells(t, t->y, 0, t->cols);
		t->wrapnext = false;
		break;
	}
	case 'L':
		if (t->y >= t->top && t->y <= t->bot) { scroll_down(t, t->y, t->bot, n); t->x = 0; t->wrapnext = false; }
		break;
	case 'M':
		if (t->y >= t->top && t->y <= t->bot) { scroll_up(t, t->y, t->bot, n); t->x = 0; t->wrapnext = false; }
		break;
	case 'P': {
		vt_cell *line = t->cell[t->y];
		if (n > t->cols - t->x) n = t->cols - t->x;
		memmove(&line[t->x], &line[t->x + n], sizeof line[0] * (t->cols - t->x - n));
		for (int c = t->x; c < t->cols - n; c++) touch(t, t->y, c);
		clear_cells(t, t->y, t->cols - n, t->cols);
		t->wrapnext = false;
		break;
	}
	case '@': {
		vt_cell *line = t->cell[t->y];
		if (n > t->cols - t->x) n = t->cols - t->x;
		memmove(&line[t->x + n], &line[t->x], sizeof line[0] * (t->cols - t->x - n));
		for (int c = t->x + n; c < t->cols; c++) touch(t, t->y, c);
		clear_cells(t, t->y, t->x, t->x + n);
		t->wrapnext = false;
		break;
	}
	case 'X':
		clear_cells(t, t->y, t->x, t->x + n > t->cols ? t->cols : t->x + n);
		t->wrapnext = false;
		break;
	case 'Z':				/* CBT: back n tab stops */
		for (int i = 0; i < n && t->x > 0; i++) {
			int x = t->x - 1;
			while (x > 0 && !t->tabs[x]) x--;
			t->x = x;
		}
		t->wrapnext = false;
		break;
	case 'S': scroll_up(t, t->top, t->bot, n); break;
	case 'T': scroll_down(t, t->top, t->bot, n); break;
	case 'c':
		if (param(t, 0, 0) == 0) say(t, "\x1b[?1;2c");	/* a VT100 with the advanced video option */
		break;
	case 'g':
		if (param(t, 0, 0) == 0) t->tabs[t->x] = false;
		else if (param(t, 0, 0) == 3) memset(t->tabs, 0, sizeof t->tabs);
		break;
	case 'h': set_mode(t, true); break;
	case 'l': set_mode(t, false); break;
	case 'm': sgr(t); break;
	case 'n':
		if (param(t, 0, 0) == 5) say(t, "\x1b[0n");
		else if (param(t, 0, 0) == 6) {
			char b[32];
			int row = t->origin ? t->y - t->top : t->y;
			snprintf(b, sizeof b, "\x1b[%d;%dR", row + 1, t->x + 1);
			say(t, b);
		}
		break;
	case 'r': {
		int top = param(t, 0, 1) - 1, bot = param(t, 1, VT_ROWS) - 1;
		if (bot > VT_ROWS - 1) bot = VT_ROWS - 1;
		if (top < bot) {
			t->top = top;
			t->bot = bot;
			move_to_origin(t, 0, 0);
		}
		break;
	}
	case 's': save_cursor(t); break;
	case 'u': restore_cursor(t); break;
	case 't':
		if (param(t, 0, 0) == 18) {	/* xterm: the text area's size */
			char b[24];
			snprintf(b, sizeof b, "\x1b[8;%d;%dt", VT_ROWS, t->cols);
			say(t, b);
		}
		break;
	case 'x':				/* DECREQTPARM */
		if (param(t, 0, 0) <= 1) {
			char b[32];
			snprintf(b, sizeof b, "\x1b[%d;1;1;112;112;1;0x", param(t, 0, 0) + 2);
			say(t, b);
		}
		break;
	}
}

static void esc_dispatch(vt *t, uint8_t c)
{
	switch (c) {
	case '[': t->state = S_CSI; t->nparams = 0; t->params[0] = -1; t->priv = false; t->inter = 0; return;
	case ']': case 'P': case '^': case '_': case 'X': t->state = S_STRING; return;
	case '(': case ')': case '#': case '%': case ' ': t->inter = (char)c; t->state = S_ESC_INTER; return;
	case '7': save_cursor(t); break;
	case '8': restore_cursor(t); break;
	case 'D': index_down(t); break;
	case 'E': t->x = 0; t->wrapnext = false; index_down(t); break;
	case 'M': index_up(t); t->wrapnext = false; break;
	case 'H': t->tabs[t->x] = true; break;
	case 'c': vt_reset(t); break;
	case '=': t->keypad_app = true; break;
	case '>': t->keypad_app = false; break;
	case 'Z': say(t, "\x1b[?1;2c"); break;
	}
	t->state = S_GROUND;
}

static void esc_inter_dispatch(vt *t, uint8_t c)
{
	if (t->inter == '(') t->g[0] = (char)c;
	else if (t->inter == ')') t->g[1] = (char)c;
	else if (t->inter == '#' && c == '8') {	/* DECALN: fill with E's */
		vt_cell e = { 'E', 0, C_DEF_FG, C_DEF_BG };
		for (int r = 0; r < VT_ROWS; r++) {
			for (int col = 0; col < t->cols; col++) t->cell[r][col] = e;
			touch_row(t, r);
		}
		t->top = 0; t->bot = VT_ROWS - 1;
		move_to(t, 0, 0);
	}
	t->state = S_GROUND;
}

static void vt52_dispatch(vt *t, uint8_t c)
{
	t->state = S_GROUND;
	switch (c) {
	case 'A': move_to(t, t->x, t->y - 1); break;
	case 'B': move_to(t, t->x, t->y + 1); break;
	case 'C': move_to(t, t->x + 1, t->y); break;
	case 'D': move_to(t, t->x - 1, t->y); break;
	case 'F': t->g[0] = '0'; t->gl = 0; break;	/* the graphics set (the VT100's stands in) */
	case 'G': t->g[0] = 'B'; t->gl = 0; break;
	case 'H': move_to(t, 0, 0); break;
	case 'I': index_up(t); break;
	case 'J':
		clear_cells(t, t->y, t->x, t->cols);
		for (int r = t->y + 1; r < VT_ROWS; r++) clear_cells(t, r, 0, t->cols);
		break;
	case 'K': clear_cells(t, t->y, t->x, t->cols); break;
	case 'Y': t->state = S_VT52_Y1; break;
	case 'Z': say(t, "\x1b/Z"); break;
	case '=': t->keypad_app = true; break;
	case '>': t->keypad_app = false; break;
	case '<': t->ansi = true; break;
	}
}

/* the C0 controls, which act even in the middle of a sequence */
static bool control(vt *t, uint8_t c)
{
	switch (c) {
	case 0x07:
		if (t->state == S_STRING || t->state == S_STRING_ESC) { t->state = S_GROUND; return true; }	/* ends an OSC */
		if (t->bell) t->bell(t->ctx);
		return true;
	case 0x08: if (t->x > 0) t->x--; t->wrapnext = false; return true;
	case 0x09: {
		int x = t->x + 1;
		while (x < t->cols - 1 && !t->tabs[x]) x++;
		t->x = x < t->cols ? x : t->cols - 1;
		return true;
	}
	case 0x0A: case 0x0B: case 0x0C:
		index_down(t);
		if (t->newline) t->x = 0;
		t->wrapnext = false;
		return true;
	case 0x0D: t->x = 0; t->wrapnext = false; return true;
	case 0x0E: t->gl = 1; return true;
	case 0x0F: t->gl = 0; return true;
	case 0x18: case 0x1A: t->state = S_GROUND; return true;
	case 0x1B:
		t->state = t->ansi ? S_ESC : S_VT52_ESC;
		t->inter = 0;
		return true;
	}
	return c < 0x20;	/* the rest of them do nothing */
}

static void byte(vt *t, uint8_t c)
{
	/* inside an OSC or DCS string, only its end matters */
	if (t->state == S_STRING) {
		if (c == 0x1B) t->state = S_STRING_ESC;
		else if (c == 0x07 || c == 0x18 || c == 0x1A) t->state = S_GROUND;
		return;
	}
	if (t->state == S_STRING_ESC) {
		t->state = c == 0x1B ? S_STRING_ESC : c == '\\' ? S_GROUND : S_STRING;
		return;
	}
	if (t->state == S_UTF8) {
		if ((c & 0xC0) == 0x80) {
			u_cp = (u_cp << 6) | (c & 0x3F);
			if (--u_left == 0) {
				t->state = S_GROUND;
				int g = from_unicode(u_cp);
				if (g < 0x80) put_char(t, (uint8_t)g);	/* (ASCII stand-ins go through the set) */
				else put_glyph(t, (uint8_t)g);
			}
			return;
		}
		t->state = S_GROUND;
		put_glyph(t, '?');			/* cut short: show it, then this byte */
	}
	if (c < 0x20) { control(t, c); return; }
	if (c == 0x7F) return;

	switch (t->state) {
	case S_GROUND:
		if (c < 0x80) { put_char(t, c); return; }
		if (c >= 0xC2 && c <= 0xDF) { u_cp = c & 0x1F; u_left = 1; t->state = S_UTF8; return; }
		if (c >= 0xE0 && c <= 0xEF) { u_cp = c & 0x0F; u_left = 2; t->state = S_UTF8; return; }
		if (c >= 0xF0 && c <= 0xF4) { u_cp = c & 0x07; u_left = 3; t->state = S_UTF8; return; }
		put_glyph(t, '?');
		return;
	case S_ESC:
		esc_dispatch(t, c);
		return;
	case S_ESC_INTER:
		if (c >= 0x20 && c <= 0x2F) return;	/* more intermediates: (ignored) */
		esc_inter_dispatch(t, c);
		return;
	case S_CSI:
		if (c >= '0' && c <= '9') {
			if (t->nparams == 0) t->nparams = 1;
			int *p = &t->params[t->nparams - 1];
			if (*p < 0) *p = 0;
			if (*p < 10000) *p = *p * 10 + (c - '0');
		} else if (c == ';' || c == ':') {
			if (t->nparams == 0) t->nparams = 1;
			if (t->nparams < 16) t->params[t->nparams++] = -1;
		} else if (c >= '<' && c <= '?') {
			t->priv = true;
		} else if (c >= 0x20 && c <= 0x2F) {
			t->inter = (char)c;
		} else if (c >= 0x40 && c <= 0x7E) {
			t->state = S_GROUND;
			csi_dispatch(t, c);
		} else {
			t->state = S_GROUND;
		}
		return;
	case S_VT52_ESC:
		vt52_dispatch(t, c);
		return;
	case S_VT52_Y1:
		vt52_row = c - 32;
		t->state = S_VT52_Y2;
		return;
	case S_VT52_Y2:
		move_to(t, c - 32, vt52_row);
		t->state = S_GROUND;
		return;
	}
}

void vt_input(vt *t, const uint8_t *s, size_t n)
{
	while (n--) byte(t, *s++);
}

/* ------------------------------------------------------------ keys */

static void send(vt *t, const char *s)
{
	say(t, s);
	if (t->local_echo) vt_input(t, (const uint8_t *)s, strlen(s));
}

void vt_key_char(vt *t, uint8_t c, bool alt)
{
	uint8_t b[2];
	size_t n = 0;
	if (alt) b[n++] = 0x1B;
	b[n++] = c;
	if (t->out) t->out(b, n, t->ctx);
	if (t->local_echo) vt_input(t, &c, 1);
}

void vt_key(vt *t, int key)
{
	char b[8];
	if (!t->ansi) {				/* VT52 */
		switch (key) {
		case VK_UP: send(t, "\x1b" "A"); return;
		case VK_DOWN: send(t, "\x1b" "B"); return;
		case VK_RIGHT: send(t, "\x1b" "C"); return;
		case VK_LEFT: send(t, "\x1b" "D"); return;
		case VK_PF1: send(t, "\x1bP"); return;
		case VK_PF2: send(t, "\x1bQ"); return;
		case VK_PF3: send(t, "\x1bR"); return;
		case VK_PF4: send(t, "\x1bS"); return;
		}
	} else {
		const char *ss3 = t->cursor_keys ? "\x1bO" : "\x1b[";
		switch (key) {
		case VK_UP: snprintf(b, sizeof b, "%sA", ss3); send(t, b); return;
		case VK_DOWN: snprintf(b, sizeof b, "%sB", ss3); send(t, b); return;
		case VK_RIGHT: snprintf(b, sizeof b, "%sC", ss3); send(t, b); return;
		case VK_LEFT: snprintf(b, sizeof b, "%sD", ss3); send(t, b); return;
		case VK_HOME: snprintf(b, sizeof b, "%sH", ss3); send(t, b); return;
		case VK_END: snprintf(b, sizeof b, "%sF", ss3); send(t, b); return;
		case VK_PF1: send(t, "\x1bOP"); return;
		case VK_PF2: send(t, "\x1bOQ"); return;
		case VK_PF3: send(t, "\x1bOR"); return;
		case VK_PF4: send(t, "\x1bOS"); return;
		case VK_INS: send(t, "\x1b[2~"); return;
		case VK_DEL: send(t, "\x1b[3~"); return;
		case VK_PGUP: send(t, "\x1b[5~"); return;
		case VK_PGDN: send(t, "\x1b[6~"); return;
		case VK_F5: send(t, "\x1b[15~"); return;	/* (as xterm and the VT220) */
		case VK_F6: send(t, "\x1b[17~"); return;
		case VK_F7: send(t, "\x1b[18~"); return;
		case VK_F8: send(t, "\x1b[19~"); return;
		case VK_F9: send(t, "\x1b[20~"); return;
		case VK_F10: send(t, "\x1b[21~"); return;
		}
	}
	switch (key) {
	case VK_ENTER:
		if (t->out) t->out((const uint8_t *)(t->newline ? "\r\n" : "\r"), t->newline ? 2 : 1, t->ctx);
		if (t->local_echo) vt_input(t, (const uint8_t *)"\r\n", 2);
		return;
	case VK_BS: vt_key_char(t, t->bs_sends_del ? 0x7F : 0x08, false); return;
	case VK_TAB: vt_key_char(t, 0x09, false); return;
	case VK_ESC: vt_key_char(t, 0x1B, false); return;
	case VK_DEL: vt_key_char(t, 0x7F, false); return;	/* (VT52: the DEL character) */
	}
}

/* ------------------------------------------------------------ the width */

void vt_set_cols(vt *t, int cols)
{
	if (cols != 64 && cols != 80) return;
	int old = t->cols;
	if (cols == old) return;
	t->cols = cols;
	if (cols > old) {		/* the new columns, blank (they may hold old text) */
		for (int r = 0; r < VT_ROWS; r++) clear_cells(t, r, old, cols);
		if (on_alt)
			for (int r = 0; r < VT_ROWS; r++)
				for (int c = old; c < cols; c++) alt_save[r][c] = blank(t);
	}
	for (int c = old; c < cols; c++) t->tabs[c] = c % 8 == 0;
	if (t->x > cols - 1) t->x = cols - 1;
	if (t->saved.x > cols - 1) t->saved.x = cols - 1;
	t->wrapnext = false;
	vt_touch_all(t);
}

/* ------------------------------------------------------------ for the tests */

void vt_row_text(const vt *t, int row, char *buf)
{
	for (int c = 0; c < t->cols; c++) {
		uint8_t ch = t->cell[row][c].ch;
		if (ch >= G_BASE && ch < G_BASE + 32) {
			uint8_t d = ch - G_BASE + 0x5F;
			ch = d == 0x71 ? '-' : d == 0x78 ? '|' : strchr("jklmntuvw", d) ? '+' : '#';
		}
		buf[c] = (char)ch;
	}
	buf[t->cols] = 0;
}
