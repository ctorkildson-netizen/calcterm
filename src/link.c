/* link.c -- the line: the serial port, telnet (net/, telnet.c) or SSH
 * (picocalc-extras' ssh/: libssh2 over a net/ connection).
 *
 * What comes in is caught by the UART's interrupt into a 16 KB ring, so
 * nothing is lost while the screen is being drawn; with XON/XOFF on, the
 * host is asked to pause when the ring is three-quarters full, and to go
 * on when it's down to a quarter.  (The text starter's serial driver has a
 * 256-byte buffer and only UART0, so the terminal has its own.)
 */
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/gpio.h"
#include "link.h"
#include "net.h"
#include "telnet.h"
#include "sshnet.h"

static bool tel;			/* the line is telnet (or SSH) */
static bool sec;			/* ...SSH: */
static net_conn *sconn;
static LIBSSH2_SESSION *ss;
static LIBSSH2_CHANNEL *sch;
static bool s_eof;
static telnet tn;

#define RING 16384			/* a power of two */
#define HIGH (RING * 3 / 4)
#define LOW (RING / 4)
#define XON 0x11
#define XOFF 0x13

static uint8_t ring[RING];
static volatile uint32_t head, tail;	/* head: the interrupt's; tail: ours */
static volatile uint32_t lost;
static volatile bool paused;		/* we sent XOFF */
static bool flow;
static uart_inst_t *uart;
static int irq_num = -1;

static void on_rx(void)
{
	while (uart_is_readable(uart)) {
		uint8_t c = (uint8_t)uart_get_hw(uart)->dr;
		uint32_t used = head - tail;
		if (used >= RING) { lost++; continue; }
		ring[head & (RING - 1)] = c;
		head++;
		if (flow && !paused && used + 1 >= HIGH) {
			uart_putc_raw(uart, XOFF);	/* (waits at most a character's time) */
			paused = true;
		}
	}
}

void link_open(const link_config *c)
{
	if (irq_num >= 0) {			/* the old one off first */
		irq_set_enabled(irq_num, false);
		irq_remove_handler(irq_num, on_rx);
		uart_set_irq_enables(uart, false, false);
		uart_deinit(uart);
	}
	unsigned tx, rx;
	if (c->port == PORT_GP45) { uart = uart1; tx = 4; rx = 5; irq_num = UART1_IRQ; }
	else { uart = uart0; tx = 0; rx = 1; irq_num = UART0_IRQ; }
	/* (the other port's pins are left as they were: GP0/GP1 to the CH340) */

	uart_init(uart, c->baud);
	gpio_set_function(tx, GPIO_FUNC_UART);
	gpio_set_function(rx, GPIO_FUNC_UART);
	gpio_pull_up(rx);			/* an idle line if nothing's plugged in */
	uart_set_hw_flow(uart, false, false);
	uart_parity_t p = c->parity == PAR_ODD ? UART_PARITY_ODD
		: c->parity == PAR_EVEN ? UART_PARITY_EVEN : UART_PARITY_NONE;
	uart_set_format(uart, c->data, c->stop, p);
	uart_set_fifo_enabled(uart, true);

	head = tail = 0;
	paused = false;
	flow = c->xonxoff;
	irq_set_exclusive_handler(irq_num, on_rx);
	irq_set_enabled(irq_num, true);
	uart_set_irq_enables(uart, true, false);
	if (flow) uart_putc_raw(uart, XON);	/* in case it was left paused */
}

static size_t ser_waiting(void)
{
	return head - tail;
}

static size_t ser_read(uint8_t *buf, size_t max)
{
	size_t n = 0;
	uint32_t h = head;
	while (n < max && tail != h) {
		buf[n++] = ring[tail & (RING - 1)];
		tail++;
	}
	if (paused && head - tail <= LOW) {
		uart_putc_raw(uart, XON);
		paused = false;
	}
	return n;
}

static void ser_write(const uint8_t *s, size_t n)
{
	while (n--) uart_putc_raw(uart, *s++);
}

static void ser_break(void)
{
	uart_tx_wait_blocking(uart);
	uart_set_break(uart, true);
	sleep_ms(250);
	uart_set_break(uart, false);
}

