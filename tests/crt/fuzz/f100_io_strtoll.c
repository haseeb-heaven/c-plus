/* F-056: cplus_strtoll / cplus_strtoull with random tokens plus explicit
 * signed/unsigned 64-bit boundary literals, bases 0/8/10/16, differential
 * against the host strtoll/strtoull. */
#include "f100_io_common.h"

static const char *extremes[] = {
    "0", "-1", "9223372036854775807", "-9223372036854775808",
    "18446744073709551615", "0x7fffffffffffffff", "-0x8000000000000000",
    "01777777777777777777777", "zzz", "", "12w"
};
#define NEXTREMES ((unsigned)(sizeof extremes / sizeof extremes[0]))

int main(void) {
    static const int bases[] = { 0, 8, 10, 16 };
    char tok[64];
    char *e1, *e2;
    unsigned i, base;
    long long a, b;
    unsigned long long ua, ub;
    int ea, eb;

    f100_init();
    for (i = 0; i < 1300; ++i) {
        base = (unsigned)bases[f100_below(4)];
        if (i < NEXTREMES) {
            (void)snprintf(tok, sizeof tok, "%s", extremes[i]);
        } else {
            f100_int_token(tok, sizeof tok, base ? (int)base : 10,
                           f100_below(4) != 0);
        }
        e1 = e2 = NULL;
        errno = 0; a = cplus_strtoll(tok, &e1, (int)base); ea = errno;
        errno = 0; b = strtoll(tok, &e2, (int)base); eb = errno;
        if (a != b || (e1 - tok) != (e2 - tok) || ((ea == ERANGE) != (eb == ERANGE)))
            F100_FAIL("cplus_strtoll differs from host");
        F100_OP();

        e1 = e2 = NULL;
        errno = 0; ua = cplus_strtoull(tok, &e1, (int)base); ea = errno;
        errno = 0; ub = strtoull(tok, &e2, (int)base); eb = errno;
        if (ua != ub || (e1 - tok) != (e2 - tok) || ((ea == ERANGE) != (eb == ERANGE)))
            F100_FAIL("cplus_strtoull differs from host");
        F100_OP();
    }
    return f100_report("F-056");
}
