/* zmtest.c -- calcterm's ZMODEM against the real thing: lrzsz's sz and rz,
 * run on a pty, as a host would run them on the serial line.
 *
 *   zmtest            all the tests
 *
 * Files go both ways and are compared byte for byte: text, every byte
 * value, an empty file, a big one, several at once; and with the line
 * damaged now and then, to see the transfer recover.
 */
#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "../src/zmodem.h"

static int master = -1, failed;
static unsigned long noise_every, seen;	/* damage one byte in every noise_every coming in */
static char dir_in[64], dir_out[64];
static long last_progress;

static int z_get(void *ctx, int ms)
{
	(void)ctx;
	struct pollfd p = { master, POLLIN, 0 };
	if (poll(&p, 1, ms) <= 0) return ZM_TIMEOUT;
	uint8_t c;
	if (read(master, &c, 1) != 1) return ZM_TIMEOUT;
	if (noise_every && ++seen % noise_every == 0) c ^= 0x55;
	return c;
}

static void z_put(void *ctx, const uint8_t *s, size_t n)
{
	(void)ctx;
	uint8_t copy[4096];
	if (noise_every && n <= sizeof copy) {		/* the damage goes both ways */
		memcpy(copy, s, n);
		for (size_t i = 0; i < n; i++) if (++seen % noise_every == 0) copy[i] ^= 0x55;
		s = copy;
	}
	while (n) {
		ssize_t w = write(master, s, n);
		if (w < 0) { if (errno == EAGAIN || errno == EINTR) continue; return; }
		s += w, n -= (size_t)w;
	}
}

static void *z_create(void *ctx, const char *name, uint32_t size)
{
	(void)ctx; (void)size;
	char path[256];
	snprintf(path, sizeof path, "%s/%s", dir_in, name);
	return fopen(path, "wb");
}

static bool z_write(void *ctx, void *f, const uint8_t *d, size_t n)
{
	(void)ctx;
	return fwrite(d, 1, n, f) == n;
}

static void z_close(void *ctx, void *f, bool ok)
{
	(void)ctx; (void)ok;
	fclose(f);
}

static int z_read(void *ctx, void *f, uint32_t at, uint8_t *d, size_t n)
{
	(void)ctx;
	if (fseek(f, (long)at, SEEK_SET)) return -1;
	return (int)fread(d, 1, n, f);
}

static void z_progress(void *ctx, const char *name, uint32_t done, uint32_t size)
{
	(void)ctx; (void)name; (void)size;
	last_progress = done;
}

static const zm_io io = { NULL, z_get, z_put, z_create, z_write, z_close, z_read, z_progress };

/* the program on a pty, in dir; its pid */
static pid_t run(const char *dir, char *const argv[])
{
	pid_t pid = forkpty(&master, NULL, NULL, NULL);
	if (pid == 0) {
		if (chdir(dir)) _exit(126);
		int null = open("/dev/null", O_WRONLY);
		(void)null;
		execvp(argv[0], argv);
		_exit(127);
	}
	struct termios t;
	tcgetattr(master, &t);
	cfmakeraw(&t);
	tcsetattr(master, TCSANOW, &t);
	return pid;
}

static void finish(pid_t pid)
{
	for (int i = 0; i < 50; i++) {
		int st;
		if (waitpid(pid, &st, WNOHANG) == pid) { close(master); return; }
		uint8_t b[256];
		struct pollfd p = { master, POLLIN, 0 };
		if (poll(&p, 1, 100) > 0 && read(master, b, sizeof b) <= 0) {}
	}
	kill(pid, SIGKILL);
	waitpid(pid, NULL, 0);
	close(master);
}

static bool same(const char *a, const char *b)
{
	FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
	bool ok = fa && fb;
	while (ok) {
		int x = getc(fa), y = getc(fb);
		if (x != y) ok = false;
		if (x == EOF) break;
	}
	if (fa) fclose(fa);
	if (fb) fclose(fb);
	return ok;
}

static void check(bool ok, const char *what)
{
	printf("%s: %s\n", what, ok ? "ok" : "FAILED");
	if (!ok) failed++;
}

static void make(const char *name, size_t size, int kind)
{
	char path[256];
	snprintf(path, sizeof path, "%s/%s", dir_out, name);
	FILE *f = fopen(path, "wb");
	srand((unsigned)size);
	for (size_t i = 0; i < size; i++) {
		int c = kind == 0 ? "#!/bin/sh\necho hello, PicoCalc\n"[i % 31] : kind == 1 ? (int)(i & 0xFF) : rand() & 0xFF;
		putc(c, f);
	}
	fclose(f);
}

