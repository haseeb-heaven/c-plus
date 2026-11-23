#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/fread_checked.c"
/* Using the fread return value in an initializer is a checked read. */
#include <stdio.h>
int main(void) {
    char buf[8];
    size_t n = fread(buf, 1, sizeof buf, stdin);
    printf("%zu\n", n);
    return 0;
}