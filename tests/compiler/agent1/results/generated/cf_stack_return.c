#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compile_fail/stack_return.c"
#include <stdio.h>

int *get_number(void) {
    int local_var = 100;
    return &local_var;
}

int main(void) {
    int *p = get_number();
    printf("%d\n", *p);
    return 0;
}
