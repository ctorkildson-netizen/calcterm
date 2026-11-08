/* xfertest.c -- calcterm's file transfers end to end on Linux: the real sz
 * and rz on a pty as the host, xfer.c with its picker and the card (a
 * folder here), keys fed from a list, the screen kept as a picture.
 *
 *   xfertest [picture.ppm]    the picker's picture goes there
 */
#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "drivers/lcd.h"
#include "drivers/filesystem.h"
#include "../src/vt.h"
#include "../src/screen.h"
#include "../src/keys.h"
#include "../src/link.h"
#include "../src/xfer.h"
#include "../src/zmodem.h"

static char root[64], host_dir[64];
const char *stub_root = root;
uint16_t lcd_mem[FRAME_HEIGHT][WIDTH]; int lcd_offset; static uint16_t lcd_bg;
void lcd_set_background(uint16_t c) { lcd_bg = c; }
void lcd_enable_cursor(bool on) { (void)on; }
void lcd_define_scrolling(uint16_t a, uint16_t b) { (void)a; (void)b; }
void lcd_blit(const uint16_t *px, uint16_t x, uint16_t y, uint16_t w, uint16_t h) { for (int r = 0; r < h; r++) for (int c = 0; c < w; c++) lcd_mem[y + r][x + c] = px[r * w + c]; }
void lcd_solid_rectangle(uint16_t col, uint16_t x, uint16_t y, uint16_t w, uint16_t h) { for (int r = 0; r < h; r++) for (int c = 0; c < w; c++) lcd_mem[y + r][x + c] = col; }
void lcd_clear_screen(void) { lcd_solid_rectangle(lcd_bg, 0, 0, WIDTH, HEIGHT); }
void lcd_scroll_up(void) {}

static uint64_t now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000 + (uint64_t)t.tv_nsec / 1000; }
absolute_time_t get_absolute_time(void) { return now_us(); }
absolute_time_t make_timeout_time_ms(uint32_t ms) { return now_us() + (uint64_t)ms * 1000; }
bool time_reached(absolute_time_t t) { return now_us() >= t; }

/* the line: a pty with sz or rz on the other end */
static int master = -1;
size_t link_read(uint8_t *b, size_t m)
{
	struct pollfd p = { master, POLLIN, 0 };
	if (poll(&p, 1, 1) <= 0) return 0;
	ssize_t n = read(master, b, m);
	return n > 0 ? (size_t)n : 0;
}
void link_write(const uint8_t *s, size_t n) { while (n) { ssize_t w = write(master, s, n); if (w <= 0) return; s += w, n -= (size_t)w; } }

/* keys from a list; SNAP saves the screen; then nothing */
#define SNAP -100
#define WAIT -1000		/* WAIT - n: nothing for n asks of the keyboard */
static int waited;
static const int *script; static int pos;
static const char *picture;
static void save(const char *path);
void keys_poll(void) {}
int keys_get(void)
{
	if (!script || !script[pos]) return -1;
	if (script[pos] == SNAP) { pos++; if (picture) save(picture); return -1; }
	if (script[pos] <= WAIT) { if (++waited < WAIT - script[pos]) return -1; waited = 0; pos++; return keys_get(); }
	return script[pos++];
}
bool keys_sticky(void) { return false; }

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

static vt term;
static int failed;

static void check(bool ok, const char *what)
{
	printf("%s: %s\n", what, ok ? "ok" : "FAILED");
	if (!ok) failed++;
}

