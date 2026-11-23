#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compile_fail/classic_bug18_unsequenced.c"
/* Bug 18: unsequenced modification. */
#include <stdio.h>
int main(void) { int i = 1; int r = i++ + ++i; printf("%d\n", r); return 0; }