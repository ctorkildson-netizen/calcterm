/* xfer.h -- files to and from the host by ZMODEM, on the PicoCalc's card. */
#ifndef XFER_H
#define XFER_H

#include <stddef.h>
#include <stdint.h>
#include "vt.h"

#define XFER_DIR "/calcterm/files"	/* where files from the host go */

/* the host has started sending (sz); rest: what came in after its start */
void xfer_receive(vt *t, const uint8_t *rest, size_t n);
/* the host is waiting for a file (rz): choose one and send it */
void xfer_send(vt *t, const uint8_t *rest, size_t n);

#endif
