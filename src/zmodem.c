/* zmodem.c -- ZMODEM file transfer, after Chuck Forsberg's description of
 * the protocol (1988) and what lrzsz, the sz and rz on Linux, do.
 *
 * Kept to what's needed to talk to lrzsz and its kin: one file at a time
 * going out, any number coming in, CRC-16 or CRC-32, with recovery from
 * damaged packets by asking for the data again from where it went wrong.
 * calcterm says it can take 8 KB before it must answer, so the host waits
 * now and then for it to catch up (the SD card is slow at times).
 */
#include <stdio.h>
#include <string.h>
#include "zmodem.h"

#define ZPAD '*'
#define ZDLE 0x18
#define ZBIN 'A'
#define ZHEX 'B'
#define ZBIN32 'C'

/* frame types */
enum { ZRQINIT, ZRINIT, ZSINIT, ZACK, ZFILE, ZSKIP, ZNAK, ZABORT, ZFIN, ZRPOS,
       ZDATA, ZEOF, ZFERR, ZCRC, ZCHALLENGE, ZCOMPL, ZCAN, ZFREECNT, ZCOMMAND };
/* how a data subpacket ends */
#define ZCRCE 'h'		/* end of frame: a header follows */
#define ZCRCG 'i'		/* more data follows, no answer wanted */
#define ZCRCQ 'j'		/* more follows; answer with ZACK */
#define ZCRCW 'k'		/* answer with ZACK, then a header follows */
/* ZRINIT's flags (ZF0) */
#define CANFDX 0x01
#define CANOVIO 0x02
#define CANFC32 0x20
#define ESCCTL 0x40
/* ZFILE's (ZF0, ZF1) */
#define ZCBIN 1
#define ZMCLOB 4

/* what the readers below return, beside bytes */
#define GOT_END 0x100		/* | the frame-end character */
#define ERR_TIMEOUT -1
#define ERR_CANCEL -2		/* the user, or the other end (five CANs) */
#define ERR_CRC -3
#define ERR_BAD -4

#define RXBUF 8192		/* what we take before we must answer */
#define BLOCK 1024		/* our data subpackets */
#define TRIES 10
#define WAIT_MS 10000

/* ------------------------------------------------------------ CRCs */

static uint16_t crc16(uint16_t crc, uint8_t b)
{
	crc ^= (uint16_t)(b << 8);
	for (int i = 0; i < 8; i++) crc = crc & 0x8000 ? (uint16_t)(crc << 1 ^ 0x1021) : (uint16_t)(crc << 1);
	return crc;
}

static uint32_t crc32(uint32_t crc, uint8_t b)
{
	crc ^= b;
	for (int i = 0; i < 8; i++) crc = crc & 1 ? crc >> 1 ^ 0xEDB88320u : crc >> 1;
	return crc;
}

/* ------------------------------------------------------------ the line */

typedef struct {
	const zm_io *io;
	int cans;		/* CANs in a row */
	bool escctl;		/* the other end wants every control character escaped */
	uint8_t out[2 * BLOCK + 64];
	size_t n_out;
} line;

static void flush(line *l)
{
	if (l->n_out) l->io->put(l->io->ctx, l->out, l->n_out);
	l->n_out = 0;
}

static void raw(line *l, uint8_t c)
{
	if (l->n_out == sizeof l->out) flush(l);
	l->out[l->n_out++] = c;
}

/* a byte of a binary header or of data, escaped where it must be */
static void esc(line *l, uint8_t c)
{
	switch (c) {
	case ZDLE: case 0x10: case 0x11: case 0x13: case 0x90: case 0x91: case 0x93:
	case 0x0D: case 0x8D:
		raw(l, ZDLE);
		raw(l, c ^ 0x40);
		return;
	}
	if (l->escctl && (c & 0x60) == 0) { raw(l, ZDLE); raw(l, c ^ 0x40); return; }
	raw(l, c);
}

/* the next byte off the line, flow-control bytes left out, five CANs noticed */
static int get(line *l, int ms)
{
	for (;;) {
		int c = l->io->get(l->io->ctx, ms);
		if (c < 0) return c == ZM_CANCEL ? ERR_CANCEL : ERR_TIMEOUT;
		if (c == 0x11 || c == 0x13 || c == 0x91 || c == 0x93) continue;
		if (c == ZDLE) { if (++l->cans >= 5) return ERR_CANCEL; }
		else l->cans = 0;
		return c;
	}
}

