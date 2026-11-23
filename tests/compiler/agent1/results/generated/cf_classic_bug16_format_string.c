#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compile_fail/classic_bug16_format_string.c"
/* Bug 16: untrusted format string. */
#include <stdio.h>
int main(void) {
    char user_input[] = "%x %x %x %s";
    printf(user_input);
    return 0;
}