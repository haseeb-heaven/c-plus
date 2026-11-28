/* NEGATIVE VLA: sizeof evaluates its operand at run time (side effect). */
#include <stdio.h>
int main(void) { int n = 4; size_t s = sizeof(int[n++]); printf("%zu %d\n", s, n); return 0; }
