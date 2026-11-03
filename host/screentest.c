/* screentest.c -- the renderer, on Linux, against a model of the LCD's frame
 * memory and its vertical scrolling.
 *
 *   screentest [-t TERM] [-k KEYS]... [-o out.ppm] command [args]
 *
 * Runs the program in a pty as ptyrun does, drawing as the PicoCalc would
 * (every so often, while the bytes stream in), then checks that what's on
 * the "display" is exactly what a fresh, whole-screen draw of the same cells
 * gives: if the incremental drawing or the scrolling ever gets out of step,
 * this finds it.  -o writes the display as a picture, twice size.
 */
#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "drivers/lcd.h"
#include "../src/vt.h"
#include "../src/screen.h"
#include "../src/shot.h"
const char *stub_root;

/* ---- the LCD model */
uint16_t lcd_mem[FRAME_HEIGHT][WIDTH];
int lcd_offset;
static uint16_t lcd_bg;
void lcd_set_background(uint16_t c) { lcd_bg = c; }
void lcd_enable_cursor(bool on) { (void)on; }
void lcd_define_scrolling(uint16_t top, uint16_t bottom) { (void)top; (void)bottom; lcd_offset = 0; }
void lcd_blit(const uint16_t *px, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
	for (int r = 0; r < h; r++)
		for (int c = 0; c < w; c++)
			lcd_mem[(lcd_offset + y + r) % FRAME_HEIGHT][x + c] = px[r * w + c];
}
void lcd_solid_rectangle(uint16_t colour, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
	for (int r = 0; r < h; r++)
		for (int c = 0; c < w; c++)
			lcd_mem[(lcd_offset + y + r) % FRAME_HEIGHT][x + c] = colour;
}
void lcd_clear_screen(void) { lcd_offset = 0; lcd_solid_rectangle(lcd_bg, 0, 0, WIDTH, HEIGHT); }
void lcd_scroll_up(void)
{
	lcd_offset = (lcd_offset + GLYPH_HEIGHT) % FRAME_HEIGHT;
	lcd_solid_rectangle(lcd_bg, 0, HEIGHT - GLYPH_HEIGHT, WIDTH, GLYPH_HEIGHT);
}
static uint16_t shown(int x, int y) { return lcd_mem[(lcd_offset + y) % FRAME_HEIGHT][x]; }

/* ---- time, for the visual bell */
static uint64_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
absolute_time_t make_timeout_time_ms(uint32_t ms) { return now_ms() + ms; }
bool time_reached(absolute_time_t t) { return now_ms() >= t; }

/* ---- the program */
static int master = -1;
static vt t;
static void out(const uint8_t *s, size_t n, void *ctx) { (void)ctx; if (write(master, s, n) < 0) perror("write"); }

static void pump(double secs)
{
	uint64_t end = now_ms() + (uint64_t)(secs * 1000);
	uint8_t buf[256];		/* small reads, so drawing interleaves as on the device */
	while (now_ms() < end) {
		struct pollfd p = { master, POLLIN, 0 };
		if (poll(&p, 1, 20) > 0) {
			ssize_t n = read(master, buf, sizeof buf);
			if (n <= 0) break;
			vt_input(&t, buf, (size_t)n);
			if (rand() % 3 == 0) screen_draw(&t, false);
		} else {
			screen_draw(&t, false);
		}
	}
	screen_draw(&t, false);
}

static size_t unescape(const char *s, char *o)
{
	size_t n = 0;
	while (*s) {
		if (*s != '\\') { o[n++] = *s++; continue; }
		s++;
		switch (*s) {
		case 'e': o[n++] = 27; s++; break;
		case 'r': o[n++] = '\r'; s++; break;
		case 'n': o[n++] = '\n'; s++; break;
		case 'x': { unsigned v; sscanf(s + 1, "%2x", &v); o[n++] = (char)v; s += 3; break; }
		default: o[n++] = *s++;
		}
	}
	return n;
}