static uint32_t ser_lost(void)
{
	return lost;
}

/* ------------------------------------------------------------ either */

static void tn_send(void *ctx, const uint8_t *s, size_t n)
{
	(void)ctx;
	net_write(s, n);
}

bool link_telnet(const char *host, uint16_t port, bool (*progress)(const char *, void *), void *ctx,
		 const char *term, int cols, int rows, char *why, size_t why_size)
{
	link_hangup();
	if (!net_connect(host, port, progress, ctx, why, why_size)) return false;
	tel = true;
	telnet_start(&tn, tn_send, NULL, term, cols, rows);
	return true;
}

void link_hangup(void)
{
	if (sec) {
		/* a polite goodbye if it goes quickly (each step may say "again"
		   for a while, non-blocking); gone either way */
		absolute_time_t until = make_timeout_time_ms(1000);
#define QUICKLY(expr) while ((expr) == LIBSSH2_ERROR_EAGAIN && !time_reached(until)) sleep_ms(1)
		if (sch) { QUICKLY(libssh2_channel_close(sch)); QUICKLY(libssh2_channel_free(sch)); }
		if (ss) {
			QUICKLY(libssh2_session_disconnect(ss, "bye"));
			libssh2_session_set_blocking(ss, 1);	/* so free doesn't stop half-way */
			libssh2_session_set_timeout(ss, 500);
			libssh2_session_free(ss);
		}
#undef QUICKLY
		if (sconn) net_conn_close(sconn);
		sch = NULL; ss = NULL; sconn = NULL;
		sec = false;
	} else if (tel) net_close();
	tel = false;
}

/* ------------------------------------------------------------ SSH */

/* a libssh2 call, again until it's done, with Esc (progress) to stop it */
#define SSH_WAIT(expr, what) ({ int _r; absolute_time_t _t = make_timeout_time_ms(150); \
	while ((_r = (expr)) == LIBSSH2_ERROR_EAGAIN) { \
		if (time_reached(_t)) { _t = make_timeout_time_ms(150); if (progress && !progress(what, ctx)) { _r = -999; break; } } \
		sleep_ms(1); } _r; })

static void ssh_why(int r, char *why, size_t why_size)
{
	if (r == -999) snprintf(why, why_size, "Stopped.");
	else snprintf(why, why_size, "SSH: %s", ssh_error(ss));
}

bool link_ssh_connect(const char *host, uint16_t port, bool (*progress)(const char *, void *), void *ctx,
		      char *fp, size_t fp_size, char *why, size_t why_size)
{
	link_hangup();
	sconn = net_dial(host, port, progress, ctx, why, why_size);
	if (!sconn) return false;
	ss = ssh_session(sconn);
	if (!ss) { snprintf(why, why_size, "Not enough memory for SSH"); link_hangup(); return false; }
	sec = tel = true;
	s_eof = false;
	int r = SSH_WAIT(libssh2_session_handshake(ss, 0), "Agreeing on encryption...");
	if (r) { ssh_why(r, why, why_size); link_hangup(); return false; }
	/* the fingerprint as OpenSSH writes it: base64 without the padding */
	const unsigned char *h = (const unsigned char *)libssh2_hostkey_hash(ss, LIBSSH2_HOSTKEY_HASH_SHA256);
	static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	size_t n = (size_t)snprintf(fp, fp_size, "SHA256:");
	for (int i = 0; h && i < 32 && n + 4 < fp_size; i += 3) {
		uint32_t v = (uint32_t)h[i] << 16 | (i + 1 < 32 ? (uint32_t)h[i + 1] << 8 : 0) | (i + 2 < 32 ? h[i + 2] : 0);
		int chars = i + 2 < 32 ? 4 : i + 1 < 32 ? 3 : 2;
		for (int k = 0; k < chars; k++) fp[n++] = b64[(v >> (18 - 6 * k)) & 63];
	}
	fp[n] = 0;
	return true;
}

