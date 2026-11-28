/* F-058: convenience numeric parsers cplus_atoi / cplus_atol / cplus_atoll /
 * cplus_atof over random valid and invalid tokens; each is compared with the
 * corresponding host function.  Tokens are kept within int range for the
 * atoi differential so the comparison stays implementation-defined, not UB. */
#include "f100_io_common.h"

int main(void) {
    char tok[64];
    unsigned i, v;

    f100_init();
    for (i = 0; i < 1300; ++i) {
        v = f100_below(4);
        if (v == 0) {
            /* short decimal token, guarantees an in-range int conversion */
            (void)snprintf(tok, sizeof tok, "%s%u",
                           f100_below(2) ? "-" : "", f100_below(100000u));
        } else {
            f100_int_token(tok, sizeof tok, 10, v != 1);
        }
        if (cplus_atoi(tok) != atoi(tok)) F100_FAIL("cplus_atoi differs from host");
        F100_OP();
        if (cplus_atol(tok) != atol(tok)) F100_FAIL("cplus_atol differs from host");
        F100_OP();
        if (cplus_atoll(tok) != atoll(tok)) F100_FAIL("cplus_atoll differs from host");
        F100_OP();

        f100_float_token(tok, sizeof tok, f100_below(3) != 0);
        if (cplus_atof(tok) != atof(tok)) F100_FAIL("cplus_atof differs from host");
        F100_OP();
    }
    return f100_report("F-058");
}
