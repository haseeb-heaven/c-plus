/* Bug 37: NaN equality comparison. */
#include <math.h>
#include <stdio.h>
int main(void) { double v = NAN; if (v == NAN) printf("x\n"); return 0; }
