/* menutest.c -- the setup menu on Linux, keys fed from a list, the screen saved as a picture */
#include <stdio.h>
#include <string.h>
#include "drivers/lcd.h"
#include "../src/vt.h"
#include "../src/screen.h"
#include "../src/keys.h"
#include "../src/link.h"
#include "../src/settings.h"
#include "../src/menu.h"
const char *stub_root = ".";
uint16_t lcd_mem[FRAME_HEIGHT][WIDTH]; int lcd_offset; static uint16_t lcd_bg;
void lcd_set_background(uint16_t c) { lcd_bg = c; }
void lcd_enable_cursor(bool on) { (void)on; }
void lcd_define_scrolling(uint16_t a, uint16_t b) { (void)a; (void)b; }
void lcd_blit(const uint16_t *px, uint16_t x, uint16_t y, uint16_t w, uint16_t h) { for (int r = 0; r < h; r++) for (int c = 0; c < w; c++) lcd_mem[y + r][x + c] = px[r * w + c]; }
void lcd_solid_rectangle(uint16_t col, uint16_t x, uint16_t y, uint16_t w, uint16_t h) { for (int r = 0; r < h; r++) for (int c = 0; c < w; c++) lcd_mem[y + r][x + c] = col; }
void lcd_clear_screen(void) { lcd_solid_rectangle(lcd_bg, 0, 0, WIDTH, HEIGHT); }
void lcd_scroll_up(void) {}
absolute_time_t make_timeout_time_ms(uint32_t ms) { return ms; }
bool time_reached(absolute_time_t t) { (void)t; return false; }
/* keys from a list; the last one closes the menu */
static const int *script; static int pos;
void keys_poll(void) {}
int keys_get(void) { return script[pos] ? script[pos++] : VK_ESC; }
bool keys_sticky(void) { return false; }
void link_open(const link_config *c) { (void)c; }
size_t link_read(uint8_t *b, size_t m) { (void)b; (void)m; return 0; }
void link_write(const uint8_t *s, size_t n) { (void)s; (void)n; }
void link_break(void) { printf("break sent\n"); }
settings cfg;
bool settings_save(const char **why) { *why = "test"; return false; }
void shot_take(const vt *t) { (void)t; printf("screenshot taken\n"); }
static void save(const char *path)
{
	FILE *f = fopen(path, "wb");
	fprintf(f, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
	for (int y = 0; y < HEIGHT; y++) for (int x = 0; x < WIDTH; x++) {
		uint16_t p = lcd_mem[y][x]; uint8_t c[3] = { (uint8_t)((p >> 11) << 3), (uint8_t)(((p >> 5) & 63) << 2), (uint8_t)((p & 31) << 3) };
		fwrite(c, 1, 3, f);
	}
	fclose(f);
}
int main(int argc, char **argv)
{
	static vt t;
	cfg.link.baud = 115200; cfg.link.data = 8; cfg.link.stop = 1; cfg.link.xonxoff = true; cfg.bs_del = true; cfg.cols = 64;
	screen_init();
	vt_init(&t, NULL, NULL, NULL);
	vt_input(&t, (const uint8_t *)"chris@mini:~$ ", 14);
	screen_draw(&t, false);
	/* s: speed up one; e: echo on; then a pause for the picture (DOWN with nothing after) */
	static const int k1[] = { 's', 'S', 'e', 'k', 'x', 0 };
	script = k1;
	menu_run(&t);
	printf("speed %lu, echo %d\n", (unsigned long)cfg.link.baud, cfg.echo);
	static const int k2[] = { 'b', 0 };	/* leaves the menu drawn on Esc? no: draw happens before Esc */
	script = k2; pos = 0;
	(void)argc;
	menu_run(&t);
	save(argv[1]);
	static const int k4[] = { 'w', 0 };	/* to 80 columns, with the menu still up */
	script = k4; pos = 0;
	vt_input(&t, (const uint8_t *)"\r\n80 columns now: the line is longer than sixty-four characters, see", 68);
	menu_run(&t);
	printf("columns %d (settings %d)\n", t.cols, cfg.cols);
	screen_draw(&t, false);
	if (argc > 2) save(argv[2]);
	static const int k3[] = { 't', 'q', 0 };
	script = k3; pos = 0;
	menu_run(&t);
	return 0;
}
