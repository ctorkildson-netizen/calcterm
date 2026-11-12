/* hosts.c -- the telnet host list (Alt+F2): see hosts.h.
 *
 * /calcterm/hosts has a line for each host: a name, its address, and the
 * port if it isn't telnet's 23; # starts a comment.
 *
 *     # name     address          port
 *     mini       192.168.1.50
 *     bbs        bbs.example.org  2323
 *
 * The list shows them, then "Type an address..." for one that isn't on it
 * (one that works is added), and "Hang up" when connected.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "drivers/filesystem.h"
#include "keys.h"
#include "screen.h"
#include "settings.h"
#include "hosts.h"

#define MAX_HOSTS 40
#define LIST_TOP 3
#define LIST_ROWS 24
#define FG 15
#define BG 4

static struct { char name[24]; char host[HOST_LEN]; uint16_t port; } list[MAX_HOSTS];
static int n_hosts;

static void load(void)
{
	n_hosts = 0;
	FIL f;
	if (f_open(&f, HOSTS_PATH, FA_READ) != FR_OK) return;
	static char text[4096];
	UINT got = 0;
	f_read(&f, text, sizeof text - 1, &got);
	f_close(&f);
	text[got] = 0;
	for (char *line = strtok(text, "\n"); line && n_hosts < MAX_HOSTS; line = strtok(NULL, "\n")) {
		char *hash = strchr(line, '#');
		if (hash) *hash = 0;
		char *w[3] = { 0 };
		int k = 0;
		for (char *t = strtok_r(line, " \t\r", &hash); t && k < 3; t = strtok_r(NULL, " \t\r", &hash)) w[k++] = t;
		if (!k) continue;
		/* one word: the address is its name too */
		const char *addr = k > 1 ? w[1] : w[0];
		long port = k > 2 ? atol(w[2]) : 23;
		if (port < 1 || port > 65535) continue;
		snprintf(list[n_hosts].name, sizeof list[0].name, "%.23s", w[0]);
		snprintf(list[n_hosts].host, HOST_LEN, "%.63s", addr);
		list[n_hosts].port = (uint16_t)port;
		n_hosts++;
	}
}

static int key_wait(void)
{
	int k;
	do { keys_poll(); k = keys_get(); if (k < 0) sleep_ms(10); } while (k < 0);
	return k & ~KF_ALT;
}

static void draw(int sel, int top, int items, bool connected)
{
	char b[65];
	screen_fill(0, 0, 64, 32, BG);
	screen_text(1, 0, "Connect by telnet", 11, BG);
	screen_text(1, 1, "Wi-Fi from /ntp/ntp.cfg   the list: " HOSTS_PATH, 7, BG);
	if (!n_hosts) screen_text(3, LIST_TOP, "(no hosts listed yet)", 7, BG);
	for (int r = 0; r < LIST_ROWS && top + r < items; r++) {
		int i = top + r;
		if (i < n_hosts) {
			if (list[i].port == 23) snprintf(b, sizeof b, " %-16.16s %-43.43s", list[i].name, list[i].host);
			else snprintf(b, sizeof b, " %-16.16s %-36.36s %6u", list[i].name, list[i].host, list[i].port);
		} else if (i == n_hosts) snprintf(b, sizeof b, " Type an address...");
		else snprintf(b, sizeof b, " Hang up (back to the serial port)");
		snprintf(b + strlen(b), sizeof b - strlen(b), "%*s", 62 - (int)strlen(b), "");
		screen_text(1, LIST_TOP + (i < n_hosts ? r : r + 1), b, i == sel ? 0 : FG, i == sel ? 11 : BG);
	}
	(void)connected;
	screen_text(1, 30, "Enter: connect   or just type an address   Esc: back", 7, BG);
}

/* a line typed on the bottom row; false: Esc */
static bool ask(const char *prompt, char *out, size_t size)
{
	size_t n = strlen(out);
	for (;;) {
		char b[65];
		snprintf(b, sizeof b, "%s%s_", prompt, out);
		snprintf(b + strlen(b), sizeof b - strlen(b), "%*s", 63 - (int)strlen(b), "");
		screen_text(0, 30, b, 11, BG);
		int k = key_wait();
		if (k == VK_ESC) return false;
		if (k == VK_ENTER) return n > 0;
		if (k == VK_BS && n) out[--n] = 0;
		else if (k >= 0x20 && k < 0x7f && n < size - 1) { out[n++] = (char)k; out[n] = 0; }
	}
}

/* "host", "host:port" or "host port" */
static bool parse(char *s, char *host, uint16_t *port)
{
	*port = 23;
	char *sep = strpbrk(s, ": ");
	if (sep) {
		long p = atol(sep + 1);
		if (p < 1 || p > 65535) return false;
		*port = (uint16_t)p;
		*sep = 0;
	}
	snprintf(host, HOST_LEN, "%.63s", s);
	return host[0] != 0;
}

int hosts_pick(bool connected, char *host, uint16_t *port, char *name)
{
	load();
	int items = n_hosts + 1 + (connected ? 1 : 0);
	int sel = 0, top = 0;
	for (;;) {
		if (sel < top) top = sel;
		if (sel >= top + LIST_ROWS) top = sel - LIST_ROWS + 1;
		draw(sel, top, items, connected);
		int k = key_wait();
		if (k == VK_ESC) return HOSTS_CANCEL;
		if (k == VK_UP && sel > 0) sel--;
		if ((k == VK_DOWN || k == VK_TAB) && sel < items - 1) sel++;
		if (k == VK_HOME) sel = 0;
		if (k == VK_END) sel = items - 1;
		/* typing starts an address, wherever the bar is */
		bool typing = k >= 0x20 && k < 0x7f;
		if (typing) sel = n_hosts;
		else if (k != VK_ENTER && k != VK_RIGHT) continue;
		if (sel < n_hosts) {
			snprintf(host, HOST_LEN, "%s", list[sel].host);
			*port = list[sel].port;
			snprintf(name, 24, "%s", list[sel].name);
			return HOSTS_CONNECT;
		}
		if (sel > n_hosts) return HOSTS_HANG_UP;
		static char typed[HOST_LEN + 8];
		typed[0] = typing ? (char)k : 0;
		typed[1] = 0;
		if (!ask("Address (:port if not 23): ", typed, sizeof typed)) continue;
		char copy[sizeof typed];
		strcpy(copy, typed);
		if (!parse(copy, host, port)) continue;
		snprintf(name, 24, "%.23s", host);
		return HOSTS_CONNECT;
	}
}

void hosts_remember(const char *host, uint16_t port)
{
	load();
	for (int i = 0; i < n_hosts; i++)
		if (!strcasecmp(list[i].host, host) && list[i].port == port) return;
	FIL f;
	f_mkdir(CFG_DIR);
	bool fresh = f_stat(HOSTS_PATH, NULL) != FR_OK;
	if (f_open(&f, HOSTS_PATH, FA_WRITE | FA_OPEN_APPEND) != FR_OK) return;
	char b[160];
	int n = 0;
	if (fresh) n = snprintf(b, sizeof b, "# calcterm's telnet hosts: name, address, port (if not 23)\n");
	if (port == 23) n += snprintf(b + n, sizeof b - (size_t)n, "%-16s %s\n", host, host);
	else n += snprintf(b + n, sizeof b - (size_t)n, "%-16s %s %u\n", host, host, port);
	UINT put;
	f_write(&f, b, (UINT)n, &put);
	f_close(&f);
}
