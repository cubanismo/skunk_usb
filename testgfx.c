
#include "ffsgpu.h"

extern void showgl(int show);
extern volatile unsigned long ticks;

void start(void) {
	showgl(1);

	while (1) {
		clrgamelst();
		drawstring(gamelstbm, (0 << 16) | 0, "Hello you big, beautiful world!");
		drawstring(gamelstbm, (12 << 16) | 0, "12345678901234567890");
		drawstring(gamelstbm, (24 << 16) | 0, "How are you doing today?");
		drawstring(gamelstbm, (36 << 16) | 0, "Two Non-printables <\x19""\x80"">");
		drawstring(gamelstbm, (48 << 16) | 0, "Entirely non-printable string:");
		drawstring(gamelstbm, (60 << 16) | 0, "\x1""\xF0""\x15");
		drawstring(gamelstbm, (72 << 16) | 0, "Empty string:");
		drawstring(gamelstbm, (84 << 16) | 0, "");
		drawstring(gamelstbm, (96 << 16) | 0, "A bunch of symbol characters:");
		drawstring(gamelstbm, (108 << 16) | 0, "~@#$%^&*()_+`=,./;':\"{}[]|\\");
		drawstring(gamelstbm, (120 << 16) | 0, "Goodnight world!");
		drawstring(gamelstbm, (132 << 16) | 0, " *** The End ***");
		invertrect(gamelstbm, (0 << 16) | 0, (48 << 16) | 192);
		invertrect(gamelstbm, (0 << 16) | 0, (48 << 16) | 192);
		invertrect(gamelstbm, (48 << 16) | 0, (48 << 16) | 192);
		invertrect(gamelstbm, (48 << 16) | 0, (48 << 16) | 192);
		invertrect(gamelstbm, (96 << 16) | 0, (48 << 16) | 192);
		invertrect(gamelstbm, (96 << 16) | 0, (48 << 16) | 192);
	}
}