bool link_ssh_login(const char *user, const char *password, bool (*progress)(const char *, void *), void *ctx,
		    const char *term, int cols, int rows, bool *wrong, char *why, size_t why_size)
{
	*wrong = false;
	if (!sec || !ss) { snprintf(why, why_size, "Not connected"); return false; }
	int r = SSH_WAIT(libssh2_userauth_password(ss, user, password), "Logging in...");
	if (r == LIBSSH2_ERROR_AUTHENTICATION_FAILED || r == LIBSSH2_ERROR_PUBLICKEY_UNVERIFIED) {
		*wrong = true;
		snprintf(why, why_size, "Wrong user name or password");
		return false;			/* (still connected: try again) */
	}
	if (r) { ssh_why(r, why, why_size); link_hangup(); return false; }
	absolute_time_t t = make_timeout_time_ms(10000);
	while (!(sch = libssh2_channel_open_session(ss)) && libssh2_session_last_errno(ss) == LIBSSH2_ERROR_EAGAIN
	       && !time_reached(t))
		sleep_ms(1);
	if (!sch) { snprintf(why, why_size, "SSH: no session (%s)", ssh_error(ss)); link_hangup(); return false; }
	r = SSH_WAIT(libssh2_channel_request_pty_ex(sch, term, (unsigned)strlen(term), NULL, 0, cols, rows, 0, 0),
		     "Asking for a terminal...");
	if (!r) r = SSH_WAIT(libssh2_channel_shell(sch), "Starting the shell...");
	if (r) { ssh_why(r, why, why_size); link_hangup(); return false; }
	return true;
}

bool link_is_ssh(void) { return sec; }

/* what's typed, to the host: libssh2 may take part of it now, the rest a
   moment later */
static void ssh_write(const uint8_t *s, size_t n)
{
	absolute_time_t until = make_timeout_time_ms(5000);
	while (n && sch && !time_reached(until)) {
		ssize_t w = libssh2_channel_write(sch, (const char *)s, n);
		if (w == LIBSSH2_ERROR_EAGAIN) { sleep_ms(1); continue; }
		if (w < 0) { s_eof = true; return; }
		s += w;
		n -= (size_t)w;
	}
}

static size_t ssh_read(uint8_t *buf, size_t max)
{
	if (!sch) return 0;
	ssize_t r = libssh2_channel_read(sch, (char *)buf, max);
	if (r > 0) return (size_t)r;
	if ((r < 0 && r != LIBSSH2_ERROR_EAGAIN) || libssh2_channel_eof(sch)) s_eof = true;
	return 0;
}

/* ------------------------------------------------------------ either */

bool link_is_telnet(void) { return tel; }
bool link_dropped(void) { return sec ? s_eof || !net_conn_open(sconn) : tel && !net_open(); }
void link_size(int cols, int rows)
{
	if (sec) { if (sch) libssh2_channel_request_pty_size(sch, cols, rows); }
	else if (tel) telnet_size(&tn, cols, rows);
}
bool link_host_echoes(void) { return sec || (tel && telnet_host_echoes(&tn)); }

size_t link_waiting(void)
{
	if (sec) return sconn ? net_conn_waiting(sconn) : 0;
	return tel ? net_waiting() : ser_waiting();
}

size_t link_read(uint8_t *buf, size_t max)
{
	if (sec) return ssh_read(buf, max);
	if (!tel) return ser_read(buf, max);
	/* telnet's commands come out, so what's left may be nothing: read on
	   until there's data or nothing more */
	uint8_t raw[512];
	for (;;) {
		size_t n = net_read(raw, max < sizeof raw ? max : sizeof raw);
		if (!n) return 0;
		size_t k = telnet_in(&tn, raw, n, buf);
		if (k) return k;
	}
}

void link_write(const uint8_t *s, size_t n)
{
	if (sec) ssh_write(s, n);
	else if (tel) telnet_out(&tn, s, n);
	else ser_write(s, n);
}

void link_break(void)
{
	if (sec) { if (sch) libssh2_channel_process_startup(sch, "break", 5, NULL, 0); }	/* (best effort) */
	else if (tel) telnet_break(&tn);
	else ser_break();
}

uint32_t link_lost(void)
{
	return tel ? 0 : ser_lost();
}
