/* Regression for the OpenQodex fallback review finding: strncat with n==0
 * is a no-op and must succeed even when the destination has no spare byte. */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "cplus_libc.h"

int main(void) {
    char d[4];
    char *r;

    strcpy(d, "abc"); /* exactly fills the 4-byte object incl. NUL */
    errno = 0;
    r = cplus_strncat_bounded(d, "x", 0u, sizeof d);
    if (r != d || errno != 0 || strcmp(d, "abc") != 0) {
        fprintf(stderr, "strncat n=0 no-op broken: r=%p errno=%d d=%s\n",
                (void *)r, errno, d);
        return 1;
    }
    puts("strncat_zero_len_bounded: ok");
    return 0;
}
