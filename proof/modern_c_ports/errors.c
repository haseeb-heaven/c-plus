/* Independent modern-C port of the "errors" the inspiration language sample workload.
 * C11, no the inspiration language source copied. See proof/modern_c_ports/README.md. */
#include "common.h"

enum result { OK = 0, ERR_ZERO = 1, ERR_NULL = 2 };
static enum result divide(int a, int b, int *out) {
    if (!out) return ERR_NULL;
    if (b == 0) return ERR_ZERO;
    *out = a / b; return OK;
}
int main(void) {
    int out = -1;
    CHECK("errors_ok", divide(9, 3, &out) == OK && out == 3);
    CHECK("errors_zero", divide(1, 0, &out) == ERR_ZERO);
    CHECK("errors_null", divide(1, 2, 0) == ERR_NULL);
    report("ERRORS"); return fails;
}
