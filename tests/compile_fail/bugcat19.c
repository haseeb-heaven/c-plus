/* Bug 455 family: untrusted format string passed directly to printf. */
#include <stdio.h>

int main(void) {
    char format[32] = "%s %x %n";
    printf(format);
    return 0;
}
