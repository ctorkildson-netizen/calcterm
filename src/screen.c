/* screen.c -- the terminal's cells onto the PicoCalc's LCD.
 *
 * Each row's changed cells go out as one strip (the first changed cell to
 * the last), drawn here into a buffer and sent with one lcd_blit.  When the
 * whole screen scrolls, the display's own vertical scrolling rolls it, so
 * only the new bottom line has to be drawn.
 *
 * At 64 columns the letters are the 5x10 font; at 80, calcterm's own
 * 4x10 (src/font4.c).  The setup menu and the notes on the bottom line are
 * always in the 5x10 font, in its 64-column grid.
 */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "drivers/lcd.h"
#include "drivers/font.h"
#include "screen.h"

extern const font_t font_term;
extern const uint8_t font4[160][10];

#define GW 5			/* the 5x10 font's width (the menu's grid) */
#define GH GLYPH_HEIGHT		/* 10 */
#define TEXT_COLS 64		/* the menu's and the notes' grid */

static const uint16_t palette[16] = {
	RGB(0, 0, 0),       RGB(205, 49, 49),   RGB(13, 188, 121),  RGB(229, 229, 16),
	RGB(50, 110, 240),  RGB(205, 60, 205),  RGB(17, 168, 205),  RGB(208, 208, 208),
	RGB(110, 110, 110), RGB(255, 90, 90),   RGB(35, 230, 140),  RGB(255, 255, 90),
	RGB(100, 160, 255), RGB(255, 110, 255), RGB(70, 220, 255),  RGB(255, 255, 255),
};

static uint16_t strip[GH * 320];	/* one row of cells, either font */
static int cur_x = -1, cur_y = -1;	/* where the cursor was drawn */
static bool sticky_drawn;
static bool flashing;
static absolute_time_t flash_end;
static char note[TEXT_COLS + 1];
static absolute_time_t note_end;
static bool note_drawn;			/* a note is on the screen's bottom row */

void screen_init(void)
{
	lcd_define_scrolling(0, 0);		/* all of it scrolls */
	lcd_set_background(palette[0]);
	lcd_clear_screen();
	lcd_enable_cursor(false);
}

/* a block element or shade (vt.h), cw pixels wide, drawn as a shape so
   neighbouring cells join up */
static void block(uint16_t *buf, int w, int px, int cw, uint8_t ch, uint16_t fg, uint16_t bg)
{
	for (int r = 0; r < GH; r++) {
		uint16_t *p = buf + r * w + px;
		for (int x = 0; x < cw; x++) {
			bool on;
			if (ch < G_SHADE) {
				int q = (r < GH / 2 ? 0 : 2) + (x < (cw + 1) / 2 ? 0 : 1);
				on = ((ch - G_BLOCK) >> q) & 1;
			} else if (ch == G_SHADE) on = (x & 1) == 0 && (r & 1) == 0;		/* light */
			else if (ch == G_SHADE + 1) on = ((x + r) & 1) == 0;			/* medium */
			else on = !((x & 1) == 1 && (r & 1) == 1);			/* dark */
			p[x] = on ? fg : bg;
		}
	}
}

/* one cell of the 4x10 font into the strip */
static void glyph4(uint16_t *buf, int w, int px, uint8_t ch, uint16_t fg, uint16_t bg, bool under)
{
	if (ch >= G_BLOCK && ch < G_END) { block(buf, w, px, 4, ch, fg, bg); return; }
	const uint8_t *g = font4[ch < 160 ? ch : '?'];
	for (int r = 0; r < GH; r++) {
		uint8_t bits = g[r];
		if (under && r == GH - 1) bits = 0xF;
		uint16_t *p = buf + r * w + px;
		p[0] = bits & 8 ? fg : bg;
		p[1] = bits & 4 ? fg : bg;
		p[2] = bits & 2 ? fg : bg;
		p[3] = bits & 1 ? fg : bg;
	}
}

/* one cell into the strip, whose width is w pixels, at pixel column px */
static void glyph(uint16_t *buf, int w, int px, uint8_t ch, uint16_t fg, uint16_t bg, bool under)
{
	if (ch >= G_BLOCK && ch < G_END) { block(buf, w, px, 5, ch, fg, bg); return; }
	const uint8_t *g = &font_term.glyphs[ch * GH];
	if (ch >= G_BASE + 32) g = &font_term.glyphs['?' * GH];
	for (int r = 0; r < GH; r++) {
		uint8_t bits = g[r];
		if (under && r == GH - 1) bits = 0x1F;
		uint16_t *p = buf + r * w + px;
		p[0] = bits & 0x10 ? fg : bg;
		p[1] = bits & 0x08 ? fg : bg;
		p[2] = bits & 0x04 ? fg : bg;
		p[3] = bits & 0x02 ? fg : bg;
		p[4] = bits & 0x01 ? fg : bg;
	}
}

static void cell_colours(const vt *t, const vt_cell *c, bool cursor, uint16_t *fg, uint16_t *bg)
{
	int f = c->fg, b = c->bg;
	if ((c->attr & (A_BOLD | A_BLINK)) && f < 8) f += 8;
	bool rev = ((c->attr & A_REV) != 0) ^ t->reverse_screen ^ cursor;
	if (flashing) rev = !rev;
	*fg = palette[rev ? b : f];
	*bg = palette[rev ? f : b];
}

