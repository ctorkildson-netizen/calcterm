/* net.h -- Wi-Fi and a TCP connection, for telnet (Pico 2 W).
 *
 * The network's name and password come from /ntp/ntp.cfg, the PicoCalc
 * shell's file for its NTP command (read with the shell's own parser), so
 * Wi-Fi is set up in one place on the card.
 */
#ifndef NET_H
#define NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* called now and then while connecting, with what's happening; false stops it */
typedef bool (*net_progress)(const char *what, void *ctx);

/* join the network if need be, then connect; false, with why, if not */
bool net_connect(const char *host, uint16_t port, net_progress progress, void *ctx,
		 char *why, size_t why_size);
void net_close(void);
bool net_open(void);			/* connected, or closed with data still to read */
bool net_closed_by_host(void);		/* the host hung up (or the line dropped) */
size_t net_read(uint8_t *buf, size_t max);
size_t net_waiting(void);
void net_write(const uint8_t *s, size_t n);	/* waits for room, as a serial line would */
const char *net_address(void);		/* ours, once joined: "192.168.1.23" */

#endif
