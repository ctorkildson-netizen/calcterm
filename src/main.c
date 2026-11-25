/* main.c -- calcterm: a VT100 terminal for the PicoCalc.
 *
 * The line is the serial port (UART0, which the PicoCalc's USB-C socket
 * carries through its CH340, or UART1 on GP4/GP5 for a level shifter to
 * RS-232), or telnet over Wi-Fi on a Pico 2 W, picked from a host list
 * with Alt+F2.  SSH and SFTP are to come.  Files go both ways by ZMODEM
 * (xfer.c), started from the host with sz or rz, over either.
 *
 * The loop takes what's arrived and feeds it to the terminal, and draws
 * what's changed: at once when the line goes quiet, and otherwise every
 * 40 ms, so a fast stream isn't held up drawing every line of it.
 */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "drivers/lcd.h"
#include "drivers/southbridge.h"
#include "drivers/filesystem.h"
#include "vt.h"
#include "screen.h"
#include "keys.h"
#include "link.h"
#include "menu.h"
#include "settings.h"
#include "shot.h"
#include "xfer.h"
#include "zmodem.h"
#include "hosts.h"
#include "net.h"
#include "btkbd.h"

#define DRAW_EVERY_MS 40

static vt term;
static zm_spotter spotter;

/* what came in: to the terminal, unless it's the start of a ZMODEM transfer */
static void incoming(const uint8_t *s, size_t n)
{
	shot_record(s, n);			/* (kept for a screenshot's .raw) */
	uint8_t show[sizeof(uint8_t) * 512 + 8], out[8];
	size_t k = 0;
	for (size_t i = 0; i < n; i++) {
		int got, spot = zm_spot(&spotter, s[i], out, &got);
		if (k + (size_t)got > sizeof show) { vt_input(&term, show, k); k = 0; }
		memcpy(show + k, out, (size_t)got);
		k += (size_t)got;
		if (spot == ZM_SPOT_NONE) continue;
		vt_input(&term, show, k);
		screen_draw(&term, false);
		if (spot == ZM_SPOT_RECEIVE) xfer_receive(&term, s + i + 1, n - i - 1);
		else xfer_send(&term, s + i + 1, n - i - 1);
		return;				/* (the rest went to the transfer) */
	}
	vt_input(&term, show, k);
}

static void to_host(const uint8_t *s, size_t n, void *ctx)
{
	(void)ctx;
	link_write(s, n);
}

static void bell(void *ctx)
{
	(void)ctx;
	screen_flash();
}

static void local(const char *s)	/* a message of our own, on the terminal's screen */
{
	vt_input(&term, (const uint8_t *)s, strlen(s));
}

static bool bt;				/* Bluetooth started: a keyboard's paired */

/* a keyboard paired with btpair (picocalc-extras): start Bluetooth, which
   then runs in the background, its keys coming in with the PicoCalc's own */
static void bluetooth(void)
{
	FILINFO fi;
	if (f_stat("/bluetooth/pairing.dat", &fi) != FR_OK) return;
	if (!net_radio()) return;		/* (the radio's shared with telnet's Wi-Fi) */
	bt = btkbd_init();
}

/* the keyboard coming and going, on the bottom line */
static void keyboard_news(void)
{
	static btkbd_state_t was = BTKBD_OFF;
	if (!bt) return;
	btkbd_state_t now = btkbd_state();
	if (now == was) return;
	char b[72];
	if (now == BTKBD_CONNECTED) snprintf(b, sizeof b, " Keyboard: %.40s connected.", btkbd_name());
	else if (was == BTKBD_CONNECTED) snprintf(b, sizeof b, " Keyboard: %.40s gone.", btkbd_name());
	else b[0] = 0;
	if (b[0]) screen_note(b, 3000);
	was = now;
}

static void sign_on(bool loaded)
{
	char b[160];
	static const char *const fmt[] = { "N", "O", "E" };
	snprintf(b, sizeof b,
		 "\x1b[1mcalcterm " PICO_PROGRAM_VERSION_STRING "\x1b[0m -- a VT100 terminal (" CALCTERM_BUILD ")\r\n"
		 "%lu %d%s%d on %s%s.\r\nAlt+F5: setup.  Alt+F1: 64 or 80 columns.  Alt+F2: telnet, SSH.\r\n\n",
		 (unsigned long)cfg.link.baud, cfg.link.data, fmt[cfg.link.parity], cfg.link.stop,
		 cfg.link.port == PORT_GP45 ? "GP4/GP5" : "USB-C",
		 loaded ? "" : " (the defaults)");
	local(b);
	if (bt && btkbd_name()[0]) {
		snprintf(b, sizeof b, "Bluetooth keyboard: %.40s (press a key on it to connect).\r\n\n", btkbd_name());
		local(b);
	}
}

