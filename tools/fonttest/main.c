/* fonttest -- the sample screen in calcterm's 4-pixel fonts, on the PicoCalc.
 *
 *   1  4x10: 80 columns, 32 rows
 *   2  4x12: 80 columns, 26 rows
 *   3  the 5x10 font calcterm has now (64 columns: the right of each line is cut)
 *   G  grey edge pixels on or off
 *   C  green text, white text
 *   Q  back to the shell
 */
#include <stdbool.h>
#include <string.h>
#include "pico/stdlib.h"
#include "drivers/lcd.h"
#include "drivers/keyboard.h"
#include "drivers/southbridge.h"
#include "picocalc_shell.h"
#include "sample.h"

volatile bool user_interrupt;
extern const uint8_t font4x10[160][10], font4x12[160][12];
extern const font_t font_term;

static uint16_t cell[12 * 5];

static uint16_t mix(uint16_t fg, uint16_t bg, int level)	/* level 0-3 */
{
	int r = ((fg >> 11) * level + (bg >> 11) * (3 - level)) / 3;
	int g = (((fg >> 5) & 63) * level + ((bg >> 5) & 63) * (3 - level)) / 3;
	int b = ((fg & 31) * level + (bg & 31) * (3 - level)) / 3;
	return (uint16_t)(r << 11 | g << 5 | b);
}

static void draw(int mode, bool grey, uint16_t fg)
{
	const uint16_t bg = 0;
	lcd_solid_rectangle(bg, 0, 0, 320, 320);
	int h = mode == 1 ? 12 : 10, w = mode == 2 ? 5 : 4, cols = 320 / w, rows = 320 / h;
	int n = sizeof sample / sizeof sample[0];
	for (int r = 0; r < rows && r < n; r++) {
		const unsigned char *s = (const unsigned char *)sample[r];
		for (int c = 0; c < cols && s[c]; c++) {
			unsigned ch = s[c];
			if (mode == 2) {
				const uint8_t *g = &font_term.glyphs[(ch < 160 ? ch : '?') * 10];
				for (int y = 0; y < 10; y++)
					for (int x = 0; x < 5; x++)
						cell[y * 5 + x] = (g[y] & (0x10 >> x)) ? fg : bg;
			} else {
				const uint8_t *g = ch < 160 ? (h == 10 ? font4x10[ch] : font4x12[ch]) : font4x10['?'];
				for (int y = 0; y < h; y++)
					for (int x = 0; x < 4; x++) {
						int lv = (g[y] >> (6 - 2 * x)) & 3;
						if (!grey) lv = lv >= 2 ? 3 : 0;
						cell[y * 4 + x] = mix(fg, bg, lv);
					}
			}
			lcd_blit(cell, (uint16_t)(c * w), (uint16_t)(r * h), (uint16_t)w, (uint16_t)h);
		}
	}
}

int main(void)
{
	sb_init();
	lcd_init();
	lcd_define_scrolling(0, 0);
	lcd_enable_cursor(false);
	keyboard_init();
	keyboard_set_background_poll(true);
	int mode = 0;
	bool grey = true, green = false;
	for (;;) {
		draw(mode, grey, green ? RGB(80, 255, 120) : RGB(208, 208, 208));
		char k = keyboard_get_key();
		if (k == '1' || k == '2' || k == '3') mode = k - '1';
		else if (k == 'g' || k == 'G') grey = !grey;
		else if (k == 'c' || k == 'C') green = !green;
		else if (k == 'q' || k == 'Q') picocalc_shell_exit();
	}
}
