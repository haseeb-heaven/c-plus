/* Bug 458 family: untrusted format string passed directly to fprintf. */
#include <stdio.h>

int main(void) {
    char format[32] = "value=%n";
    fprintf(stderr, format);
    return 0;
}