/* while connecting: what's happening on the bottom line; Esc stops it */
static bool progress(const char *what, void *ctx)
{
	(void)ctx;
	char b[72];
	snprintf(b, sizeof b, " %.58s  (Esc: stop)", what);
	screen_note(b, 30000);
	screen_draw(&term, false);
	keys_poll();
	int k;
	while ((k = keys_get()) >= 0)
		if ((k & ~KF_ALT) == VK_ESC) return false;
	return true;
}

static bool ssh_connect(const char *target, const char *at, uint16_t port, char *why, size_t why_size);

static void hosts(void)
{
	char host[HOST_LEN], name[24], b[160];
	uint16_t port;
	int what = hosts_pick(link_is_telnet(), host, &port, name);
	vt_touch_all(&term);			/* the terminal back */
	screen_draw(&term, false);
	if (what == HOSTS_HANG_UP) {
		link_hangup();
		local("\r\n[Hung up. Back on the serial port.]\r\n");
		screen_note(" Hung up.", 2000);
		return;
	}
	if (what != HOSTS_CONNECT) return;
	char why[96];
	bool was = link_is_telnet();
	char *at = strchr(host, '@');
	bool ok = at ? ssh_connect(host, at, port, why, sizeof why)
		     : link_telnet(host, port, progress, NULL, "calcterm", term.cols, VT_ROWS, why, sizeof why);
	if (!ok) {
		snprintf(b, sizeof b, " %.62s", why);
		screen_note(b, 8000);
		if (was) local("\r\n[Hung up. Back on the serial port.]\r\n");
		return;
	}
	hosts_remember(host, port);
	snprintf(b, sizeof b, "\r\n[Connected to %.40s%s. Alt+F2: hang up.]\r\n", name, at ? " by SSH" : "");
	local(b);
	screen_note(" Connected.", 2000);
}

/* a line typed on the bottom line (hidden: dots, for a password); false: Esc */
static bool ask_line(const char *prompt, char *out, size_t size, bool hidden)
{
	size_t n = strlen(out);
	for (;;) {
		char b[72], shown[40];
		size_t k = n < sizeof shown - 1 ? n : sizeof shown - 1;
		for (size_t i = 0; i < k; i++) shown[i] = hidden ? '*' : out[n - k + i];
		shown[k] = 0;
		snprintf(b, sizeof b, " %s%s_", prompt, shown);
		screen_note(b, 600000);
		screen_draw(&term, false);
		keys_poll();
		int c = keys_get();
		if (c < 0) { sleep_ms(10); continue; }
		c &= ~KF_ALT;
		if (c == VK_ESC) { screen_note("", 1); return false; }
		if (c == VK_ENTER) { screen_note("", 1); return true; }
		if (c == VK_BS && n) out[--n] = 0;
		else if (c >= 0x20 && c < 0x7f && n < size - 1) { out[n++] = (char)c; out[n] = 0; }
	}
}

/* SSH to user@host: connect, check the host's key against the one trusted
   before (or ask, the first time), then the password (three tries) */
