/* ptyrun.c -- run a real program against the terminal, on Linux, and show
 * the screen it leaves: the same bytes a host would send down the serial line.
 *
 *   ptyrun [-t TERM] [-w SECONDS] [-k KEYS]... command [args]
 *
 * Each -k sends its keys after a pause (C escapes: \e \r \n \t \xHH, and
 * \p for another half-second's pause).  The terminal's answers go back to
 * the program, as they would over the wire.
 */
#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "../src/vt.h"

static int master = -1;
static vt t;

static void out(const uint8_t *s, size_t n, void *ctx)
{
	(void)ctx;
	if (write(master, s, n) < 0) perror("write");
}

static double now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void pump(double secs)
{
	double end = now() + secs;
	uint8_t buf[4096];
	for (;;) {
		double left = end - now();
		if (left <= 0) return;
		struct pollfd p = { master, POLLIN, 0 };
		if (poll(&p, 1, (int)(left * 1000) + 1) <= 0) continue;
		ssize_t n = read(master, buf, sizeof buf);
		if (n <= 0) return;
		vt_input(&t, buf, (size_t)n);
		if (getenv("PTYRUN_LOG")) { FILE *f = fopen(getenv("PTYRUN_LOG"), "ab"); fwrite(buf, 1, (size_t)n, f); fclose(f); }
	}
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
		case 't': o[n++] = '\t'; s++; break;
		case 'p': o[n++] = 0; s++; break;	/* (a pause, below) */
		case 'x': { unsigned v; sscanf(s + 1, "%2x", &v); o[n++] = (char)v; s += 3; break; }
		default: o[n++] = *s++;
		}
	}
	return n;
}

int main(int argc, char **argv)
{
	const char *term = "vt100";
	double wait = 1.5;
	char *keys[32];
	int nkeys = 0, opt;
	while ((opt = getopt(argc, argv, "+t:w:k:")) != -1) {
		if (opt == 't') term = optarg;
		else if (opt == 'w') wait = atof(optarg);
		else if (opt == 'k' && nkeys < 32) keys[nkeys++] = optarg;
	}
	if (optind >= argc) { fprintf(stderr, "usage: ptyrun [-t TERM] [-w S] [-k KEYS]... cmd\n"); return 2; }

	vt_init(&t, out, NULL, NULL);
	struct winsize ws = { VT_ROWS, VT_COLS, 0, 0 };
	pid_t pid = forkpty(&master, NULL, NULL, &ws);
	if (pid < 0) { perror("forkpty"); return 1; }
	if (pid == 0) {
		setenv("TERM", term, 1);
		setenv("LANG", "C.UTF-8", 1);
		execvp(argv[optind], argv + optind);
		perror("exec");
		_exit(127);
	}
	pump(wait);
	for (int i = 0; i < nkeys; i++) {
		char b[1024];
		size_t n = unescape(keys[i], b), from = 0;
		for (size_t j = 0; j <= n; j++) {
			if (j == n || b[j] == 0) {
				if (j > from) out((uint8_t *)b + from, j - from, NULL);
				pump(0.5);
				from = j + 1;
			}
		}
		pump(0.7);
	}

	printf("+%.*s+\n", VT_COLS, "----------------------------------------------------------------");
	for (int r = 0; r < VT_ROWS; r++) {
		char line[VT_COLS + 1];
		vt_row_text(&t, r, line);
		if (r == t.y) line[t.x] = line[t.x] == ' ' ? '_' : line[t.x];
		printf("|%s|\n", line);
	}
	printf("+%.*s+\n", VT_COLS, "----------------------------------------------------------------");
	kill(pid, SIGHUP);
	waitpid(pid, NULL, 0);
	return 0;
}
