#include "../../log/log.h"
#include <ctype.h>
#include <stdio.h> /* EOF */

/* ISO C requires c to be EOF or representable as unsigned char; cast before
 * the table lookup so negative signed char values cannot index out of range. */
int cplus_isblank(int c){ cplus_libc_log("isblank","enter"); if (c == EOF) return 0; return isblank((unsigned char)c) != 0; }
