/*
 * musl-derived stdlib integer functions (copied verbatim from musl libc,
 * renamed to the C+ cplus_ namespace). musl @
 * 7813e9119eb5fe681b6750b8a033a0490bca0941, MIT. See
 * libc/upstream/musl/PROVENANCE.md. Local adaptation: names cplus_*; logging.
 */
#include <stdlib.h>
#include <ctype.h>
#include "../../log/log.h"

/* musl: src/stdlib/div.c */
div_t cplus_div(int num, int den) {
    cplus_libc_log("div", "enter");
    return (div_t){ num/den, num%den };
}
/* musl: src/stdlib/ldiv.c */
ldiv_t cplus_ldiv(long num, long den) {
    cplus_libc_log("ldiv", "enter");
    return (ldiv_t){ num/den, num%den };
}
/* musl: src/stdlib/lldiv.c */
lldiv_t cplus_lldiv(long long num, long long den) {
    cplus_libc_log("lldiv", "enter");
    return (lldiv_t){ num/den, num%den };
}
/* musl: src/stdlib/llabs.c */
long long cplus_llabs(long long a) {
    cplus_libc_log("llabs", "enter");
    return a>0 ? a : -a;
}
/* musl: src/stdlib/atoll.c */
long long cplus_atoll(const char *s) {
    cplus_libc_log("atoll", "enter");
    long long n=0;
    int neg=0;
    while (isspace(*s)) s++;
    switch (*s) {
    case '-': neg=1;
    case '+': s++;
    }
    /* Compute n as a negative number to avoid overflow on LLONG_MIN */
    while (isdigit(*s))
        n = 10*n - (*s++ - '0');
    return neg ? n : -n;
}
