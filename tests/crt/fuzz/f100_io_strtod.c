/* F-057: cplus_strtod / cplus_strtof / cplus_strtold with LCG-generated valid
 * and invalid floating tokens, differentially checked against the host
 * strtod/strtof/strtold (value, end-pointer offset and ERANGE). */
#include "f100_io_common.h"

int main(void) {
    char tok[64];
    char *e1, *e2;
    unsigned i;
    double a, b;
    float af, bf;
    long double al, bl;
    int ea, eb;

    f100_init();
    for (i = 0; i < 1150; ++i) {
        f100_float_token(tok, sizeof tok, f100_below(3) != 0);

        e1 = e2 = NULL;
        errno = 0; a = cplus_strtod(tok, &e1); ea = errno;
        errno = 0; b = strtod(tok, &e2); eb = errno;
        if (a != b || (e1 - tok) != (e2 - tok) || ((ea == ERANGE) != (eb == ERANGE)))
            F100_FAIL("cplus_strtod differs from host");
        F100_OP();

        e1 = e2 = NULL;
        errno = 0; af = cplus_strtof(tok, &e1); ea = errno;
        errno = 0; bf = strtof(tok, &e2); eb = errno;
        if (af != bf || (e1 - tok) != (e2 - tok) || ((ea == ERANGE) != (eb == ERANGE)))
            F100_FAIL("cplus_strtof differs from host");
        F100_OP();

        e1 = e2 = NULL;
        errno = 0; al = cplus_strtold(tok, &e1); ea = errno;
        errno = 0; bl = strtold(tok, &e2); eb = errno;
        if (al != bl || (e1 - tok) != (e2 - tok) || ((ea == ERANGE) != (eb == ERANGE)))
            F100_FAIL("cplus_strtold differs from host");
        F100_OP();
    }
    return f100_report("F-057");
}
