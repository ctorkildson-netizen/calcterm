/* tntest.c -- calcterm's telnet against the real thing: inetutils telnetd,
 * started for each connection by tnserve.py (a shell instead of login).
 *
 *   python3 tnserve.py 2323 &   then   tntest [port]
 *
 * Checks what the host learns from the options (TERM, the screen's size,
 * and the size again after it changes), that a 255 byte survives, and
 * ZMODEM both ways over the connection: sz and rz on the far side,
 * calcterm's own ZMODEM here, files compared byte for byte.
 */
#define _DEFAULT_SOURCE
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../src/telnet.h"
#include "../src/zmodem.h"

static int sock = -1, failed;
static telnet tn;
static uint8_t got[16384];
static size_t got_n, got_i;
static char seen[65536];		/* the text so far, for wait_for */
static size_t seen_n;
static char dir_in[64], dir_out[64];

static void raw_send(void *ctx, const uint8_t *s, size_t n)
{
	(void)ctx;
	while (n) {
		ssize_t w = write(sock, s, n);
		if (w <= 0) return;
		s += w, n -= (size_t)w;
	}
}

/* the next byte of data from the host, or -1 after ms with none */
static int get(int ms)
{
	while (got_i == got_n) {
		struct pollfd p = { sock, POLLIN, 0 };
		if (poll(&p, 1, ms) <= 0) return -1;
		uint8_t raw[4096];
		ssize_t r = read(sock, raw, sizeof raw);
		if (r <= 0) return -1;
		got_n = telnet_in(&tn, raw, (size_t)r, got);
		got_i = 0;
	}
	return got[got_i++];
}

static void type(const char *s) { telnet_out(&tn, (const uint8_t *)s, strlen(s)); }

static bool wait_for(const char *what, int ms)
{
	seen_n = 0;
	for (;;) {
		int c = get(ms);
		if (c < 0) return false;
		if (seen_n < sizeof seen - 1) { seen[seen_n++] = (char)c; seen[seen_n] = 0; }
		if (memmem(seen, seen_n, what, strlen(what))) return true;
	}
}

static void check(bool ok, const char *what)
{
	printf("%s: %s\n", what, ok ? "ok" : "FAILED");
	if (!ok) failed++;
}

/* ZMODEM's i/o, through the telnet layer */
static int z_get(void *ctx, int ms) { (void)ctx; int c = get(ms); return c < 0 ? ZM_TIMEOUT : c; }
static void z_put(void *ctx, const uint8_t *s, size_t n) { (void)ctx; telnet_out(&tn, s, n); }
static void *z_create(void *ctx, const char *name, uint32_t size)
{
	(void)ctx; (void)size;
	char p[256];
	snprintf(p, sizeof p, "%s/%s", dir_in, name);
	return fopen(p, "wb");
}
static bool z_write(void *ctx, void *f, const uint8_t *d, size_t n) { (void)ctx; return fwrite(d, 1, n, f) == n; }
static void z_close(void *ctx, void *f, bool ok) { (void)ctx; (void)ok; fclose(f); }
static int z_read(void *ctx, void *f, uint32_t at, uint8_t *d, size_t n)
{
	(void)ctx;
	if (fseek(f, (long)at, SEEK_SET)) return -1;
	return (int)fread(d, 1, n, f);
}
static void z_progress(void *ctx, const char *name, uint32_t done, uint32_t size) { (void)ctx; (void)name; (void)done; (void)size; }
static const zm_io io = { NULL, z_get, z_put, z_create, z_write, z_close, z_read, z_progress };

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

static void make(const char *name, size_t size)
{
	char p[256];
	snprintf(p, sizeof p, "%s/%s", dir_out, name);
	FILE *f = fopen(p, "wb");
	srand((unsigned)size);
	for (size_t i = 0; i < size; i++) {
		/* every byte value, then runs that matter to telnet: 255s, CR NUL, CR LF */
		int c = i < 256 ? (int)i : i % 97 < 8 ? 255 : i % 89 == 0 ? '\r' : i % 89 == 1 ? 0 : rand() & 0xFF;
		putc(c, f);
	}
	fclose(f);
}

