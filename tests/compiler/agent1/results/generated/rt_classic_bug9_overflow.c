#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/runtime_fail/classic_bug9_overflow.c"
/* Bug 9: signed integer overflow. */
#include <limits.h>
#include <stdio.h>
int main(void) {
    int max = INT_MAX;
    int overflowed = max + 1;
    printf("%d\n", overflowed);
    return 0;
}
