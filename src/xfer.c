/* xfer.c -- files to and from the host by ZMODEM (zmodem.c), on the
 * PicoCalc: the line, the card, the keyboard, and a picker for the file
 * to send.
 *
 * `sz file` on the host: the file arrives in /calcterm/files, replacing
 * one of the same name.  `rz` on the host: a picker opens on the card, and
 * the chosen file goes to the host's current folder.  Esc stops either.
 * The bottom line says how it's going, and how it ended.
 */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "drivers/filesystem.h"
#include "keys.h"
#include "link.h"
#include "screen.h"
#include "xfer.h"
#include "zmodem.h"

typedef struct {
	vt *t;
	const uint8_t *rest;		/* what came after the start, still to be read */
	size_t n_rest;
	uint8_t buf[256];		/* from the line */
	size_t n_buf, at_buf;
	FIL file;
	char path[FS_MAX_PATH_LEN];
	bool sending;
	absolute_time_t next_note, next_key;
} xfer;

static bool esc_pressed(void)
{
	keys_poll();
	int k;
	while ((k = keys_get()) >= 0)
		if ((k & ~KF_ALT) == VK_ESC) return true;
	return false;
}

static int x_get(void *ctx, int ms)
{
	xfer *x = ctx;
	absolute_time_t until = make_timeout_time_ms((uint32_t)ms);
	for (;;) {
		if (time_reached(x->next_key)) {	/* Esc (the keyboard is slow to ask: now and then) */
			x->next_key = make_timeout_time_ms(100);
			if (esc_pressed()) return ZM_CANCEL;
		}
		if (x->n_rest) { x->n_rest--; return *x->rest++; }
		if (x->at_buf < x->n_buf) return x->buf[x->at_buf++];
		x->n_buf = link_read(x->buf, sizeof x->buf);
		x->at_buf = 0;
		if (x->n_buf) continue;
		if (time_reached(until)) return ZM_TIMEOUT;
	}
}

static void x_put(void *ctx, const uint8_t *s, size_t n)
{
	(void)ctx;
	link_write(s, n);
}