/* an escaped byte: the byte, GOT_END | the frame end, or an error */
static int get_esc(line *l, int ms)
{
	int c = get(l, ms);
	if (c != ZDLE) return c;
	c = get(l, ms);
	if (c < 0) return c;
	if (c >= ZCRCE && c <= ZCRCW) return GOT_END | c;
	if (c == 'l') return 0x7F;
	if (c == 'm') return 0xFF;
	if (c == ZDLE) return ERR_BAD;
	if ((c & 0x60) == 0x40) return c ^ 0x40;
	return ERR_BAD;
}

/* ------------------------------------------------------------ headers */

typedef struct { int type; uint8_t b[4]; bool crc32; } header;

static uint32_t pos_of(const header *h)
{
	return (uint32_t)h->b[0] | (uint32_t)h->b[1] << 8 | (uint32_t)h->b[2] << 16 | (uint32_t)h->b[3] << 24;
}

static void set_pos(uint8_t *b, uint32_t p)
{
	b[0] = (uint8_t)p, b[1] = (uint8_t)(p >> 8), b[2] = (uint8_t)(p >> 16), b[3] = (uint8_t)(p >> 24);
}

static void hex2(line *l, uint8_t v)
{
	static const char d[] = "0123456789abcdef";
	raw(l, (uint8_t)d[v >> 4]);
	raw(l, (uint8_t)d[v & 15]);
}

static void send_hex(line *l, int type, const uint8_t b[4])
{
	raw(l, ZPAD); raw(l, ZPAD); raw(l, ZDLE); raw(l, ZHEX);
	uint16_t crc = crc16(0, (uint8_t)type);
	hex2(l, (uint8_t)type);
	for (int i = 0; i < 4; i++) { hex2(l, b[i]); crc = crc16(crc, b[i]); }
	hex2(l, (uint8_t)(crc >> 8));
	hex2(l, (uint8_t)crc);
	raw(l, '\r'); raw(l, 0x8A);
	if (type != ZFIN && type != ZACK) raw(l, 0x11);	/* XON, in case the host was paused */
	flush(l);
}

static void send_hex_pos(line *l, int type, uint32_t p)
{
	uint8_t b[4];
	set_pos(b, p);
	send_hex(l, type, b);
}

static void send_bin(line *l, int type, const uint8_t b[4], bool use32)
{
	raw(l, ZPAD); raw(l, ZDLE); raw(l, use32 ? ZBIN32 : ZBIN);
	esc(l, (uint8_t)type);
	if (use32) {
		uint32_t crc = crc32(0xFFFFFFFFu, (uint8_t)type);
		for (int i = 0; i < 4; i++) { esc(l, b[i]); crc = crc32(crc, b[i]); }
		crc = ~crc;
		for (int i = 0; i < 4; i++) esc(l, (uint8_t)(crc >> (8 * i)));
	} else {
		uint16_t crc = crc16(0, (uint8_t)type);
		for (int i = 0; i < 4; i++) { esc(l, b[i]); crc = crc16(crc, b[i]); }
		esc(l, (uint8_t)(crc >> 8));
		esc(l, (uint8_t)crc);
	}
}

static int hex_digit(line *l, int ms)
{
	int c = get(l, ms);
	if (c < 0) return c;
	c &= 0x7F;
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	return ERR_BAD;
}

static int hex_byte(line *l, int ms)
{
	int hi = hex_digit(l, ms);
	if (hi < 0) return hi;
	int lo = hex_digit(l, ms);
	if (lo < 0) return lo;
	return hi << 4 | lo;
}

