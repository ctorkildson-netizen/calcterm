/* test_vt.c -- the terminal's tests, on Linux: cc -o test_vt test_vt.c ../src/vt.c */
#include <stdio.h>
#include <string.h>
#include "../src/vt.h"

static vt t;
static char said[256];
static size_t nsaid;
static int bells, failures, checks;

static void out(const uint8_t *s, size_t n, void *ctx)
{
	(void)ctx;
	if (nsaid + n < sizeof said) { memcpy(said + nsaid, s, n); nsaid += n; said[nsaid] = 0; }
}
static void bell(void *ctx) { (void)ctx; bells++; }

static void feed(const char *s) { vt_input(&t, (const uint8_t *)s, strlen(s)); }
static void fresh(void) { vt_init(&t, out, bell, NULL); nsaid = 0; said[0] = 0; bells = 0; }

static void row_is(int line, int row, const char *want)
{
	char got[VT_COLS + 1], pad[VT_COLS + 1];
	vt_row_text(&t, row, got);
	snprintf(pad, sizeof pad, "%-*s", VT_COLS, want);
	checks++;
	if (strcmp(got, pad)) {
		failures++;
		printf("line %d: row %d is\n  [%s]\nnot\n  [%s]\n", line, row, got, pad);
	}
}
#define ROW(r, s) row_is(__LINE__, r, s)

static void is(int line, const char *what, long got, long want)
{
	checks++;
	if (got != want) { failures++; printf("line %d: %s is %ld, not %ld\n", line, what, got, want); }
}
#define IS(what, got, want) is(__LINE__, what, got, want)

static void said_is(int line, const char *want)
{
	checks++;
	if (strcmp(said, want)) {
		failures++;
		printf("line %d: said [", line);
		for (size_t i = 0; i < nsaid; i++) printf(said[i] == 27 ? "^[" : "%c", said[i]);
		printf("]\n");
	}
	nsaid = 0; said[0] = 0;
}
#define SAID(s) said_is(__LINE__, s)