/* sz in dir_out sends names; we receive into dir_in */
static void receive(const char *what, const char *const *names, int count)
{
	char *argv[12] = { "sz", "-q" };
	int a = 2;
	for (int i = 0; i < count; i++) argv[a++] = (char *)names[i];
	argv[a] = NULL;
	pid_t pid = run(dir_out, argv);
	/* as calcterm does: watch for the start, then take over */
	zm_spotter sp = { 0 };
	uint8_t out[8];
	int n, got = -1;
	for (int i = 0; i < 2000 && got < 0; i++) {
		int c = z_get(NULL, 5000);
		if (c < 0) break;
		int s = zm_spot(&sp, (uint8_t)c, out, &n);
		if (s == ZM_SPOT_RECEIVE) {
			char msg[96];
			got = zm_receive(&io, msg, sizeof msg);
			printf("  (%s)\n", msg);
		}
	}
	finish(pid);
	bool ok = got == count;
	for (int i = 0; i < count; i++) {
		char x[256], y[256];
		snprintf(x, sizeof x, "%s/%s", dir_out, names[i]);
		snprintf(y, sizeof y, "%s/%s", dir_in, names[i]);
		ok &= same(x, y);
	}
	check(ok, what);
}

/* rz in dir_in asks; we send name from dir_out */
static void send(const char *what, const char *name)
{
	char *argv[] = { "rz", "-q", NULL };
	char path[256], there[256];
	snprintf(path, sizeof path, "%s/%s", dir_out, name);
	snprintf(there, sizeof there, "%s/%s", dir_in, name);
	unlink(there);
	pid_t pid = run(dir_in, argv);
	zm_spotter sp = { 0 };
	uint8_t out[8];
	int n;
	bool sent = false, spotted = false;
	for (int i = 0; i < 4000 && !spotted; i++) {
		int c = z_get(NULL, 20000);
		if (c < 0) { printf("  (rz never asked)\n"); break; }
		if (zm_spot(&sp, (uint8_t)c, out, &n) == ZM_SPOT_SEND) {
			spotted = true;
			FILE *f = fopen(path, "rb");
			struct stat st;
			stat(path, &st);
			char msg[96];
			struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
			sent = zm_send(&io, f, name, (uint32_t)st.st_size, (uint32_t)st.st_mtime, msg, sizeof msg);
			clock_gettime(CLOCK_MONOTONIC, &t1);
			if (getenv("TIMING")) printf("  (zm_send took %.2f s; data done at %ld bytes)\n", (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9, last_progress);
			fclose(f);
			printf("  (%s)\n", msg);
		}
	}
	finish(pid);
	check(sent && same(path, there), what);
}

int main(void)
{
	strcpy(dir_in, "/tmp/zmtest-in-XXXXXX");
	strcpy(dir_out, "/tmp/zmtest-out-XXXXXX");
	if (!mkdtemp(dir_in) || !mkdtemp(dir_out)) { perror("mkdtemp"); return 2; }
	make("hello.sh", 310, 0);
	make("bytes.bin", 256 * 4, 1);
	make("empty", 0, 1);
	make("big.bin", 300000, 2);
	make("odd.bin", 12345, 2);

	/* the spotter: a start in the middle of text, and text that only looks like one */
	{
		zm_spotter sp = { 0 };
		const char *s = "a ** b *** c \x18 rz\r**\x18" "B00";
		char shown[64] = "";
		uint8_t out[8];
		int n, result = 0;
		for (const char *p = s; *p; p++) {
			int r = zm_spot(&sp, (uint8_t)*p, out, &n);
			strncat(shown, (char *)out, (size_t)n);
			if (r) result = r;
		}
		check(result == ZM_SPOT_RECEIVE && strcmp(shown, "a ** b *** c \x18 rz\r") == 0, "spotting sz's start");
	}

	const char *one[] = { "hello.sh" }, *bytes[] = { "bytes.bin" }, *empty[] = { "empty" };
	const char *big[] = { "big.bin" }, *several[] = { "hello.sh", "odd.bin", "bytes.bin", "empty" };
	receive("receive a script", one, 1);
	receive("receive every byte value", bytes, 1);
	receive("receive an empty file", empty, 1);
	receive("receive 300 KB", big, 1);
	receive("receive four files at once", several, 4);
	noise_every = 7919;
	receive("receive 300 KB through a noisy line", big, 1);
	noise_every = 0;

	send("send a script", "hello.sh");
	send("send every byte value", "bytes.bin");
	send("send an empty file", "empty");
	send("send 300 KB", "big.bin");
	noise_every = 7919;
	send("send 300 KB through a noisy line", "big.bin");
	noise_every = 0;

	char cmd[160];
	snprintf(cmd, sizeof cmd, "rm -rf %s %s", dir_in, dir_out);
	if (system(cmd)) {}
	printf("%s\n", failed ? "SOME FAILED" : "all ok");
	return failed != 0;
}