/* the next header: its type, or an error (garbage before it is skipped) */
static int get_header(line *l, header *h, int ms)
{
	int garbage = 0;
	for (;;) {
		int c = get(l, ms);
		if (c < 0) return c;
		if (c != ZPAD) { if (++garbage > 2 * RXBUF) return ERR_BAD; continue; }
		do c = get(l, ms); while (c == ZPAD);
		if (c < 0) return c;
		if (c != ZDLE) continue;
		int kind = get(l, ms);
		if (kind < 0) return kind;
		int v[7], n = kind == ZBIN32 ? 9 : 7;
		if (kind == ZHEX) {
			for (int i = 0; i < 7; i++) if ((v[i] = hex_byte(l, ms)) < 0) { if (v[i] == ERR_BAD) goto again; return v[i]; }
			uint16_t crc = 0;
			for (int i = 0; i < 5; i++) crc = crc16(crc, (uint8_t)v[i]);
			if (crc != (v[5] << 8 | v[6])) goto again;
			/* (the CR, LF and XON after it are skipped as garbage next time) */
			h->crc32 = false;
		} else if (kind == ZBIN || kind == ZBIN32) {
			int w[9];
			for (int i = 0; i < n; i++) {
				w[i] = get_esc(l, ms);
				if (w[i] < 0) { if (w[i] == ERR_BAD) goto again; return w[i]; }
				if (w[i] & GOT_END) goto again;
			}
			if (kind == ZBIN) {
				uint16_t crc = 0;
				for (int i = 0; i < 5; i++) crc = crc16(crc, (uint8_t)w[i]);
				if (crc != (w[5] << 8 | w[6])) goto again;
			} else {
				uint32_t crc = 0xFFFFFFFFu;
				for (int i = 0; i < 5; i++) crc = crc32(crc, (uint8_t)w[i]);
				crc = ~crc;
				if (crc != ((uint32_t)w[5] | (uint32_t)w[6] << 8 | (uint32_t)w[7] << 16 | (uint32_t)w[8] << 24)) goto again;
			}
			for (int i = 0; i < 5; i++) v[i] = w[i];
			h->crc32 = kind == ZBIN32;
		} else {
			continue;
		}
		h->type = v[0];
		for (int i = 0; i < 4; i++) h->b[i] = (uint8_t)v[i + 1];
		return h->type;
	again:
		if (++garbage > 2 * RXBUF) return ERR_BAD;
	}
}

/* ------------------------------------------------------------ data subpackets */

/* a subpacket into buf: the frame end (ZCRCE and so on), or an error */
static int get_data(line *l, bool use32, uint8_t *buf, size_t max, size_t *len)
{
	*len = 0;
	uint16_t c16 = 0;
	uint32_t c32 = 0xFFFFFFFFu;
	for (;;) {
		int c = get_esc(l, WAIT_MS);
		if (c < 0) return c;
		if (c & GOT_END) {
			int end = c & 0xFF;
			if (use32) {
				c32 = crc32(c32, (uint8_t)end);
				uint32_t want = 0;
				for (int i = 0; i < 4; i++) {
					int b = get_esc(l, WAIT_MS);
					if (b < 0) return b;
					if (b & GOT_END) return ERR_CRC;
					want |= (uint32_t)b << (8 * i);
				}
				if (~c32 != want) return ERR_CRC;
			} else {
				c16 = crc16(c16, (uint8_t)end);
				int hi = get_esc(l, WAIT_MS), lo;
				if (hi < 0) return hi;
				lo = get_esc(l, WAIT_MS);
				if (lo < 0) return lo;
				if ((hi | lo) & GOT_END || c16 != (hi << 8 | lo)) return ERR_CRC;
			}
			return end;
		}
		if (*len >= max) return ERR_BAD;
		buf[(*len)++] = (uint8_t)c;
		if (use32) c32 = crc32(c32, (uint8_t)c);
		else c16 = crc16(c16, (uint8_t)c);
	}
}

static void send_data(line *l, const uint8_t *d, size_t n, int end, bool use32)
{
	if (use32) {
		uint32_t crc = 0xFFFFFFFFu;
		for (size_t i = 0; i < n; i++) { esc(l, d[i]); crc = crc32(crc, d[i]); }
		raw(l, ZDLE); raw(l, (uint8_t)end);
		crc = ~crc32(crc, (uint8_t)end);
		for (int i = 0; i < 4; i++) esc(l, (uint8_t)(crc >> (8 * i)));
	} else {
		uint16_t crc = 0;
		for (size_t i = 0; i < n; i++) { esc(l, d[i]); crc = crc16(crc, d[i]); }
		raw(l, ZDLE); raw(l, (uint8_t)end);
		crc = crc16(crc, (uint8_t)end);
		esc(l, (uint8_t)(crc >> 8));
		esc(l, (uint8_t)crc);
	}
	if (end == ZCRCW) raw(l, 0x11);
	flush(l);
}

static void cancel(line *l)
{
	static const uint8_t c[] = { ZDLE, ZDLE, ZDLE, ZDLE, ZDLE, ZDLE, ZDLE, ZDLE, 8, 8, 8, 8, 8, 8, 8, 8 };
	flush(l);
	l->io->put(l->io->ctx, c, sizeof c);
}

void zm_cancel(const zm_io *io)
{
	line l = { .io = io };
	cancel(&l);
}

