#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compile_fail/classic_bug3_stack_return.c"
/* Bug 3: returning a pointer to a local stack variable. */
#include <stdio.h>
int *get_number(void) {
    int local_var = 100;
    return &local_var;
}
int main(void) {
    int *ptr = get_number();
    printf("%d\n", *ptr);
    return 0;
}
