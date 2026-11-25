/* F-055: cplus_strtol / cplus_strtoul over LCG-generated valid and invalid
 * tokens in bases 8/10/16.  The host strtol/strtoul are an independent
 * differential oracle for value, end-pointer offset and ERANGE. */
#include "f100_io_common.h"

int main(void) {
    static const int bases[] = { 8, 10, 16 };
    char tok[64];
    char *e1, *e2;
    unsigned i, base;
    long a, b;
    unsigned long ua, ub;
    int ea, eb;

    f100_init();
    for (i = 0; i < 1300; ++i) {
        base = (unsigned)bases[f100_below(3)];
        f100_int_token(tok, sizeof tok, (int)base, f100_below(4) != 0);

        e1 = e2 = NULL;
        errno = 0; a = cplus_strtol(tok, &e1, (int)base); ea = errno;
        errno = 0; b = strtol(tok, &e2, (int)base); eb = errno;
        if (a != b || (e1 - tok) != (e2 - tok) || ((ea == ERANGE) != (eb == ERANGE)))
            F100_FAIL("cplus_strtol differs from host strtol");
        F100_OP();

        e1 = e2 = NULL;
        errno = 0; ua = cplus_strtoul(tok, &e1, (int)base); ea = errno;
        errno = 0; ub = strtoul(tok, &e2, (int)base); eb = errno;
        if (ua != ub || (e1 - tok) != (e2 - tok) || ((ea == ERANGE) != (eb == ERANGE)))
            F100_FAIL("cplus_strtoul differs from host strtoul");
        F100_OP();
    }
    return f100_report("F-055");
}