/* as main.c does: what comes in goes to the terminal until a transfer starts */
static void host(char *const argv[], const char *dir)
{
	pid_t pid = forkpty(&master, NULL, NULL, NULL);
	if (pid == 0) { if (chdir(dir)) _exit(126); execvp(argv[0], argv); _exit(127); }
	struct termios t;
	tcgetattr(master, &t);
	cfmakeraw(&t);
	tcsetattr(master, TCSANOW, &t);
	zm_spotter sp = { 0 };
	uint8_t buf[512], out[8];
	bool done = false;
	for (int quiet = 0; !done && quiet < 3000; ) {
		size_t n = link_read(buf, sizeof buf);
		if (!n) { quiet++; continue; }
		quiet = 0;
		for (size_t i = 0; i < n; i++) {
			int got, s = zm_spot(&sp, buf[i], out, &got);
			vt_input(&term, out, (size_t)got);
			if (s == ZM_SPOT_RECEIVE) { xfer_receive(&term, buf + i + 1, n - i - 1); done = true; break; }
			if (s == ZM_SPOT_SEND) { xfer_send(&term, buf + i + 1, n - i - 1); done = true; break; }
		}
	}
	for (int i = 0; i < 100; i++) {			/* let it finish; what follows goes to the screen */
		uint8_t b[256];
		struct pollfd p = { master, POLLIN, 0 };
		ssize_t n = 0;
		if (poll(&p, 1, 50) > 0 && (n = read(master, b, sizeof b)) > 0) { vt_input(&term, b, (size_t)n); if (getenv("SHOW")) { fprintf(stderr, "after:"); for (ssize_t k = 0; k < n; k++) fprintf(stderr, " %02x", b[k]); fprintf(stderr, "\n"); } }
		if (n <= 0 && pid && waitpid(pid, NULL, WNOHANG) == pid) pid = 0;
		if (n <= 0 && !pid) break;		/* gone, and nothing more to read */
	}
	if (pid) { kill(pid, SIGKILL); waitpid(pid, NULL, 0); }
	close(master);
}

/* nothing left over from the protocol on the screen */
static bool screen_clean(void)
{
	for (int r = 0; r < VT_ROWS; r++) {
		char row[VT_MAX_COLS + 1];
		vt_row_text(&term, r, row);
		if (strstr(row, "OO") || strchr(row, '?') || strstr(row, "**")) { printf("  (row %d: |%s|)\n", r + 1, row); return false; }
	}
	return true;
}

static bool same(const char *a, const char *b)
{
	char cmd[300];
	snprintf(cmd, sizeof cmd, "cmp -s '%s' '%s'", a, b);
	return system(cmd) == 0;
}

static bool bottom_says(const char *s)
{
	/* the note is drawn over the bottom row: read it back from the LCD by
	   comparing with the text drawn on its own */
	static uint16_t keep[10][WIDTH];
	memcpy(keep, &lcd_mem[310], sizeof keep);
	screen_text(0, 31, s, 0, 11);
	bool ok = true;
	for (int y = 0; y < 10; y++)
		for (int x = 0; x < (int)strlen(s) * 5 && x < WIDTH; x++)
			ok &= lcd_mem[310 + y][x] == keep[y][x];
	memcpy(&lcd_mem[310], keep, sizeof keep);
	return ok;
}

static void write_file(const char *path, const char *text, size_t n)
{
	FILE *f = fopen(path, "wb");
	fwrite(text, 1, n, f);
	fclose(f);
}