static void say(char *msg, size_t size, const char *s)
{
	if (msg && size) snprintf(msg, size, "%s", s);
}

/* ------------------------------------------------------------ spotting a start */

int zm_spot(zm_spotter *s, uint8_t c, uint8_t *out, int *n_out)
{
	static const uint8_t start[] = { ZPAD, ZPAD, ZDLE, ZHEX, '0' };
	*n_out = 0;
	if (s->n == (int)sizeof start) {	/* the type's second digit */
		s->n = 0;
		if (c == '0') return ZM_SPOT_RECEIVE;	/* ZRQINIT: sz */
		if (c == '1') return ZM_SPOT_SEND;	/* ZRINIT: rz */
		memcpy(out, start, sizeof start);
		*n_out = sizeof start;
		out[(*n_out)++] = c;
		return ZM_SPOT_NONE;
	}
	s->held[s->n++] = c;
	/* not a start after all: let go of bytes from the front until what's
	   left could still be one */
	for (;;) {
		bool could = true;
		for (int i = 0; i < s->n; i++) could &= s->held[i] == start[i];
		if (could) break;
		out[(*n_out)++] = s->held[0];
		memmove(s->held, s->held + 1, (size_t)--s->n);
	}
	return ZM_SPOT_NONE;
}

/* ------------------------------------------------------------ receiving */

static void send_zrinit(line *l)
{
	uint8_t b[4] = { RXBUF & 0xFF, RXBUF >> 8, 0, CANFDX | CANOVIO | CANFC32 };
	send_hex(l, ZRINIT, b);
}

/* ZFILE's subpacket: "name\0size mtime mode ...\0"; the name without its folders */
static void file_info(const uint8_t *d, size_t n, char *name, size_t name_size, uint32_t *size)
{
	size_t len = 0;
	while (len < n && d[len]) len++;
	const char *base = (const char *)d;
	for (size_t i = 0; i < len; i++) if (d[i] == '/' || d[i] == '\\') base = (const char *)d + i + 1;
	snprintf(name, name_size, "%.*s", (int)(len - (size_t)(base - (const char *)d)), base);
	*size = 0;
	for (size_t i = len + 1; i < n && d[i] >= '0' && d[i] <= '9'; i++) *size = *size * 10 + (uint32_t)(d[i] - '0');
}

