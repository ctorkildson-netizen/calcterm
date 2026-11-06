/* zmodem.h -- ZMODEM file transfer, both ways, over whatever line the
 * caller provides.
 *
 * The host starts it: `sz file` sends a file to calcterm, `rz` asks for
 * one.  zm_spot() watches what comes in for either start, and the caller
 * then runs zm_receive() or zm_send(), which take over the line until the
 * transfer is done.  Everything the protocol needs from the outside world
 * (the line, the files, the clock) comes through a zm_io, so the same code
 * runs on the PicoCalc and in the tests on Linux.
 */
#ifndef ZMODEM_H
#define ZMODEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ZM_TIMEOUT -1		/* from get(): nothing came in time */
#define ZM_CANCEL -2		/* from get(): the user wants to stop */

typedef struct {
	void *ctx;
	/* the line: the next byte, waiting up to ms milliseconds (0: don't wait) */
	int (*get)(void *ctx, int ms);
	void (*put)(void *ctx, const uint8_t *s, size_t n);
	/* receiving: a file to write (NULL if it can't be), its data, and its end
	   (ok false: the transfer failed, so the file is incomplete) */
	void *(*create)(void *ctx, const char *name, uint32_t size);
	bool (*write)(void *ctx, void *f, const uint8_t *d, size_t n);
	void (*close)(void *ctx, void *f, bool ok);
	/* sending: bytes from the file at an offset; how many, 0 at the end, -1 on error */
	int (*read)(void *ctx, void *f, uint32_t at, uint8_t *d, size_t n);
	/* how it's going, now and then (may be NULL) */
	void (*progress)(void *ctx, const char *name, uint32_t done, uint32_t size);
} zm_io;

/* What comes in, a byte at a time: ZM_SPOT_RECEIVE when the host has
   started sending a file (sz), ZM_SPOT_SEND when it's waiting for one (rz),
   ZM_SPOT_NONE otherwise.  The start's own bytes are kept back from the
   screen: out gets the bytes that are for it (none, while a start might be
   under way, or several, when it turns out not to be one). */
enum { ZM_SPOT_NONE, ZM_SPOT_RECEIVE, ZM_SPOT_SEND };
typedef struct { uint8_t held[8]; int n; } zm_spotter;
int zm_spot(zm_spotter *s, uint8_t c, uint8_t *out, int *n_out);

/* Receive files from the host (it has started with sz).  Returns how many
   came; msg says how it ended. */
int zm_receive(const zm_io *io, char *msg, size_t msg_size);

/* Send one file to the host (which is running rz); f is passed to read().
   Returns true if it got there. */
bool zm_send(const zm_io *io, void *f, const char *name, uint32_t size, uint32_t mtime,
	     char *msg, size_t msg_size);

/* Tell the host to give up (as when the user cancels before anything's sent). */
void zm_cancel(const zm_io *io);

#endif
