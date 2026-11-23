#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/classic_bug17_safe_function.c"
/* Bug 17: safe alternative to a double-evaluating macro. */
#include <stdio.h>
static int square(int x) { return x * x; }
int main(void) {
    int i = 3;
    int result = square(i++);
    printf("result=%d i=%d\n", result, i);
    return 0;
}