int zm_receive(const zm_io *io, char *msg, size_t msg_size)
{
	line l = { .io = io };
	static uint8_t buf[BLOCK * 8 + 16];
	char name[64] = "", m[96];
	int files = 0, tries = 0;
	header h;
	say(msg, msg_size, "Nothing came");
	send_zrinit(&l);
	for (;;) {			/* between files */
		int t = get_header(&l, &h, WAIT_MS);
		if (t == ERR_CANCEL) { cancel(&l); say(msg, msg_size, files ? "Stopped" : "Cancelled"); return files; }
		if (t < 0 || t == ZRQINIT || t == ZNAK) {
			if (++tries > TRIES) { cancel(&l); say(msg, msg_size, "The host stopped answering"); return files; }
			send_zrinit(&l);
			continue;
		}
		tries = 0;
		size_t n;
		if (t == ZSINIT) {
			get_data(&l, h.crc32, buf, sizeof buf, &n);
			send_hex_pos(&l, ZACK, 1);
			continue;
		}
		if (t == ZFIN) {
			send_hex_pos(&l, ZFIN, 0);
			/* "OO": over and out, after the CR and LF that end its ZFIN */
			for (int o = 0, n = 0; o < 2 && n < 8; n++) {
				int c = get(&l, 500);
				if (c < 0) break;
				if (c == 'O') o++;
			}
			return files;
		}
		if (t == ZFREECNT) { send_hex_pos(&l, ZACK, 0x7FFFFFFF); continue; }
		if (t == ZCOMMAND) { get_data(&l, h.crc32, buf, sizeof buf, &n); send_hex_pos(&l, ZCOMPL, 0); continue; }
		if (t != ZFILE) { send_zrinit(&l); continue; }

		/* a file */
		int r = get_data(&l, h.crc32, buf, sizeof buf - 1, &n);
		if (r < 0) { send_hex_pos(&l, ZNAK, 0); continue; }
		buf[n] = 0;
		uint32_t size;
		file_info(buf, n, name, sizeof name, &size);
		void *f = name[0] ? io->create(io->ctx, name, size) : NULL;
		if (!f) {
			snprintf(m, sizeof m, "Couldn't make %.40s on the card", name);
			say(msg, msg_size, m);
			send_hex_pos(&l, ZSKIP, 0);
			continue;
		}
		uint32_t at = 0;
		bool ok = false;
		send_hex_pos(&l, ZRPOS, 0);
		if (io->progress) io->progress(io->ctx, name, 0, size);
		for (tries = 0; tries <= TRIES; ) {	/* the file's data */
			t = get_header(&l, &h, WAIT_MS);
			if (t == ERR_CANCEL) break;
			if (t == ZEOF) {
				if (pos_of(&h) != at) continue;		/* (an old one: the data's still coming) */
				ok = true;
				break;
			}
			if (t == ZFILE) { get_data(&l, h.crc32, buf, sizeof buf, &n); send_hex_pos(&l, ZRPOS, at); continue; }
			if (t == ZFIN) break;
			if (t != ZDATA || pos_of(&h) != at) {
				tries++;
				send_hex_pos(&l, ZRPOS, at);
				continue;
			}
			/* subpackets, until the frame ends */
			for (;;) {
				r = get_data(&l, h.crc32, buf, sizeof buf, &n);
				if (r < 0) {
					if (r == ERR_CANCEL) { t = ERR_CANCEL; break; }
					tries++;
					send_hex_pos(&l, ZRPOS, at);	/* from where it went wrong */
					break;
				}
				if (n && !io->write(io->ctx, f, buf, n)) { t = ERR_BAD; break; }
				at += (uint32_t)n;
				tries = 0;
				if (io->progress) io->progress(io->ctx, name, at, size);
				if (r == ZCRCW || r == ZCRCQ) send_hex_pos(&l, ZACK, at);
				if (r == ZCRCW || r == ZCRCE) break;
			}
			if (t == ERR_CANCEL || t == ERR_BAD) break;
		}
		io->close(io->ctx, f, ok);
		if (!ok) {
			cancel(&l);
			snprintf(m, sizeof m, t == ERR_BAD ? "Couldn't write %.40s to the card" : "%.40s didn't all come", name);
			say(msg, msg_size, m);
			return files;
		}
		files++;
		snprintf(m, sizeof m, "Received %.40s (%lu bytes)", name, (unsigned long)at);
		say(msg, msg_size, m);
		send_zrinit(&l);
		tries = 0;
	}
}

/* ------------------------------------------------------------ sending */

/* the data from at to the end, then ZEOF; true when the host has it all */
static bool send_file_data(line *l, void *f, const char *name, uint32_t size, uint32_t at,
			   bool use32, uint32_t rxbuf, char *msg, size_t msg_size)
{
	const zm_io *io = l->io;
	static uint8_t d[BLOCK];
	header h;
	int tries = 0;
	uint32_t furthest = at;		/* (errors only count while there's no headway) */
	for (;;) {
		uint8_t b[4];
		if (at > furthest) { furthest = at; tries = 0; }
		set_pos(b, at);
		send_bin(l, ZDATA, b, use32);
		uint32_t since_ack = 0;
		bool restart = false;
		for (;;) {
			int n = io->read(io->ctx, f, at, d, BLOCK);
			if (n < 0) { cancel(l); say(msg, msg_size, "Couldn't read the file"); return false; }
			bool last = at + (uint32_t)n >= size || n == 0;
			int end = last ? ZCRCE : ZCRCG;
			if (!last && rxbuf && since_ack + 2 * BLOCK > rxbuf) end = ZCRCW;
			send_data(l, d, (size_t)n, end, use32);
			at += (uint32_t)n;
			since_ack += (uint32_t)n;
			if (io->progress) io->progress(io->ctx, name, at, size);
			if (end == ZCRCW) {		/* wait for it to catch up */
				int t = get_header(l, &h, WAIT_MS);
				if (t == ERR_CANCEL) { say(msg, msg_size, "Cancelled"); cancel(l); return false; }
				if (t == ZACK) { since_ack = 0; set_pos(b, at); send_bin(l, ZDATA, b, use32); continue; }
				if (t == ZRPOS) { at = pos_of(&h); restart = true; break; }
				if (++tries > TRIES) { cancel(l); say(msg, msg_size, "The host stopped answering"); return false; }
				restart = true;		/* (no answer: go again from where it last said) */
				break;
			}
			/* the host may have something to say: a damaged packet */
			int c = io->get(io->ctx, 0);
			if (c == ZM_CANCEL) { cancel(l); say(msg, msg_size, "Cancelled"); return false; }
			if (c == ZPAD || c == ZDLE) {
				int t = get_header(l, &h, 1000);
				if (t == ERR_CANCEL) { say(msg, msg_size, "Cancelled"); return false; }
				if (t == ZRPOS) {
					send_data(l, d, 0, ZCRCE, use32);	/* end this frame */
					at = pos_of(&h);
					restart = true;
					break;
				}
				if (t == ZSKIP) { say(msg, msg_size, "The host skipped it"); return false; }
			}
			if (last) break;
		}
		if (restart) { if (++tries > TRIES * 3) { cancel(l); say(msg, msg_size, "Too many errors"); return false; } continue; }
		/* the end: ZEOF, and wait for the host to say it has it */
		for (;;) {
			set_pos(b, at);
			send_bin(l, ZEOF, b, use32);
			flush(l);
			int t = get_header(l, &h, WAIT_MS);
			if (t == ZRINIT) return true;
			if (t == ERR_CANCEL) { say(msg, msg_size, "Cancelled"); cancel(l); return false; }
			if (t == ZRPOS) { at = pos_of(&h); break; }
			if (t == ZSKIP) { say(msg, msg_size, "The host skipped it"); return false; }
			if (t == ZACK) continue;
			if (++tries > TRIES) { cancel(l); say(msg, msg_size, "The host stopped answering"); return false; }
		}
	}
}

