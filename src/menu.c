/* menu.c -- the setup menu (Alt+F5), drawn over the terminal.
 *
 * Up and Down choose, Left and Right (or Space) change a setting, Enter
 * does an action, Esc or F5 closes it.  A change takes effect at once; Save
 * puts the settings on the card for next time.  What arrives while it's
 * open goes on into the terminal, to be seen when it closes.
 */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "picocalc_shell.h"
#include "drivers/filesystem.h"
#include "keys.h"
#include "link.h"
#include "screen.h"
#include "settings.h"
#include "menu.h"
#include "shot.h"

#define BOX_W 46
#define BOX_X ((VT_COLS - BOX_W) / 2)
#define BOX_Y 6
#define FG 15
#define BG 4
#define HI_FG 0
#define HI_BG 11

static const uint32_t speeds[] = { 300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400 };
#define NSPEEDS (sizeof speeds / sizeof speeds[0])
static const struct { int data, parity, stop; const char *name; } formats[] = {
	{ 8, PAR_NONE, 1, "8N1" }, { 7, PAR_EVEN, 1, "7E1" }, { 7, PAR_ODD, 1, "7O1" },
	{ 8, PAR_EVEN, 1, "8E1" }, { 8, PAR_ODD, 1, "8O1" }, { 8, PAR_NONE, 2, "8N2" },
	{ 7, PAR_NONE, 2, "7N2" },
};
#define NFORMATS (sizeof formats / sizeof formats[0])

enum { I_PORT, I_SPEED, I_FORMAT, I_FLOW, I_ECHO, I_ENTER, I_BS,
       I_SHOT, I_BREAK, I_RESET, I_SAVE, I_LEAVE, I_COUNT };

static const char *const names[I_COUNT] = {
	"Port", "Speed", "Format", "Flow control", "Local echo", "Enter sends", "Backspace sends",
	"Take a screenshot (also Alt+F4)", "Send a break", "Reset the terminal", "Save these settings", "Back to the shell",
};

static char note[BOX_W];

static int format_index(void)
{
	for (unsigned i = 0; i < NFORMATS; i++)
		if (formats[i].data == cfg.link.data && formats[i].parity == cfg.link.parity
		    && formats[i].stop == cfg.link.stop) return (int)i;
	return 0;
}

static const char *value(int i, char *b, size_t n)
{
	switch (i) {
	case I_PORT: return cfg.link.port == PORT_GP45 ? "GP4/GP5 (UART1)" : "USB-C (UART0)";
	case I_SPEED: snprintf(b, n, "%lu", (unsigned long)cfg.link.baud); return b;
	case I_FORMAT: return formats[format_index()].name;
	case I_FLOW: return cfg.link.xonxoff ? "XON/XOFF" : "none";
	case I_ECHO: return cfg.echo ? "on" : "off";
	case I_ENTER: return cfg.newline ? "CR LF" : "CR";
	case I_BS: return cfg.bs_del ? "DEL (^?)" : "BS (^H)";
	}
	return "";
}

void apply_settings(vt *t)
{
	t->local_echo = cfg.echo;
	t->newline = cfg.newline;
	t->bs_sends_del = cfg.bs_del;
}

static void change(vt *t, int i, int dir)
{
	switch (i) {
	case I_PORT: cfg.link.port = (cfg.link.port + PORT_COUNT + dir) % PORT_COUNT; break;
	case I_SPEED: {
		int s = 0;
		for (unsigned k = 0; k < NSPEEDS; k++) if (speeds[k] == cfg.link.baud) s = (int)k;
		s = (s + (int)NSPEEDS + dir) % (int)NSPEEDS;
		cfg.link.baud = speeds[s];
		break;
	}
	case I_FORMAT: {
		int f = (format_index() + (int)NFORMATS + dir) % (int)NFORMATS;
		cfg.link.data = formats[f].data;
		cfg.link.parity = formats[f].parity;
		cfg.link.stop = formats[f].stop;
		break;
	}
	case I_FLOW: cfg.link.xonxoff = !cfg.link.xonxoff; break;
	case I_ECHO: cfg.echo = !cfg.echo; break;
	case I_ENTER: cfg.newline = !cfg.newline; break;
	case I_BS: cfg.bs_del = !cfg.bs_del; break;
	default: return;
	}
	if (i <= I_FLOW) link_open(&cfg.link);
	apply_settings(t);
	note[0] = 0;
}

static void draw(int sel)
{
	char line[BOX_W + 1], b[16];
	screen_fill(BOX_X, BOX_Y, BOX_W, I_COUNT + 7, BG);
	screen_text(BOX_X + 2, BOX_Y + 1, "calcterm " PICO_PROGRAM_VERSION_STRING " -- setup  (" CALCTERM_BUILD ")", 11, BG);
	int row = BOX_Y + 3;
	for (int i = 0; i < I_COUNT; i++) {
		if (i == I_SHOT) row++;
		if (i < I_SHOT) snprintf(line, sizeof line, " %-16s %-25s", names[i], value(i, b, sizeof b));
		else snprintf(line, sizeof line, " %-42s", names[i]);
		bool on = i == sel;
		screen_text(BOX_X + 1, row++, line, on ? HI_FG : FG, on ? HI_BG : BG);
	}
	row++;
	if (note[0]) snprintf(line, sizeof line, " %-43s", note);
	else snprintf(line, sizeof line, " %-43s", sel < I_SHOT ? "Left/Right: change   Esc: close" : "Enter: do it   Esc: close");
	screen_text(BOX_X + 1, row, line, note[0] ? 11 : 7, BG);
}

static void keep_reading(vt *t)
{
	uint8_t buf[256];
	size_t n;
	while ((n = link_read(buf, sizeof buf)) > 0) vt_input(t, buf, n);
}

void menu_run(vt *t)
{
	int sel = 0;
	bool shoot = false;
	note[0] = 0;
	draw(sel);
	for (;;) {
		keys_poll();
		keep_reading(t);
		int k = keys_get();
		if (k < 0) { sleep_ms(5); continue; }
		k &= ~KF_ALT;
		if (k == VK_ESC || k == K_MENU || k == VK_F5) break;
		if (k == VK_UP) sel = (sel + I_COUNT - 1) % I_COUNT;
		else if (k == VK_DOWN || k == VK_TAB) sel = (sel + 1) % I_COUNT;
		else if (k == VK_LEFT) change(t, sel, -1);
		else if (k == VK_RIGHT || k == ' ') change(t, sel, +1);
		else if (k == VK_ENTER) {
			if (sel < I_SHOT) { change(t, sel, +1); }
			else if (sel == I_SHOT) { shoot = true; break; }
			else if (sel == I_BREAK) { link_break(); snprintf(note, sizeof note, "Break sent."); }
			else if (sel == I_RESET) { vt_reset(t); apply_settings(t); snprintf(note, sizeof note, "The terminal's reset."); }
			else if (sel == I_SAVE) {
				const char *why;
				if (settings_save(&why)) snprintf(note, sizeof note, "Saved in %s.", CFG_PATH);
				else snprintf(note, sizeof note, "Not saved: %.30s", why);
			} else if (sel == I_LEAVE) {
				if (!picocalc_shell_exit())
					snprintf(note, sizeof note, "No shell to go back to (no loader or card).");
			}
		}
		draw(sel);
	}
	vt_touch_all(t);
	if (shoot) {			/* of the terminal, without the menu */
		screen_draw(t, false);
		shot_take(t);
	}
}
