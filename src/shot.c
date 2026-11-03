/* shot.c -- screenshots (Alt+F4).
 *
 * The LCD isn't read back: the terminal's own cells are drawn again, row
 * by row, the way the screen draws them, into a 320x320 24-bit BMP.  A .txt
 * beside it has the screen as text, for bug reports.  Both go in
 * /screenshots, named by the date and time when the shell's clock is set
 * (calcterm-2026-11-03_101500.bmp), and otherwise by number
 * (calcterm-001.bmp), as picocalc-extras' screenshots for other programs.
 */
#define _POSIX_C_SOURCE 200809L	/* gmtime_r */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "pico/stdlib.h"
#include "drivers/filesystem.h"
#include "picocalc_shell.h"
#include "screen.h"
#include "shot.h"

#define W (VT_COLS * 5)
#define H (VT_ROWS * 10)

static void put_le(uint8_t *p, uint32_t v, int n)
{
	for (int i = 0; i < n; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static bool exists(const char *path)
{
	return f_stat(path, NULL) == FR_OK;
}

/* the next free name, without its extension, in name; false if none */
static bool shot_name(char *name, size_t size)
{
	char path[80];
	int64_t ms;
	int offset;
	if (picocalc_shell_time(&ms, &offset)) {
		time_t local = (time_t)(ms / 1000) + (time_t)offset * 60;
		struct tm tm;
		gmtime_r(&local, &tm);
		char stamp[24];
		strftime(stamp, sizeof stamp, "%Y-%m-%d_%H%M%S", &tm);
		snprintf(name, size, "calcterm-%s", stamp);
		for (int n = 2; n < 100; n++) {		/* two in one second */
			snprintf(path, sizeof path, "/screenshots/%s.bmp", name);
			if (!exists(path)) return true;
			snprintf(name, size, "calcterm-%s-%d", stamp, n);
		}
		return false;
	}
	for (int n = 1; n < 1000; n++) {
		snprintf(name, size, "calcterm-%03d", n);
		snprintf(path, sizeof path, "/screenshots/%s.bmp", name);
		if (!exists(path)) return true;
	}
	return false;
}

static bool write_all(FIL *f, const void *p, UINT n)
{
	UINT put = 0;
	return f_write(f, p, n, &put) == FR_OK && put == n;
}

static bool write_bmp(const vt *t, const char *path)
{
	FIL f;
	if (f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return false;
	uint8_t hdr[54] = { 'B', 'M' };
	put_le(hdr + 2, 54 + W * H * 3, 4);
	put_le(hdr + 10, 54, 4);
	put_le(hdr + 14, 40, 4);
	put_le(hdr + 18, W, 4);
	put_le(hdr + 22, H, 4);
	put_le(hdr + 26, 1, 2);
	put_le(hdr + 28, 24, 2);
	put_le(hdr + 34, W * H * 3, 4);
	bool ok = write_all(&f, hdr, sizeof hdr);
	static uint16_t px[W * 10];
	static uint8_t line[W * 3];
	for (int r = VT_ROWS - 1; ok && r >= 0; r--) {	/* BMP: the bottom row first */
		screen_render_row(t, r, px);
		for (int y = 9; ok && y >= 0; y--) {
			for (int x = 0; x < W; x++) {
				uint16_t v = px[y * W + x];
				line[x * 3 + 0] = (uint8_t)((v & 0x1F) << 3);
				line[x * 3 + 1] = (uint8_t)(((v >> 5) & 0x3F) << 2);
				line[x * 3 + 2] = (uint8_t)((v >> 11) << 3);
			}
			ok = write_all(&f, line, sizeof line);
		}
	}
	if (f_close(&f) != FR_OK) ok = false;
	return ok;
}

static bool write_txt(const vt *t, const char *path)
{
	FIL f;
	if (f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return false;
	char b[VT_COLS + 16];
	bool ok = write_all(&f, "calcterm screenshot\r\n\r\n", 23);
	for (int r = 0; ok && r < VT_ROWS; r++) {
		char row[VT_COLS + 1];
		vt_row_text(t, r, row);
		for (int c = 0; c < VT_COLS; c++)
			if ((uint8_t)row[c] < 0x20 || (uint8_t)row[c] >= 0x7F) row[c] = '.';
		int n = snprintf(b, sizeof b, "%2d |%s|\r\n", r + 1, row);
		ok = write_all(&f, b, (UINT)n);
	}
	int n = snprintf(b, sizeof b, "\r\ncursor: row %d, column %d\r\n", t->y + 1, t->x + 1);
	if (ok) ok = write_all(&f, b, (UINT)n);
	if (f_close(&f) != FR_OK) ok = false;
	return ok;
}

void shot_take(const vt *t)
{
	char name[48], path[80], msg[96];
	FRESULT r = f_mkdir("/screenshots");
	bool ok = (r == FR_OK || r == FR_EXIST) && shot_name(name, sizeof name);
	if (ok) {
		snprintf(path, sizeof path, "/screenshots/%s.bmp", name);
		ok = write_bmp(t, path);
	}
	if (ok) {
		snprintf(path, sizeof path, "/screenshots/%s.txt", name);
		ok = write_txt(t, path);
	}
	if (ok) snprintf(msg, sizeof msg, " Saved /screenshots/%s.bmp", name);
	else snprintf(msg, sizeof msg, " The screenshot failed (is there a card in?)");
	screen_note(msg, 2500);
}
