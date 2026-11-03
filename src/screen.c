/* screen.c -- the terminal's cells onto the PicoCalc's LCD.
 *
 * Each row's changed cells go out as one strip (the first changed cell to
 * the last), drawn here into a buffer and sent with one lcd_blit.  When the
 * whole screen scrolls, the display's own vertical scrolling rolls it, so
 * only the new bottom line has to be drawn.
 */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "drivers/lcd.h"
#include "drivers/font.h"
#include "screen.h"

extern const font_t font_term;

#define GW 5			/* glyph width */
#define GH GLYPH_HEIGHT		/* 10 */

static const uint16_t palette[16] = {
	RGB(0, 0, 0),       RGB(205, 49, 49),   RGB(13, 188, 121),  RGB(229, 229, 16),
	RGB(50, 110, 240),  RGB(205, 60, 205),  RGB(17, 168, 205),  RGB(208, 208, 208),
	RGB(110, 110, 110), RGB(255, 90, 90),   RGB(35, 230, 140),  RGB(255, 255, 90),
	RGB(100, 160, 255), RGB(255, 110, 255), RGB(70, 220, 255),  RGB(255, 255, 255),
};

static uint16_t strip[GH * VT_COLS * GW];
static int cur_x = -1, cur_y = -1;	/* where the cursor was drawn */
static bool sticky_drawn;
static bool flashing;
static absolute_time_t flash_end;
static char note[VT_COLS + 1];
static absolute_time_t note_end;

void screen_init(void)
{
	lcd_define_scrolling(0, 0);		/* all of it scrolls */
	lcd_set_background(palette[0]);
	lcd_clear_screen();
	lcd_enable_cursor(false);
}

/* one cell into the strip, whose width is w pixels, at pixel column px */
static void glyph(uint16_t *buf, int w, int px, uint8_t ch, uint16_t fg, uint16_t bg, bool under)
{
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
	for (int c = c0; c <= c1; c++) {
		const vt_cell *cell = &t->cell[row][c];
		bool cursor = t->cursor_on && row == t->y && c == t->x;
		uint16_t fg, bg;
		cell_colours(t, cell, cursor, &fg, &bg);
		uint8_t ch = cell->ch;
		if (sticky_drawn && row == 0 && c == VT_COLS - 1) {	/* sticky Ctrl's mark */
			ch = '^';
			fg = palette[0];
			bg = palette[11];
		}
		glyph(buf, w, (c - c0) * GW, ch, fg, bg, cell->attr & A_UNDER);
	}
}

static void draw_span(const vt *t, int row, int c0, int c1)
{
	int w = (c1 - c0 + 1) * GW;
	render_span(t, row, c0, c1, strip, w);
	lcd_blit(strip, (uint16_t)(c0 * GW), (uint16_t)(row * GH), (uint16_t)w, GH);
}

void screen_render_row(const vt *t, int row, uint16_t *buf)
{
	bool sticky = sticky_drawn, flash = flashing;
	sticky_drawn = flashing = false;	/* (the picture is of the screen itself) */
	render_span(t, row, 0, VT_COLS - 1, buf, VT_COLS * GW);
	sticky_drawn = sticky;
	flashing = flash;
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
		t->hw_scroll = 0;
	}
	/* the old cursor, the sticky mark: put back what's under them */
	bool show = t->cursor_on;
	if (cur_y >= 0 && (!show || cur_x != t->x || cur_y != t->y)) {
		t->dirty[cur_y] = true;
		t->dirty_cols[cur_y] |= (uint64_t)1 << cur_x;
	}
	if (sticky != sticky_drawn) {
		sticky_drawn = sticky;
		t->dirty[0] = true;
		t->dirty_cols[0] |= (uint64_t)1 << (VT_COLS - 1);
	}
	if (show) {
		t->dirty[t->y] = true;
		t->dirty_cols[t->y] |= (uint64_t)1 << t->x;
	}
	for (int r = 0; r < VT_ROWS; r++) {
		if (!t->dirty[r]) continue;
		uint64_t d = t->dirty_cols[r];
		if (d) draw_span(t, r, __builtin_ctzll(d), 63 - __builtin_clzll(d));
		t->dirty[r] = false;
		t->dirty_cols[r] = 0;
	}
	cur_x = show ? t->x : -1;
	cur_y = show ? t->y : -1;
	if (note[0]) {
		if (time_reached(note_end)) {
			note[0] = 0;
			t->dirty[VT_ROWS - 1] = true;		/* what was under it, back */
			t->dirty_cols[VT_ROWS - 1] = ~(uint64_t)0;
		} else {
			screen_text(0, VT_ROWS - 1, note, 0, 11);
		}
	}
}

void screen_note(const char *s, int ms)
{
	snprintf(note, sizeof note, "%-*.*s", VT_COLS, VT_COLS, s);
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
	if (col + n > VT_COLS) n = VT_COLS - col;
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
