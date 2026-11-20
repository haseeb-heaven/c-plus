/* Bug 13: modifying a string literal. */
#include <stdio.h>
int main(void) {
    char *str = "hello";
    str[0] = 'H';
    printf("%s\n", str);
    return 0;
}
