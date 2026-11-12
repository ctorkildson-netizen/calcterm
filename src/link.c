/* link.c -- the line: the serial port, or telnet (net.c, telnet.c).
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

static bool tel;			/* the line is telnet */
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
	if (tel) net_close();
	tel = false;
}

bool link_is_telnet(void) { return tel; }
bool link_dropped(void) { return tel && !net_open(); }
void link_size(int cols, int rows) { if (tel) telnet_size(&tn, cols, rows); }
bool link_host_echoes(void) { return tel && telnet_host_echoes(&tn); }

size_t link_waiting(void)
{
	return tel ? net_waiting() : ser_waiting();
}

size_t link_read(uint8_t *buf, size_t max)
{
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
	if (tel) telnet_out(&tn, s, n);
	else ser_write(s, n);
}

void link_break(void)
{
	if (tel) telnet_break(&tn);
	else ser_break();
}

uint32_t link_lost(void)
{
	return tel ? 0 : ser_lost();
}