static void write_ppm(const char *path)
{
	FILE *f = fopen(path, "wb");
	fprintf(f, "P6\n%d %d\n255\n", WIDTH * 2, HEIGHT * 2);
	for (int y = 0; y < HEIGHT * 2; y++)
		for (int x = 0; x < WIDTH * 2; x++) {
			uint16_t p = shown(x / 2, y / 2);
			uint8_t rgb[3] = { (uint8_t)((p >> 11) << 3), (uint8_t)(((p >> 5) & 63) << 2), (uint8_t)((p & 31) << 3) };
			fwrite(rgb, 1, 3, f);
		}
	fclose(f);
}

int main(int argc, char **argv)
{
	const char *term = "vt100", *ppm = NULL;
	char *keys[32];
	int nkeys = 0, opt;
	const char *shotdir = NULL;
	while ((opt = getopt(argc, argv, "+t:k:o:s:")) != -1) {
		if (opt == 's') shotdir = optarg;
		else if (opt == 't') term = optarg;
		else if (opt == 'o') ppm = optarg;
		else if (opt == 'k' && nkeys < 32) keys[nkeys++] = optarg;
	}
	if (optind >= argc) return 2;
	srand(1);
	screen_init();
	vt_init(&t, out, NULL, NULL);
	struct winsize ws = { VT_ROWS, VT_COLS, 0, 0 };
	pid_t pid = forkpty(&master, NULL, NULL, &ws);
	if (pid == 0) {
		setenv("TERM", term, 1);
		setenv("LANG", "C.UTF-8", 1);
		execvp(argv[optind], argv + optind);
		_exit(127);
	}
	pump(1.5);
	for (int i = 0; i < nkeys; i++) {
		char b[1024];
		size_t n = unescape(keys[i], b);
		out((uint8_t *)b, n, NULL);
		pump(0.8);
	}
	kill(pid, SIGHUP);
	waitpid(pid, NULL, 0);

	/* the display, against a fresh draw of the same cells */
	static uint16_t got[HEIGHT][WIDTH];
	for (int y = 0; y < HEIGHT; y++) for (int x = 0; x < WIDTH; x++) got[y][x] = shown(x, y);
	if (ppm) write_ppm(ppm);
	memset(lcd_mem, 0xAB, sizeof lcd_mem);
	lcd_offset = 0;
	vt_touch_all(&t);
	screen_draw(&t, false);
	int bad = 0;
	for (int y = 0; y < HEIGHT; y++)
		for (int x = 0; x < WIDTH; x++)
			if (got[y][x] != shown(x, y)) {
				if (bad++ < 5) printf("differs at pixel %d,%d (cell %d,%d)\n", x, y, x / 5, y / 10);
			}
	if (shotdir) {
		/* the screenshot, against the display */
		stub_root = shotdir;
		shot_take(&t);
		char p[512];
		snprintf(p, sizeof p, "%s/screenshots/calcterm-001.bmp", shotdir);
		FILE *f = fopen(p, "rb");
		int sbad = 0;
		if (!f) { printf("no screenshot\n"); sbad = 1; }
		else {
			fseek(f, 54, SEEK_SET);
			for (int y = HEIGHT - 1; y >= 0; y--)
				for (int x = 0; x < WIDTH; x++) {
					uint8_t c[3];
					if (fread(c, 1, 3, f) != 3) { sbad++; continue; }
					uint16_t v = shown(x, y);
					uint8_t want[3] = { (uint8_t)((v & 31) << 3), (uint8_t)(((v >> 5) & 63) << 2), (uint8_t)((v >> 11) << 3) };
					if (memcmp(c, want, 3)) sbad++;
				}
			fclose(f);
		}
		printf("screenshot: %s (%d pixels differ)\n", sbad ? "MISMATCH" : "ok", sbad);
		bad += sbad;
	}
	printf("%s: %s (%d pixels differ)\n",
	       argv[optind], bad ? "MISMATCH" : "ok", bad);
	return bad != 0;
}
