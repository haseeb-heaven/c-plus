/* musl-derived ctype extras (isascii, toascii). musl @ 7813e911, MIT. */
#include "../../log/log.h"
#include <ctype.h>

int cplus_isascii(int c) {
    cplus_libc_log("isascii", "enter");
    return !(c & ~0x7f);
}
int cplus_toascii(int c) {
    cplus_libc_log("toascii", "enter");
    return c & 0x7f;
}
