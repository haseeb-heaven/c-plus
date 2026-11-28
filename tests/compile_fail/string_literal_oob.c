/* A constant subscript beyond a string-literal-backed pointer's proven
 * extent (length + NUL) must be a compile-time bounds error. */
#include <stdio.h>

int main(void) {
    const char *str = "Heaven";
    printf("Hello, %c\n", str[0xFFFFF]);
    return 0;
}