int main(void)
{
	/* plain text, CR, LF, wrapping */
	fresh();
	feed("hello\r\nworld");
	ROW(0, "hello"); ROW(1, "world");
	IS("x", t.x, 5); IS("y", t.y, 1);

	fresh();
	for (int i = 0; i < VT_COLS; i++) feed("x");
	IS("x at the margin", t.x, VT_COLS - 1); IS("still row 0", t.y, 0);
	feed("y");
	ROW(1, "y"); IS("wrapped", t.y, 1);

	fresh();
	feed("\x1b[?7l");			/* no autowrap: overwrite the last column */
	for (int i = 0; i < VT_COLS + 5; i++) feed("x");
	feed("Z");
	IS("no wrap", t.y, 0);
	IS("last col", t.cell[0][VT_COLS - 1].ch, 'Z');

	/* scrolling the whole screen, and what's left to draw */
	fresh();
	for (int i = 0; i < VT_ROWS; i++) { char b[24]; snprintf(b, sizeof b, "line %d\r\n", i); feed(b); }
	ROW(0, "line 1"); ROW(VT_ROWS - 2, "line 31"); ROW(VT_ROWS - 1, "");
	IS("rolled once", t.hw_scroll, 1);

	/* cursor movement */
	fresh();
	feed("\x1b[10;20H*");
	IS("CUP row", t.cell[9][19].ch, '*');
	feed("\x1b[H\x1b[5B\x1b[3C#");
	IS("CUD/CUF", t.cell[5][3].ch, '#');
	feed("\x1b[2A\x1b[2D@");
	IS("CUU/CUB", t.cell[3][2].ch, '@');
	feed("\x1b[99;99H");
	IS("clamped x", t.x, VT_COLS - 1); IS("clamped y", t.y, VT_ROWS - 1);

	/* erasing */
	fresh();
	feed("abcdefgh\x1b[1;4H\x1b[K");
	ROW(0, "abc");
	feed("\x1b[1;2H\x1b[1K");
	ROW(0, "  c");
	feed("\r\nzzz\x1b[2J");
	ROW(0, ""); ROW(1, "");

	/* insert and delete characters and lines */
	fresh();
	feed("abcdef\x1b[1;3H\x1b[2P");
	ROW(0, "abef");
	feed("\x1b[1;2H\x1b[3@");
	ROW(0, "a   bef");
	feed("\x1b[4hXY\x1b[4l");
	ROW(0, "aXY   bef");
	fresh();
	feed("1\r\n2\r\n3\r\n4\x1b[2;1H\x1b[L");
	ROW(0, "1"); ROW(1, ""); ROW(2, "2"); ROW(3, "3"); ROW(4, "4");
	feed("\x1b[2M");
	ROW(1, "3"); ROW(2, "4");

	/* the scrolling region */
	fresh();
	feed("top\x1b[32;1Hbottom\x1b[2;4r");
	IS("DECSTBM homes", t.y, 0);
	feed("\x1b[2;1Ha\r\nb\r\nc\r\nd");
	ROW(0, "top"); ROW(1, "b"); ROW(2, "c"); ROW(3, "d"); ROW(4, ""); ROW(31, "bottom");
	IS("region scroll isn't rolled", t.hw_scroll, 0);
	feed("\x1b[2;1H\x1bM");
	ROW(1, ""); ROW(2, "b");
	feed("\x1b[?6h\x1b[1;1HO");
	ROW(1, "O");
	feed("\x1b[?6l\x1b[r");

	/* tabs */
	fresh();
	feed("\tA\tB");
	IS("tab 1", t.cell[0][8].ch, 'A'); IS("tab 2", t.cell[0][16].ch, 'B');
	feed("\x1b[3g\r\x1b[5C\x1bH\r\tC");
	IS("set tab", t.cell[0][5].ch, 'C');
	fresh();
	feed("\x1b[1;20H\x1b[ZA\x1b[2ZB");
	IS("back tab", t.cell[0][16].ch, 'A'); IS("back two tabs", t.cell[0][8].ch, 'B');

	/* attributes and colours */
	fresh();
	feed("\x1b[1;4;31;42mX\x1b[0mY\x1b[94;105mZ\x1b[38;5;196mR\x1b[48;2;0;0;255mB");
	IS("bold", t.cell[0][0].attr, A_BOLD | A_UNDER);
	IS("fg", t.cell[0][0].fg, 1); IS("bg", t.cell[0][0].bg, 2);
	IS("reset", t.cell[0][1].attr, 0); IS("reset fg", t.cell[0][1].fg, C_DEF_FG);
	IS("bright fg", t.cell[0][2].fg, 12); IS("bright bg", t.cell[0][2].bg, 13);
	IS("256 red", t.cell[0][3].fg, 9);
	IS("rgb blue", t.cell[0][4].bg, 12);

	/* the line-drawing set, by SO/SI and by UTF-8 */
	fresh();
	feed("\x1b)0\x0elqqk\x0f.\x1b(0x\x1b(B|");
	ROW(0, "+--+.||");
	fresh();
	feed("\xe2\x94\x8c\xe2\x94\x80\xe2\x94\x90 caf\xc3\xa9 \xe2\x80\x9cq\xe2\x80\x9d");
	ROW(0, "+-+ caf? \"q\"");

	/* sequences it doesn't know leave nothing behind */
	fresh();
	feed("a\x1b]0;a window title\x07" "b\x1b]2;another\x1b\\c\x1b[?2004hd\x1b[>c\x1bPdcs\x1b\\e");
	ROW(0, "abcde");
	SAID("");

	/* answers */
	fresh();
	feed("\x1b[c"); SAID("\x1b[?1;2c");
	feed("\x1b[5n"); SAID("\x1b[0n");
	feed("\x1b[7;9H\x1b[6n"); SAID("\x1b[7;9R");
	feed("\x1b[999;999H\x1b[6n"); SAID("\x1b[32;64R");		/* what `resize` asks */
	feed("\x07"); IS("bell", bells, 1);

	/* save and restore */
	fresh();
	feed("\x1b[5;5H\x1b[1m\x1b" "7\x1b[H\x1b[0m\x1b" "8X");
	IS("restored pos", t.cell[4][4].ch, 'X'); IS("restored attr", t.cell[4][4].attr, A_BOLD);

	/* the alternate screen */
	fresh();
	feed("main\x1b[?1049h");
	ROW(0, "");
	feed("alt\x1b[?1049l");
	ROW(0, "main"); IS("cursor back", t.x, 4);

	/* keys */
	fresh();
	vt_key(&t, VK_UP); SAID("\x1b[A");
	feed("\x1b[?1h"); vt_key(&t, VK_UP); SAID("\x1bOA");
	vt_key(&t, VK_PF1); SAID("\x1bOP");
	vt_key(&t, VK_ENTER); SAID("\r");
	vt_key(&t, VK_BS); SAID("\x7f");
	vt_key(&t, VK_PGDN); SAID("\x1b[6~");
	vt_key(&t, VK_F5); SAID("\x1b[15~");
	vt_key(&t, VK_F10); SAID("\x1b[21~");
	vt_key_char(&t, 'x', true); SAID("\x1bx");
	feed("\x1b[20h"); vt_key(&t, VK_ENTER); SAID("\r\n");

	/* VT52 */
	fresh();
	feed("\x1b[?2l");
	IS("VT52 mode", t.ansi, 0);
	feed("\x1bY%*V\x1b" "A\x1b" "D5");
	IS("ESC Y", t.cell[5][10].ch, 'V');
	IS("up, left", t.cell[4][10].ch, '5');
	vt_key(&t, VK_UP); SAID("\x1b" "A");
	feed("\x1bZ"); SAID("\x1b/Z");
	feed("\x1bH\x1bJ");
	ROW(5, "");
	feed("\x1b<");
	IS("back to ANSI", t.ansi, 1);

	/* DECALN, and RIS */
	fresh();
	feed("\x1b#8");
	ROW(0, "EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE");
	feed("\x1b" "c");
	ROW(0, "");

	/* controls inside a sequence act, and the sequence carries on */
	fresh();
	feed("ab\x1b[\r2Cc");
	ROW(0, "abc");

	printf("%d checks, %d failed\n", checks, failures);
	return failures != 0;
}
