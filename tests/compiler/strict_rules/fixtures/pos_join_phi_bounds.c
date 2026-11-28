/* POSITIVE join: both arms of the phi are inside the callee's bounds. */
#include <stdio.h>
static void wr(int *p, int c) { if (c) p[2] = 1; else p[0] = 2; }
int main(void) { int a[4] = {0}; wr(a, 1); printf("v=%d\n", a[2]); return 0; }
