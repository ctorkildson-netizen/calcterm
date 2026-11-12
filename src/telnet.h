/* telnet.h -- the telnet protocol (RFC 854), between calcterm and a TCP
 * connection: it takes the commands out of what comes in, answers the
 * host's questions, and escapes what goes out.
 *
 * Portable: no Pico in it, so the host tests run it against a real telnetd.
 *
 * calcterm offers, and accepts from the host:
 *   BINARY (RFC 856) both ways, so the line carries all eight bits as they
 *     are (ZMODEM needs that);
 *   SUPPRESS-GO-AHEAD (858) both ways, for a full-duplex line;
 *   ECHO (857) from the host: it echoes, and calcterm doesn't;
 *   TERMINAL-TYPE (1091): calcterm's name, so the host sets TERM;
 *   NAWS (1073): the screen's size, sent again when it changes, so the
 *     host knows 64 or 80 columns without calcsize.
 * Anything else is refused.
 */
#ifndef TELNET_H
#define TELNET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { TN_BINARY = 0, TN_ECHO = 1, TN_SGA = 3, TN_TTYPE = 24, TN_NAWS = 31, TN_OPTS = 32 };

typedef struct {
	void (*send)(void *ctx, const uint8_t *s, size_t n);	/* raw, to the connection */
	void *ctx;
	const char *term;		/* the terminal type to give */
	int cols, rows;
	/* where an option stands: 0 off, 1 asked for (waiting), 2 on */
	uint8_t us[TN_OPTS], him[TN_OPTS];
	int state;			/* where we are in a command */
	uint8_t verb;			/* WILL, WONT, DO or DONT */
	uint8_t sb[40];			/* a subnegotiation's bytes */
	size_t sb_n;
	bool cr;			/* the last data byte was CR (not binary: CR NUL is CR) */
} telnet;

/* ready, and say what we'd like */
void telnet_start(telnet *t, void (*send)(void *, const uint8_t *, size_t), void *ctx,
		  const char *term, int cols, int rows);
/* what came in: the data, written to out (at most n bytes); how many */
size_t telnet_in(telnet *t, const uint8_t *in, size_t n, uint8_t *out);
/* data to the host, escaped */
void telnet_out(telnet *t, const uint8_t *s, size_t n);
/* the screen's size changed */
void telnet_size(telnet *t, int cols, int rows);
/* a break (IAC BRK) */
void telnet_break(telnet *t);
/* whether the host echoes what's typed */
bool telnet_host_echoes(const telnet *t);

#endif
