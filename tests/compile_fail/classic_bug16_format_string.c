/* Bug 16: untrusted format string. */
#include <stdio.h>
int main(void) {
    char user_input[] = "%x %x %x %s";
    printf(user_input);
    return 0;
}