static void *x_create(void *ctx, const char *name, uint32_t size)
{
	xfer *x = ctx;
	(void)size;
	f_mkdir("/calcterm");
	f_mkdir(XFER_DIR);
	snprintf(x->path, sizeof x->path, XFER_DIR "/%s", name);
	if (f_open(&x->file, x->path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return NULL;
	return &x->file;
}

static bool x_write(void *ctx, void *f, const uint8_t *d, size_t n)
{
	(void)ctx;
	UINT put = 0;
	return f_write(f, d, (UINT)n, &put) == FR_OK && put == n;
}

static void x_close(void *ctx, void *f, bool ok)
{
	xfer *x = ctx;
	f_close(f);
	if (!ok) f_unlink(x->path);		/* (half a file is worse than none) */
}

static int x_read(void *ctx, void *f, uint32_t at, uint8_t *d, size_t n)
{
	(void)ctx;
	UINT got = 0;
	if (f_tell((FIL *)f) != at && f_lseek(f, at) != FR_OK) return -1;
	if (f_read(f, d, (UINT)n, &got) != FR_OK) return -1;
	return (int)got;
}

static void x_progress(void *ctx, const char *name, uint32_t done, uint32_t size)
{
	xfer *x = ctx;
	if (!time_reached(x->next_note) && done && done != size) return;
	char b[128];
	if (size) snprintf(b, sizeof b, " %s %.24s: %lu of %lu bytes (%d%%)  Esc: stop", x->sending ? "Sending" : "Receiving",
			   name, (unsigned long)done, (unsigned long)size, (int)((uint64_t)done * 100 / size));
	else snprintf(b, sizeof b, " %s %.24s: %lu bytes  Esc: stop", x->sending ? "Sending" : "Receiving", name, (unsigned long)done);
	screen_note(b, 60000);
	screen_draw(x->t, false);
	x->next_note = make_timeout_time_ms(250);
}

static void begin(xfer *x, vt *t, const uint8_t *rest, size_t n, bool sending)
{
	memset(x, 0, sizeof *x);
	x->t = t;
	x->rest = rest;
	x->n_rest = n;
	x->sending = sending;
	x->next_note = x->next_key = get_absolute_time();
}

static zm_io io_for(xfer *x)
{
	zm_io io = { x, x_get, x_put, x_create, x_write, x_close, x_read, x_progress };
	return io;
}

static void finish(vt *t, const char *msg)
{
	/* the host's start ended its line ("rz waiting to receive."), but the
	   transfer took that: end it here, so the prompt starts a new one */
	if (t->x != 0) vt_input(t, (const uint8_t *)"\r\n", 2);
	char b[80];
	snprintf(b, sizeof b, " %s", msg);
	screen_note(b, 6000);
	screen_draw(t, false);
}

void xfer_receive(vt *t, const uint8_t *rest, size_t n)
{
	static xfer x;
	begin(&x, t, rest, n, false);
	zm_io io = io_for(&x);
	char msg[80];
	screen_note(" Receiving a file (ZMODEM)...  Esc: stop", 60000);
	screen_draw(t, false);
	int got = zm_receive(&io, msg, sizeof msg);
	if (got > 1) snprintf(msg, sizeof msg, "%d files received, in " XFER_DIR, got);
	finish(t, msg);
}

/* ------------------------------------------------------------ the picker */

#define MAX_ENTRIES 200
#define NAME_LEN 48
#define LIST_TOP 3
#define LIST_ROWS 25
static struct { char name[NAME_LEN]; bool dir; uint32_t size; } entry[MAX_ENTRIES];
static int n_entries;
static char cwd[FS_MAX_PATH_LEN] = XFER_DIR;

static int before(int a, int b)		/* folders first, then by name */
{
	if (entry[a].dir != entry[b].dir) return entry[a].dir;
	return strcasecmp(entry[a].name, entry[b].name) < 0;
}

static bool list(void)
{
	DIR d;
	FILINFO fi;
	n_entries = 0;
	if (f_opendir(&d, cwd) != FR_OK) return false;
	if (strcmp(cwd, "/") != 0) {
		snprintf(entry[0].name, NAME_LEN, "..");
		entry[0].dir = true;
		n_entries = 1;
	}
	while (n_entries < MAX_ENTRIES && f_readdir(&d, &fi) == FR_OK && fi.fname[0]) {
		if (fi.fattrib & (AM_HID | AM_SYS)) continue;
		snprintf(entry[n_entries].name, NAME_LEN, "%.47s", fi.fname);
		entry[n_entries].dir = (fi.fattrib & AM_DIR) != 0;
		entry[n_entries].size = (uint32_t)fi.fsize;
		n_entries++;
	}
	f_closedir(&d);
	int from = strcmp(cwd, "/") != 0;	/* (".." stays at the top) */
	for (int i = from + 1; i < n_entries; i++)
		for (int j = i; j > from && before(j, j - 1); j--) {
			__typeof__(entry[0]) tmp = entry[j];
			entry[j] = entry[j - 1];
			entry[j - 1] = tmp;
		}
	return true;
}

static void draw_picker(int sel, int top)
{
	char b[65];
	screen_fill(0, 0, 64, 32, 4);
	screen_text(1, 0, "Send a file to the host (rz is waiting for it)", 11, 4);
	snprintf(b, sizeof b, "%.62s", cwd);
	screen_text(1, 1, b, 15, 4);
	if (!n_entries) screen_text(3, LIST_TOP, "(nothing here)", 7, 4);
	for (int r = 0; r < LIST_ROWS && top + r < n_entries; r++) {
		int i = top + r;
		if (entry[i].dir) snprintf(b, sizeof b, " %.55s/", entry[i].name);
		else snprintf(b, sizeof b, " %-50.50s %9lu ", entry[i].name, (unsigned long)entry[i].size);
		snprintf(b + strlen(b), sizeof b - strlen(b), "%*s", 62 - (int)strlen(b), "");
		screen_text(1, LIST_TOP + r, b, i == sel ? 0 : 15, i == sel ? 11 : 4);
	}
	screen_text(1, 30, "Enter: send it (or open the folder)  Bksp: up  Esc: cancel", 7, 4);
}

static void parent(void)
{
	char *slash = strrchr(cwd, '/');
	if (!slash || slash == cwd) strcpy(cwd, "/");
	else *slash = 0;
}

/* a file chosen, its path in path; false: cancelled */
static bool pick(char *path, size_t size, uint32_t *bytes, uint32_t *mtime)
{
	if (!list()) { strcpy(cwd, "/"); list(); }
	int sel = 0, top = 0;
	for (;;) {
		if (sel < top) top = sel;
		if (sel >= top + LIST_ROWS) top = sel - LIST_ROWS + 1;
		draw_picker(sel, top);
		int k;
		do { keys_poll(); k = keys_get(); if (k < 0) sleep_ms(10); } while (k < 0);
		k &= ~KF_ALT;
		if (k == VK_ESC) return false;
		if (k == VK_UP && sel > 0) sel--;
		if (k == VK_DOWN && sel < n_entries - 1) sel++;
		if (k == VK_PGUP) sel = sel > LIST_ROWS ? sel - LIST_ROWS : 0;
		if (k == VK_PGDN) sel = sel + LIST_ROWS < n_entries ? sel + LIST_ROWS : (n_entries ? n_entries - 1 : 0);
		if (k == VK_HOME) sel = 0;
		if (k == VK_END && n_entries) sel = n_entries - 1;
		if (k == VK_BS || k == VK_LEFT) { parent(); list(); sel = top = 0; }
		if ((k == VK_ENTER || k == VK_RIGHT) && n_entries) {
			if (strcmp(entry[sel].name, "..") == 0) { parent(); list(); sel = top = 0; continue; }
			char p[FS_MAX_PATH_LEN + NAME_LEN];
			snprintf(p, sizeof p, "%s%s%s", cwd, strcmp(cwd, "/") ? "/" : "", entry[sel].name);
			if (entry[sel].dir) {
				snprintf(cwd, sizeof cwd, "%.259s", p);
				if (!list()) { parent(); list(); }
				sel = top = 0;
				continue;
			}
			if (k == VK_RIGHT) continue;
			FILINFO fi;
			if (f_stat(p, &fi) != FR_OK) continue;
			snprintf(path, size, "%.259s", p);
			*bytes = (uint32_t)fi.fsize;
			/* the FAT date and time as seconds since 1970 (local time, as the card has it) */
			int y = 1980 + (fi.fdate >> 9), m = (fi.fdate >> 5) & 15, d = fi.fdate & 31;
			if (m < 1 || m > 12 || d < 1) { *mtime = 0; return true; }
			int yy = m <= 2 ? y - 1 : y, era = yy / 400, yoe = yy - era * 400;
			int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
			long days = era * 146097L + (yoe * 365 + yoe / 4 - yoe / 100 + doy) - 719468L;
			*mtime = (uint32_t)(days * 86400 + (fi.ftime >> 11) * 3600 + ((fi.ftime >> 5) & 63) * 60 + (fi.ftime & 31) * 2);
			return true;
		}
	}
}

void xfer_send(vt *t, const uint8_t *rest, size_t n)
{
	static xfer x;
	char path[FS_MAX_PATH_LEN], msg[80];
	uint32_t size = 0, mtime = 0;
	bool chosen = pick(path, sizeof path, &size, &mtime);
	vt_touch_all(t);			/* the terminal back */
	begin(&x, t, rest, n, true);
	zm_io io = io_for(&x);
	if (!chosen) {
		zm_cancel(&io);
		/* rz answers with CANs and backspaces: not for the screen */
		absolute_time_t until = make_timeout_time_ms(1000);
		while (!time_reached(until)) {
			int c = x_get(&x, 50);
			if (c == ZM_TIMEOUT || c == ZM_CANCEL) continue;
			if (c != 0x18 && c != 0x08) { uint8_t b = (uint8_t)c; vt_input(t, &b, 1); break; }
		}
		finish(t, "Nothing sent");
		return;
	}
	if (f_open(&x.file, path, FA_READ) != FR_OK) {
		zm_cancel(&io);
		finish(t, "Couldn't open that file");
		return;
	}
	const char *name = strrchr(path, '/');
	name = name ? name + 1 : path;
	screen_note(" Sending...  Esc: stop", 60000);
	screen_draw(t, false);
	zm_send(&io, &x.file, name, size, mtime, msg, sizeof msg);
	f_close(&x.file);
	finish(t, msg);
}
