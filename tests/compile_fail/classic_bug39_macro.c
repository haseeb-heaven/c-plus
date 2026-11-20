/* Bug 39: multi-statement macro without do/while wrapper. */
#include <stdio.h>
#define LOG_AND_RESET(x) printf("Val: %d\n", x); x = 0;
int main(void) { int c = 5; if (0) LOG_AND_RESET(c); printf("%d\n", c); return 0; }
