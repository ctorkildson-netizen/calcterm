/* link.h -- the line to the other computer: a serial port, or telnet over
 * Wi-Fi (Pico 2 W). The rest of calcterm reads and writes it the same way
 * either way; telnet's commands are dealt with in here. */
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

/* telnet: connect (the serial port stays as it is, unused), and hang up,
   back to the serial port. progress is called while connecting, with what's
   happening; it returns false to stop. */
bool link_telnet(const char *host, uint16_t port, bool (*progress)(const char *, void *), void *ctx,
		 const char *term, int cols, int rows, char *why, size_t why_size);
void link_hangup(void);
bool link_is_telnet(void);
bool link_dropped(void);			/* telnet: the host hung up */
void link_size(int cols, int rows);		/* telnet: the screen's size changed */
bool link_host_echoes(void);			/* telnet: the host echoes what's typed */

#endif
