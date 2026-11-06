/* keys.c -- the PicoCalc's keyboard.
 *
 * As in Diamond: the text starter's driver looks every 100 ms and doesn't
 * repeat, so this reads the keyboard itself, often, and repeats a held key.
 * Ctrl tapped on its own makes the next key a Ctrl key (sticky Ctrl), for
 * the one Ctrl key in its corner while the other hand holds the PicoCalc.
 */
#include <ctype.h>
#include "pico/stdlib.h"
#include "drivers/keyboard.h"
#include "drivers/southbridge.h"
#include "keys.h"

volatile bool user_interrupt;		/* the keyboard driver sets it on Brk */

#define KQ 64
#define REPEAT_DELAY_MS 450
#define REPEAT_EVERY_MS 50

static int kq[KQ];
static unsigned kq_head, kq_tail;
static bool m_ctrl, m_shift, m_alt, ctrl_used, sticky;
static uint8_t held;
static int held_key;
static bool held_shift;
static absolute_time_t next_repeat;

static void kq_put(int k)
{
	unsigned n = (kq_head + 1) % KQ;
	if (n == kq_tail) return;
	kq[kq_head] = k;
	kq_head = n;
}

static int translate(uint8_t c)
{
	switch (c) {
	case KEY_ENTER: case KEY_RETURN: return VK_ENTER;
	case KEY_BACKSPACE: return VK_BS;
	case KEY_TAB: return VK_TAB;
	case KEY_ESC: return VK_ESC;
	case KEY_UP: return VK_UP;
	case KEY_DOWN: return VK_DOWN;
	case KEY_LEFT: return VK_LEFT;
	case KEY_RIGHT: return VK_RIGHT;
	case KEY_HOME: return VK_HOME;
	case KEY_END: return VK_END;
	case KEY_PAGE_UP: return VK_PGUP;
	case KEY_PAGE_DOWN: return VK_PGDN;
	case KEY_DEL: return VK_DEL;
	case KEY_INSERT: return VK_INS;
	case KEY_F1: return m_alt ? K_WIDTH : VK_PF1;	/* Alt+F1: 64 or 80 columns */
	case KEY_F2: return VK_PF2;
	case KEY_F3: return VK_PF3;
	case KEY_F4: return m_alt ? K_SHOT : VK_PF4;	/* Alt+F4: a screenshot */
	case KEY_F5: return m_alt ? K_MENU : VK_F5;	/* Alt+F5: the setup menu */
	case KEY_F6: return VK_F6;
	case KEY_F7: return VK_F7;
	case KEY_F8: return VK_F8;
	case KEY_F9: return VK_F9;
	case KEY_F10: return VK_F10;
	case KEY_BREAK: return K_BRK;
	}
	if (c < 0x20 || c >= 0x7F) return -1;
	int k = c;
	if (m_ctrl || sticky) {
		if (isalpha(c)) k = c & 0x1F;
		else if (c == ' ' || c == '@' || c == '2') k = 0;
		else if (c == '[' || c == '3') k = 0x1B;
		else if (c == '\\' || c == '4') k = 0x1C;
		else if (c == ']' || c == '5') k = 0x1D;
		else if (c == '^' || c == '6') k = 0x1E;
		else if (c == '_' || c == '-' || c == '7') k = 0x1F;
		else if (c == '?' || c == '8') k = 0x7F;
	} else if (m_shift && isalpha(c)) {
		k = toupper(c);
	}
	return k;
}

void keys_init(void)
{
	keyboard_init();			/* (no background polling: it's read here) */
	kq_head = kq_tail = 0;
}

void keys_poll(void)
{
	for (int i = 0; i < 16; i++) {
		uint16_t e = sb_read_keyboard();
		uint8_t state = e >> 8, code = e & 0xFF;
		if (state == 0) break;
		bool down = state == KEY_STATE_PRESSED, up = state == KEY_STATE_RELEASED;
		switch (code) {
		case KEY_MOD_CTRL:
			if (down) { m_ctrl = true; ctrl_used = false; }
			else if (up) {
				m_ctrl = false;
				if (!ctrl_used) sticky = !sticky;	/* a tap: on, or off again */
			}
			continue;
		case KEY_MOD_SHL: case KEY_MOD_SHR: if (down || up) m_shift = down; continue;
		case KEY_MOD_ALT: if (down || up) m_alt = down; continue;
		case KEY_MOD_SYM: continue;
		}
		if (down) {
			if (m_ctrl) ctrl_used = true;
			int k = translate(code);
			sticky = false;			/* used, or cancelled by another key */
			if (k < 0) continue;
			if (m_alt && k < 0x200) k |= KF_ALT;	/* (not on calcterm's own keys) */
			kq_put(k);
			held = code;
			held_key = k;
			held_shift = m_shift;
			next_repeat = make_timeout_time_ms(REPEAT_DELAY_MS);
		} else if (up && held) {
			/* a key let go on the other side of Shift comes up under
			   another name ('q' down, 'Q' up): still the held one */
			if (code == held || tolower(code) == tolower(held) || m_shift != held_shift)
				held = 0;
		}
	}
	if (held && held_key < 0x200 && time_reached(next_repeat)) {	/* (calcterm's own keys don't repeat) */
		kq_put(held_key);
		next_repeat = make_timeout_time_ms(REPEAT_EVERY_MS);
	}
}

int keys_get(void)
{
	if (kq_head == kq_tail) return -1;
	int k = kq[kq_tail];
	kq_tail = (kq_tail + 1) % KQ;
	return k;
}

bool keys_sticky(void)
{
	return sticky;
}