bool zm_send(const zm_io *io, void *f, const char *name, uint32_t size, uint32_t mtime,
	     char *msg, size_t msg_size)
{
	line l = { .io = io };
	header h;
	char m[96];
	say(msg, msg_size, "The host didn't answer");
	/* ask for its ZRINIT afresh (the start of its first one was taken to spot it) */
	int t = -1;
	for (int tries = 0; tries < TRIES && t != ZRINIT; tries++) {
		send_hex_pos(&l, ZRQINIT, 0);
		t = get_header(&l, &h, tries ? WAIT_MS : 3000);
		if (t == ERR_CANCEL) { cancel(&l); say(msg, msg_size, "Cancelled"); return false; }
	}
	if (t != ZRINIT) { cancel(&l); return false; }
	bool use32 = (h.b[3] & CANFC32) != 0;
	l.escctl = (h.b[3] & ESCCTL) != 0;
	uint32_t rxbuf = (uint32_t)h.b[0] | (uint32_t)h.b[1] << 8;

	/* the file's name and size */
	static uint8_t info[160];
	int n = snprintf((char *)info, sizeof info, "%s", name);
	n++;
	n += snprintf((char *)info + n, sizeof info - (size_t)n, "%lu %lo 100644 0 1 %lu",
		      (unsigned long)size, (unsigned long)mtime, (unsigned long)size);
	n++;
	bool sent = false;
	for (int tries = 0; tries < TRIES && !sent; tries++) {
		uint8_t b[4] = { 0, 0, ZMCLOB, ZCBIN };		/* (ZF1: replace one already there) */
		send_bin(&l, ZFILE, b, use32);
		send_data(&l, info, (size_t)n, ZCRCW, use32);
		for (;;) {
			t = get_header(&l, &h, WAIT_MS);
			if (t == ZRINIT || t == ZACK) continue;		/* (old ones, still arriving) */
			break;
		}
		if (t == ERR_CANCEL) { cancel(&l); say(msg, msg_size, "Cancelled"); return false; }
		if (t == ZSKIP) { say(msg, msg_size, "The host didn't want it (is it there already?)"); break; }
		if (t == ZRPOS) sent = send_file_data(&l, f, name, size, pos_of(&h), use32, rxbuf, msg, msg_size);
		else continue;
		if (!sent) return false;
	}
	/* done: ZFIN both ways, then "OO" */
	for (int tries = 0; tries < 3; tries++) {
		send_hex_pos(&l, ZFIN, 0);
		t = get_header(&l, &h, 3000);
		if (t == ZFIN) {		/* (the CR and LF that end its header: not for the screen) */
			for (int n = 0; n < 3; n++) {
				int c = l.io->get(l.io->ctx, 200);
				if (c != '\r' && c != '\n' && c != 0x8A) break;
			}
			break;
		}
		if (t == ERR_CANCEL) break;
	}
	raw(&l, 'O'); raw(&l, 'O');
	flush(&l);
	if (sent) {
		snprintf(m, sizeof m, "Sent %.40s (%lu bytes)", name, (unsigned long)size);
		say(msg, msg_size, m);
	}
	return sent;
}
