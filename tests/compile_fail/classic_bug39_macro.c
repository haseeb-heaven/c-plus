/* Bug 39: multi-statement macro without do/while wrapper. */
#define SET_TWO(a, b) a = 1; b = 2;
int main(void) { int x = 0, y = 0; if (0) SET_TWO(x, y); return x + y; }