/* cells c0..c1 of a row into buf, w pixels wide; sticky: draw its mark */
static void render_span(const vt *t, int row, int c0, int c1, uint16_t *buf, int w)
{
	int cw = t->cols == 80 ? 4 : GW;
	for (int c = c0; c <= c1; c++) {
		const vt_cell *cell = &t->cell[row][c];
		bool cursor = t->cursor_on && row == t->y && c == t->x;
		uint16_t fg, bg;
		cell_colours(t, cell, cursor, &fg, &bg);
		uint8_t ch = cell->ch;
		if (sticky_drawn && row == 0 && c == t->cols - 1) {	/* sticky Ctrl's mark */
			ch = '^';
			fg = palette[0];
			bg = palette[11];
		}
		if (cw == 4) glyph4(buf, w, (c - c0) * 4, ch, fg, bg, cell->attr & A_UNDER);
		else glyph(buf, w, (c - c0) * GW, ch, fg, bg, cell->attr & A_UNDER);
	}
}

static void draw_span(const vt *t, int row, int c0, int c1)
{
	int cw = t->cols == 80 ? 4 : GW;
	int w = (c1 - c0 + 1) * cw;
	render_span(t, row, c0, c1, strip, w);
	lcd_blit(strip, (uint16_t)(c0 * cw), (uint16_t)(row * GH), (uint16_t)w, GH);
}

void screen_render_row(const vt *t, int row, uint16_t *buf)
{
	bool sticky = sticky_drawn, flash = flashing;
	sticky_drawn = flashing = false;	/* (the picture is of the screen itself) */
	render_span(t, row, 0, t->cols - 1, buf, 320);
	sticky_drawn = sticky;
	flashing = flash;
}

static void mark(vt *t, int row, int col);
static void mark_row(vt *t, int row)
{
	mark(t, row, 0);
	mark(t, row, t->cols - 1);
}

/* a cell to be drawn again (the cursor's, the sticky mark's) */
static void mark(vt *t, int row, int col)
{
	if (col >= t->cols) col = t->cols - 1;
	if (!t->dirty[row] || col < t->dlo[row]) t->dlo[row] = (uint8_t)col;
	if (!t->dirty[row] || col > t->dhi[row]) t->dhi[row] = (uint8_t)col;
	t->dirty[row] = true;
}

void screen_draw(vt *t, bool sticky)
{
	if (flashing && time_reached(flash_end)) {
		flashing = false;
		vt_touch_all(t);
	}
	if (t->hw_scroll) {
		for (int i = 0; i < t->hw_scroll; i++) lcd_scroll_up();
		cur_y -= t->hw_scroll;
		/* a note on the bottom row has rolled up with the rest: the rows it
		   went to, back as they should be */
		if (note_drawn)
			for (int r = VT_ROWS - 1 - t->hw_scroll; r < VT_ROWS - 1; r++)
				if (r >= 0) mark_row(t, r);
		t->hw_scroll = 0;
	}
	/* the old cursor, the sticky mark: put back what's under them */
	bool show = t->cursor_on;
	if (cur_y >= 0 && (!show || cur_x != t->x || cur_y != t->y)) {
		mark(t, cur_y, cur_x);
	}
	if (sticky != sticky_drawn) {
		sticky_drawn = sticky;
		mark(t, 0, t->cols - 1);
	}
	if (show) {
		mark(t, t->y, t->x);
	}
	for (int r = 0; r < VT_ROWS; r++) {
		if (!t->dirty[r]) continue;
		int hi = t->dhi[r] < t->cols ? t->dhi[r] : t->cols - 1;
		if (t->dlo[r] <= hi) draw_span(t, r, t->dlo[r], hi);
		t->dirty[r] = false;
	}
	cur_x = show ? t->x : -1;
	cur_y = show ? t->y : -1;
	if (note[0]) {
		if (time_reached(note_end)) {
			note[0] = 0;
			t->dirty[VT_ROWS - 1] = true;		/* what was under it, back */
			t->dlo[VT_ROWS - 1] = 0;
			t->dhi[VT_ROWS - 1] = (uint8_t)(t->cols - 1);
		} else {
			screen_text(0, VT_ROWS - 1, note, 0, 11);
		}
	}
	note_drawn = note[0] != 0;
}

void screen_note(const char *s, int ms)
{
	snprintf(note, sizeof note, "%-*.*s", TEXT_COLS, TEXT_COLS, s);
	note_end = make_timeout_time_ms((uint32_t)ms);
}

void screen_flash(void)
{
	flashing = true;
	flash_end = make_timeout_time_ms(80);
}

void screen_text(int col, int row, const char *s, int fg, int bg)
{
	int n = (int)strlen(s);
	if (col + n > TEXT_COLS) n = TEXT_COLS - col;
	if (n <= 0) return;
	int w = n * GW;
	for (int i = 0; i < n; i++)
		glyph(strip, w, i * GW, (uint8_t)s[i], palette[fg], palette[bg], false);
	lcd_blit(strip, (uint16_t)(col * GW), (uint16_t)(row * GH), (uint16_t)w, GH);
}

void screen_fill(int col, int row, int w, int h, int bg)
{
	lcd_solid_rectangle(palette[bg], (uint16_t)(col * GW), (uint16_t)(row * GH),
			    (uint16_t)(w * GW), (uint16_t)(h * GH));
}