int main(int argc, char **argv)
{
	int port = argc > 1 ? atoi(argv[1]) : 2323;
	sock = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons((uint16_t)port) };
	a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (connect(sock, (struct sockaddr *)&a, sizeof a)) { perror("connect"); return 2; }
	telnet_start(&tn, raw_send, NULL, "calcterm", 64, 32);

	type("PS1='$ '; echo T=$TERM; stty size\r");
	check(wait_for("T=calcterm", 5000), "the host's TERM is calcterm");
	check(wait_for("32 64", 3000), "the host knows 64x32");
	printf("  (binary: us %d, him %d)\n", tn.us[TN_BINARY], tn.him[TN_BINARY]);
	/* (telnetd takes binary from us but keeps its own side as text: CR NUL
	   for CR, which the transfers below check comes out right) */
	check(tn.us[TN_BINARY] == 2, "binary to the host");
	check(telnet_host_echoes(&tn), "the host echoes");

	telnet_size(&tn, 80, 32);
	type("stty size\r");
	check(wait_for("32 80", 3000), "the host knows 80 columns after a change");

	type("printf 'A\\377B\\377\\377C\\n'\r");
	check(wait_for("A\xff" "B\xff\xff" "C", 3000), "255s from the host");
	type("od -An -tx1 <<< $'\\xff'\r");
	wait_for("$ ", 3000);

	strcpy(dir_in, "/tmp/tntest-in-XXXXXX");
	strcpy(dir_out, "/tmp/tntest-out-XXXXXX");
	if (!mkdtemp(dir_in) || !mkdtemp(dir_out)) { perror("mkdtemp"); return 2; }
	make("tricky.bin", 200000);

	/* sz on the far side: we receive */
	char cmd[256];
	snprintf(cmd, sizeof cmd, "cd %s; sz -q tricky.bin\r", dir_out);
	type(cmd);
	{
		zm_spotter sp = { 0 };
		uint8_t out[8];
		int n, r = -1;
		for (int i = 0; i < 100000 && r < 0; i++) {
			int c = get(10000);
			if (c < 0) break;
			if (zm_spot(&sp, (uint8_t)c, out, &n) == ZM_SPOT_RECEIVE) {
				char msg[96];
				r = zm_receive(&io, msg, sizeof msg);
				printf("  (%s)\n", msg);
			}
		}
		char x[256], y[256];
		snprintf(x, sizeof x, "%s/tricky.bin", dir_out);
		snprintf(y, sizeof y, "%s/tricky.bin", dir_in);
		check(r == 1 && same(x, y), "sz over telnet, 200 KB with 255s and CR NUL");
	}
	wait_for("$ ", 5000);

	/* rz on the far side: we send */
	snprintf(cmd, sizeof cmd, "cd %s; rz -q\r", dir_in);
	char back[256];
	snprintf(back, sizeof back, "%s/back.bin", dir_in);
	type(cmd);
	{
		zm_spotter sp = { 0 };
		uint8_t out[8];
		int n;
		bool ok = false;
		for (int i = 0; i < 100000; i++) {
			int c = get(10000);
			if (c < 0) break;
			if (zm_spot(&sp, (uint8_t)c, out, &n) == ZM_SPOT_SEND) {
				char path[256], msg[96];
				snprintf(path, sizeof path, "%s/tricky.bin", dir_out);
				FILE *f = fopen(path, "rb");
				struct stat st;
				stat(path, &st);
				ok = zm_send(&io, f, "back.bin", (uint32_t)st.st_size, (uint32_t)st.st_mtime, msg, sizeof msg);
				fclose(f);
				printf("  (%s)\n", msg);
				break;
			}
		}
		wait_for("$ ", 8000);
		char x[256];
		snprintf(x, sizeof x, "%s/tricky.bin", dir_out);
		check(ok && same(x, back), "rz over telnet, 200 KB with 255s and CR NUL");
	}

	type("exit\r");
	close(sock);
	snprintf(cmd, sizeof cmd, "rm -rf %s %s", dir_in, dir_out);
	if (system(cmd)) {}
	printf("%s\n", failed ? "SOME FAILED" : "all ok");
	return failed != 0;
}
