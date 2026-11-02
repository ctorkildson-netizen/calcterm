/* link.h -- the line to the other computer: for now a serial port. */
#ifndef LINK_H
#define LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { PORT_USB, PORT_GP45, PORT_COUNT };	/* UART0 (the USB-C socket's CH340), UART1 on GP4/GP5 */
enum { PAR_NONE, PAR_ODD, PAR_EVEN };

typedef struct {
	int port;
	uint32_t baud;
	int data, parity, stop;
	bool xonxoff;		/* ask the host to pause when we fall behind */
} link_config;

void link_open(const link_config *c);
size_t link_read(uint8_t *buf, size_t max);	/* what's come in; doesn't wait */
size_t link_waiting(void);			/* how much has come in */
void link_write(const uint8_t *s, size_t n);
void link_break(void);				/* a quarter-second break */
uint32_t link_lost(void);			/* bytes dropped for want of room */

#endif
