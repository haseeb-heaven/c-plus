#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compile_fail/classic_bug7_uninit.c"
/* Bug 7: uninitialized variable read. */
#include <stdio.h>
int main(void) {
    int count;
    if (count > 0)
        printf("positive\n");
    return 0;
}
