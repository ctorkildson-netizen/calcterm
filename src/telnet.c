/* telnet.c -- the telnet protocol: see telnet.h.
 *
 * Options are agreed as RFC 1143 asks, to stay out of loops: a request is
 * answered only when it would change something, and a request we made is
 * remembered (1, "asked") so the host's agreement isn't taken for a new
 * request and answered again.
 */
#include <string.h>
#include "telnet.h"

enum {
	SE = 240, NOP = 241, DM = 242, BRK = 243, IP = 244, AO = 245, AYT = 246,
	EC = 247, EL = 248, GA = 249, SB = 250, WILL = 251, WONT = 252, DO = 253,
	DONT = 254, IAC = 255
};
enum { S_DATA, S_IAC, S_OPT, S_SB, S_SB_IAC };
enum { TT_IS = 0, TT_SEND = 1 };

static void put(telnet *t, const uint8_t *s, size_t n) { t->send(t->ctx, s, n); }

static void cmd(telnet *t, uint8_t verb, uint8_t opt)
{
	uint8_t b[3] = { IAC, verb, opt };
	put(t, b, 3);
}

static bool we_can(int opt) { return opt == TN_BINARY || opt == TN_SGA || opt == TN_TTYPE || opt == TN_NAWS; }
static bool he_may(int opt) { return opt == TN_BINARY || opt == TN_SGA || opt == TN_ECHO; }

/* NAWS: the width and the height, 16 bits each, an IAC in them doubled */
static void send_size(telnet *t)
{
	uint8_t b[16];
	size_t k = 0;
	b[k++] = IAC; b[k++] = SB; b[k++] = TN_NAWS;
	uint8_t v[4] = { (uint8_t)(t->cols >> 8), (uint8_t)t->cols, (uint8_t)(t->rows >> 8), (uint8_t)t->rows };
	for (int i = 0; i < 4; i++) {
		b[k++] = v[i];
		if (v[i] == IAC) b[k++] = IAC;
	}
	b[k++] = IAC; b[k++] = SE;
	put(t, b, k);
}

static void send_ttype(telnet *t)
{
	uint8_t b[48];
	size_t k = 0, n = strlen(t->term);
	if (n > sizeof b - 6) n = sizeof b - 6;
	b[k++] = IAC; b[k++] = SB; b[k++] = TN_TTYPE; b[k++] = TT_IS;
	memcpy(b + k, t->term, n);
	k += n;
	b[k++] = IAC; b[k++] = SE;
	put(t, b, k);
}

void telnet_start(telnet *t, void (*send)(void *, const uint8_t *, size_t), void *ctx,
		  const char *term, int cols, int rows)
{
	memset(t, 0, sizeof *t);
	t->send = send;
	t->ctx = ctx;
	t->term = term;
	t->cols = cols;
	t->rows = rows;
	/* what we'd like; the host may say no to any of it */
	static const uint8_t ours[] = { TN_BINARY, TN_SGA, TN_TTYPE, TN_NAWS };
	static const uint8_t his[] = { TN_BINARY, TN_SGA };
	for (size_t i = 0; i < sizeof ours; i++) { t->us[ours[i]] = 1; cmd(t, WILL, ours[i]); }
	for (size_t i = 0; i < sizeof his; i++) { t->him[his[i]] = 1; cmd(t, DO, his[i]); }
}

/* the host's WILL, WONT, DO or DONT */
static void option(telnet *t, uint8_t verb, uint8_t opt)
{
	bool ours = verb == DO || verb == DONT, yes = verb == WILL || verb == DO;
	if (opt >= TN_OPTS) {				/* nothing we know of */
		if (yes) cmd(t, ours ? WONT : DONT, opt);
		return;
	}
	uint8_t *st = ours ? &t->us[opt] : &t->him[opt];
	bool ok = ours ? we_can(opt) : he_may(opt);
	if (yes) {
		if (*st == 2) return;			/* already on */
		if (!ok) { cmd(t, ours ? WONT : DONT, opt); return; }
		if (*st == 0) cmd(t, ours ? WILL : DO, opt);	/* (1: it's the answer to ours) */
		*st = 2;
		if (ours && opt == TN_NAWS) send_size(t);
	} else {
		if (*st == 0) return;			/* already off */
		if (*st == 2) cmd(t, ours ? WONT : DONT, opt);	/* (1: it's a no to ours) */
		*st = 0;
	}
}

static void subneg(telnet *t)
{
	if (t->sb_n >= 2 && t->sb[0] == TN_TTYPE && t->sb[1] == TT_SEND && t->us[TN_TTYPE] == 2)
		send_ttype(t);
}

size_t telnet_in(telnet *t, const uint8_t *in, size_t n, uint8_t *out)
{
	size_t k = 0;
	for (size_t i = 0; i < n; i++) {
		uint8_t c = in[i];
		switch (t->state) {
		case S_DATA:
			if (c == IAC) { t->state = S_IAC; break; }
			/* not binary, CR NUL stands for CR alone */
			if (c == 0 && t->cr && t->him[TN_BINARY] != 2) { t->cr = false; break; }
			t->cr = c == '\r';
			out[k++] = c;
			break;
		case S_IAC:
			t->state = S_DATA;
			if (c == IAC) { out[k++] = IAC; t->cr = false; }	/* a 255 in the data */
			else if (c >= WILL) { t->verb = c; t->state = S_OPT; }
			else if (c == SB) { t->sb_n = 0; t->state = S_SB; }
			/* NOP, DM, GA and the rest: nothing to do */
			break;
		case S_OPT:
			option(t, t->verb, c);
			t->state = S_DATA;
			break;
		case S_SB:
			if (c == IAC) t->state = S_SB_IAC;
			else if (t->sb_n < sizeof t->sb) t->sb[t->sb_n++] = c;
			break;
		case S_SB_IAC:
			if (c == SE) { subneg(t); t->state = S_DATA; }
			else {
				if (c == IAC && t->sb_n < sizeof t->sb) t->sb[t->sb_n++] = IAC;
				t->state = S_SB;
			}
			break;
		}
	}
	return k;
}

void telnet_out(telnet *t, const uint8_t *s, size_t n)
{
	uint8_t b[256];
	size_t k = 0;
	bool binary = t->us[TN_BINARY] == 2;
	for (size_t i = 0; i < n; i++) {
		if (k > sizeof b - 2) { put(t, b, k); k = 0; }
		b[k++] = s[i];
		if (s[i] == IAC) b[k++] = IAC;
		/* not binary, a CR alone goes as CR NUL */
		else if (s[i] == '\r' && !binary && !(i + 1 < n && s[i + 1] == '\n')) b[k++] = 0;
	}
	if (k) put(t, b, k);
}

void telnet_size(telnet *t, int cols, int rows)
{
	if (cols == t->cols && rows == t->rows) return;
	t->cols = cols;
	t->rows = rows;
	if (t->us[TN_NAWS] == 2) send_size(t);
}

void telnet_break(telnet *t)
{
	uint8_t b[2] = { IAC, BRK };
	put(t, b, 2);
}

bool telnet_host_echoes(const telnet *t)
{
	return t->him[TN_ECHO] == 2;
}
