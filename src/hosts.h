/* hosts.h -- the telnet host list (Alt+F2), kept in /calcterm/hosts. */
#ifndef HOSTS_H
#define HOSTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HOSTS_PATH "/calcterm/hosts"
#define HOST_LEN 64

enum { HOSTS_CANCEL, HOSTS_CONNECT, HOSTS_HANG_UP };

/* the list, over the terminal: a host chosen (or typed) to connect to, a
   hang-up (offered when connected), or nothing */
int hosts_pick(bool connected, char *host, uint16_t *port, char *name);

/* a typed address that worked: onto the list, if it isn't there */
void hosts_remember(const char *host, uint16_t port);

#endif