int main(int argc, char **argv)
{
	picture = argc > 1 ? argv[1] : NULL;
	strcpy(root, "/tmp/xfertest-card-XXXXXX");
	strcpy(host_dir, "/tmp/xfertest-host-XXXXXX");
	if (!mkdtemp(root) || !mkdtemp(host_dir)) { perror("mkdtemp"); return 2; }
	screen_init();
	vt_init(&term, NULL, NULL, NULL);
	vt_input(&term, (const uint8_t *)"chris@mini:~$ sz deploy.sh\r\n", 28);

	/* sz: a file onto the card */
	char p[300], q[300];
	snprintf(p, sizeof p, "%s/deploy.sh", host_dir);
	write_file(p, "#!/bin/sh\nrsync -a build/ pico:/srv/\n", 37);
	char *sz[] = { "sz", "deploy.sh", NULL };
	host(sz, host_dir);
	check(term.x == 0, "sz: the next output starts a new line");
	snprintf(q, sizeof q, "%s" XFER_DIR "/deploy.sh", root);
	check(same(p, q), "sz: the file arrives in " XFER_DIR);
	screen_draw(&term, false);
	check(bottom_says(" Received deploy.sh (37 bytes)"), "sz: the bottom line says so");
	check(screen_clean(), "sz: nothing left over on the screen");

	/* sz again: the same name replaces it */
	write_file(p, "#!/bin/sh\necho v2\n", 18);
	host(sz, host_dir);
	check(same(p, q), "sz: a second time replaces it");

	/* rz: pick a file on the card and send it */
	snprintf(q, sizeof q, "%s" XFER_DIR "/notes.txt", root);
	write_file(q, "calcterm notes\n", 15);
	snprintf(q, sizeof q, "%s" XFER_DIR "/sub", root);
	mkdir(q, 0777);
	unlink(p);
	static const int pick[] = { SNAP, VK_DOWN, VK_DOWN, VK_ENTER, 0 };	/* "..", sub/, deploy.sh: folders first */
	script = pick; pos = 0;
	char *rz[] = { "rz", NULL };
	host(rz, host_dir);
	check(term.x == 0, "rz: the next output starts a new line");
	snprintf(p, sizeof p, "%s/deploy.sh", host_dir);
	snprintf(q, sizeof q, "%s" XFER_DIR "/deploy.sh", root);
	check(same(p, q), "rz: the chosen file reaches the host");
	screen_draw(&term, false);
	check(bottom_says(" Sent deploy.sh (18 bytes)"), "rz: the bottom line says so");
	check(screen_clean(), "rz: nothing left over on the screen");

	/* sz of a big file, and Esc part way: nothing of it on the screen, no
	   half a file on the card, and the shell's next line on a line of its own */
	{
		char bigp[300];
		snprintf(bigp, sizeof bigp, "%s/big.bin", host_dir);
		FILE *f = fopen(bigp, "wb");
		srand(7);
		for (int i = 0; i < 3000000; i++) putc(rand() & 0xFF, f);
		fclose(f);
		int before = 0;
		for (int r = 0; r < VT_ROWS; r++) { char row[VT_MAX_COLS + 1]; vt_row_text(&term, r, row); for (char *c = row; *c; c++) before += *c != ' '; }
		static const int stop[] = { WAIT - 3, VK_ESC, 0 };
		script = stop; pos = 0;
		char *szbig[] = { "sh", "-c", "sz big.bin; printf '\\r\\nPROMPT\\r\\n'", NULL };
		host(szbig, host_dir);
		int after = 0;
		for (int r = 0; r < VT_ROWS; r++) { char row[VT_MAX_COLS + 1]; vt_row_text(&term, r, row); for (char *c = row; *c; c++) after += *c != ' '; }
		char got[300];
		snprintf(got, sizeof got, "%s" XFER_DIR "/big.bin", root);
		check(access(got, F_OK) != 0, "sz, Esc: no half a file left on the card");
		printf("  (printed characters on the screen: %d before, %d after)\n", before, after);
		check(after - before < 120, "sz, Esc: nothing of the transfer on the screen");
		bool prompt_ok = false;
		for (int r = 0; r < VT_ROWS; r++) { char row[VT_MAX_COLS + 1]; vt_row_text(&term, r, row); if (strncmp(row, "PROMPT", 6) == 0) prompt_ok = true; }
		if (!prompt_ok || getenv("SHOW"))
			for (int r = 0; r < VT_ROWS; r++) { char row[VT_MAX_COLS + 1]; vt_row_text(&term, r, row); if (row[0] != ' ' || row[1] != ' ') printf("  %2d |%s|\n", r + 1, row); }
		check(prompt_ok, "sz, Esc: the shell's next line starts a line");
		unlink(bigp);
		script = NULL;
	}

	/* rz with the file already on the host */
	write_file(p, "old\n", 4);
	static const int again[] = { VK_DOWN, VK_DOWN, VK_ENTER, 0 };
	script = again; pos = 0;
	picture = NULL;
	host(rz, host_dir);
	check(same(p, q), "rz: replaces a file of the same name");

	/* rz, then Esc in the picker: nothing goes, and rz gives up */
	unlink(p);
	static const int cancel[] = { VK_ESC, 0 };
	script = cancel; pos = 0;
	picture = NULL;
	host(rz, host_dir);
	check(access(p, F_OK) != 0, "rz: Esc sends nothing");
	screen_draw(&term, false);
	check(bottom_says(" Nothing sent"), "rz: the bottom line says so");
	check(term.x == 0 || term.y > 0, "rz: Esc leaves the cursor where it was");

	char cmd[200];
	snprintf(cmd, sizeof cmd, "rm -rf %s %s", root, host_dir);
	if (system(cmd)) {}
	printf("%s\n", failed ? "SOME FAILED" : "all ok");
	return failed != 0;
}
