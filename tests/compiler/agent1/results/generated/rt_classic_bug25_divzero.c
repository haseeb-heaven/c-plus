#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/runtime_fail/classic_bug25_divzero.c"
/* Bug 25: division by zero. */
#include <stdio.h>
int main(void) { int z = 0; int r = 1 / z; printf("%d\n", r); return 0; }