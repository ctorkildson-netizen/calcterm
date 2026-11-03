/* main.c -- calcterm: a VT100 terminal for the PicoCalc.
 *
 * For now the line is the serial port: UART0, which the PicoCalc's USB-C
 * socket carries through its CH340, or UART1 on GP4/GP5 for a level shifter
 * to RS-232.  Telnet, SSH and an FTP client are to come.
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

#define DRAW_EVERY_MS 40

static vt term;

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

static void sign_on(bool loaded)
{
	char b[160];
	static const char *const fmt[] = { "N", "O", "E" };
	snprintf(b, sizeof b,
		 "\x1b[1mcalcterm " PICO_PROGRAM_VERSION_STRING "\x1b[0m -- a VT100 terminal (" CALCTERM_BUILD ")\r\n"
		 "%lu %d%s%d on %s%s.  Alt+F5: setup.\r\n\n",
		 (unsigned long)cfg.link.baud, cfg.link.data, fmt[cfg.link.parity], cfg.link.stop,
		 cfg.link.port == PORT_GP45 ? "GP4/GP5" : "USB-C",
		 loaded ? "" : " (the defaults)");
	local(b);
}

static void key(int k)
{
	bool alt = (k & KF_ALT) != 0;
	k &= ~KF_ALT;
	if (k == K_MENU) { menu_run(&term); return; }
	if (k == K_BRK) { link_break(); return; }
	if (k == K_SHOT) { screen_draw(&term, false); shot_take(&term); return; }
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
	vt_init(&term, to_host, bell, NULL);
	apply_settings(&term);
	sign_on(loaded);

	absolute_time_t next_draw = get_absolute_time();
	uint8_t buf[512];
	for (;;) {
		keys_poll();
		int k;
		while ((k = keys_get()) >= 0) key(k);

		/* what's come in, for up to a few milliseconds */
		absolute_time_t until = make_timeout_time_ms(8);
		size_t n;
		bool busy = false;
		while ((n = link_read(buf, sizeof buf)) > 0) {
			vt_input(&term, buf, n);
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
