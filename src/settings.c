/* settings.c -- the settings, as lines of name=value in /calcterm.cfg, so the
 * file can be read and changed by hand (unknown names are skipped). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "drivers/filesystem.h"
#include "settings.h"

settings cfg;

void settings_defaults(void)
{
	cfg.link.port = PORT_USB;
	cfg.link.baud = 115200;
	cfg.link.data = 8;
	cfg.link.parity = PAR_NONE;
	cfg.link.stop = 1;
	cfg.link.xonxoff = true;
	cfg.echo = false;
	cfg.newline = false;
	cfg.bs_del = true;
}

static bool yes(const char *v) { return !strcmp(v, "on") || !strcmp(v, "yes") || !strcmp(v, "1"); }

bool settings_load(void)
{
	FIL f;
	if (f_open(&f, CFG_PATH, FA_READ) != FR_OK) return false;
	static char text[1024];
	UINT got = 0;
	f_read(&f, text, sizeof text - 1, &got);
	f_close(&f);
	text[got] = 0;
	for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
		char *eq = strchr(line, '=');
		if (!eq || line[0] == '#') continue;
		*eq = 0;
		char *v = eq + 1;
		v[strcspn(v, "\r\n")] = 0;
		if (!strcmp(line, "port")) cfg.link.port = !strcmp(v, "gp4-gp5") ? PORT_GP45 : PORT_USB;
		else if (!strcmp(line, "speed")) { long b = atol(v); if (b >= 50 && b <= 921600) cfg.link.baud = (uint32_t)b; }
		else if (!strcmp(line, "format") && strlen(v) == 3) {
			if (v[0] == '7' || v[0] == '8') cfg.link.data = v[0] - '0';
			cfg.link.parity = v[1] == 'E' ? PAR_EVEN : v[1] == 'O' ? PAR_ODD : PAR_NONE;
			if (v[2] == '1' || v[2] == '2') cfg.link.stop = v[2] - '0';
		}
		else if (!strcmp(line, "xonxoff")) cfg.link.xonxoff = yes(v);
		else if (!strcmp(line, "echo")) cfg.echo = yes(v);
		else if (!strcmp(line, "newline")) cfg.newline = yes(v);
		else if (!strcmp(line, "backspace")) cfg.bs_del = strcmp(v, "bs") != 0;
	}
	return true;
}

bool settings_save(const char **why)
{
	char text[512];
	int n = snprintf(text, sizeof text,
		"# calcterm settings (the F5 menu writes this file)\n"
		"port=%s\nspeed=%lu\nformat=%d%c%d\nxonxoff=%s\necho=%s\nnewline=%s\nbackspace=%s\n",
		cfg.link.port == PORT_GP45 ? "gp4-gp5" : "usb-c",
		(unsigned long)cfg.link.baud,
		cfg.link.data, cfg.link.parity == PAR_EVEN ? 'E' : cfg.link.parity == PAR_ODD ? 'O' : 'N',
		cfg.link.stop,
		cfg.link.xonxoff ? "on" : "off", cfg.echo ? "on" : "off",
		cfg.newline ? "on" : "off", cfg.bs_del ? "del" : "bs");
	FIL f;
	FRESULT r = f_open(&f, CFG_PATH, FA_WRITE | FA_CREATE_ALWAYS);
	if (r != FR_OK) { *why = fs_error_string(r); return false; }
	UINT put = 0;
	r = f_write(&f, text, (UINT)n, &put);
	FRESULT rc = f_close(&f);
	if (r == FR_OK) r = rc;
	if (r != FR_OK || put != (UINT)n) { *why = r != FR_OK ? fs_error_string(r) : "the card is full"; return false; }
	return true;
}
