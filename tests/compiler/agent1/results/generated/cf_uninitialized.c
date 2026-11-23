#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compile_fail/uninitialized.c"
#include <stdio.h>

int main(void) {
    int count;
    if (count > 0)
        printf("positive\n");
    return 0;
}