static bool ssh_connect(const char *target, const char *at, uint16_t port, char *why, size_t why_size)
{
	char user[33], host[HOST_LEN], fp[64], b[160];
	snprintf(user, sizeof user, "%.*s", (int)(at - target), target);
	snprintf(host, sizeof host, "%s", at + 1);
	if (!link_ssh_connect(host, port, progress, NULL, fp, sizeof fp, why, why_size)) return false;
	switch (hosts_key(host, port, fp)) {
	case KEY_KNOWN:
		break;
	case KEY_CHANGED:
		snprintf(b, sizeof b, "\r\n[%s's key has CHANGED since calcterm trusted it:\r\n %s\r\n"
			 "Someone may be pretending to be it. If you know why it changed (the system\r\n"
			 "was reinstalled, say), delete its line in " KNOWN_PATH ".]\r\n", host, fp);
		local(b);
		link_hangup();
		snprintf(why, why_size, "Not connected: the host's key has changed");
		return false;
	default: {
		snprintf(b, sizeof b, "\r\n[First time connecting to %s. Its key:\r\n %s]\r\n", host, fp);
		local(b);
		char yn[4] = "";
		if (!ask_line("Trust this host? (y/n) ", yn, sizeof yn, false) || (yn[0] != 'y' && yn[0] != 'Y')) {
			link_hangup();
			snprintf(why, why_size, "Not connected.");
			return false;
		}
		hosts_key_remember(host, port, fp);
	}
	}
	for (int tries = 0; tries < 3; tries++) {
		static char password[65];
		password[0] = 0;
		snprintf(b, sizeof b, "%s's password: ", user);
		if (!ask_line(b, password, sizeof password, true)) {
			link_hangup();
			snprintf(why, why_size, "Not connected.");
			return false;
		}
		bool wrong;
		bool ok = link_ssh_login(user, password, progress, NULL, "calcterm", term.cols, VT_ROWS,
					 &wrong, why, why_size);
		memset(password, 0, sizeof password);
		if (ok) return true;
		if (!wrong) return false;
		screen_note(" Wrong password.", 2000);
	}
	link_hangup();
	snprintf(why, why_size, "Wrong password three times.");
	return false;
}

static void key(int k)
{
	bool alt = (k & KF_ALT) != 0;
	k &= ~KF_ALT;
	if (k == K_MENU) { menu_run(&term); return; }
	if (k == K_BRK) { link_break(); return; }
	if (k == K_SHOT) { screen_draw(&term, false); shot_take(&term); return; }
	if (k == K_WIDTH) { cfg.cols = cfg.cols == 80 ? 64 : 80; apply_settings(&term); return; }
	if (k == K_HOSTS) { hosts(); return; }
	if (k == K_BTDIAG) {			/* (testing) Bluetooth's state, into the terminal's text */
		char b[200];
		if (!bt) snprintf(b, sizeof b, "\r\n[Bluetooth not started]\r\n");
		else snprintf(b, sizeof b, "\r\n[%s | %s | %s]\r\n[%s]\r\n", btkbd_status(), btkbd_diag(),
			      btkbd_trace_line(0), btkbd_trace_line(1));
		local(b);
		return;
	}
	if (k >= 0x100) vt_key(&term, k);
	else vt_key_char(&term, (uint8_t)k, alt);
}

int main(void)
{
	sb_init();
	lcd_init();
	screen_init();
	fs_init();
	keys_init();

	settings_defaults();
	bool loaded = settings_load();
	link_open(&cfg.link);
	bluetooth();
	vt_init(&term, to_host, bell, NULL);
	apply_settings(&term);
	sign_on(loaded);

	int shown_cols = term.cols;
	absolute_time_t next_draw = get_absolute_time();
	uint8_t buf[512];
	for (;;) {
		keys_poll();
		int k;
		while ((k = keys_get()) >= 0) key(k);
		if (term.cols != shown_cols) {		/* the width changed: tell the host, or say how */
			shown_cols = term.cols;
			char b[80];
			if (link_is_telnet()) {
				link_size(term.cols, VT_ROWS);
				snprintf(b, sizeof b, " %d columns (the host has been told).", term.cols);
			} else
				snprintf(b, sizeof b, " %d columns. On the host: calcsize (or stty cols %d)", term.cols, term.cols);
			screen_note(b, 4000);
		}
		keyboard_news();
		/* telnet: the host echoes, or (if it says it won't) calcterm does */
		term.local_echo = link_is_telnet() ? !link_host_echoes() : cfg.echo;
		if (link_dropped()) {
			link_hangup();
			local("\r\n[The host hung up. Back on the serial port; Alt+F2 to connect again.]\r\n");
			screen_note(" Connection closed.", 4000);
		}

		/* what's come in, for up to a few milliseconds */
		absolute_time_t until = make_timeout_time_ms(8);
		size_t n;
		bool busy = false;
		while ((n = link_read(buf, sizeof buf)) > 0) {
			incoming(buf, n);
			busy = true;
			if (time_reached(until)) break;
		}
		if (!busy || time_reached(next_draw)) {
			screen_draw(&term, keys_sticky());
			next_draw = make_timeout_time_ms(DRAW_EVERY_MS);
		}
		if (!busy) sleep_ms(2);
	}
}
