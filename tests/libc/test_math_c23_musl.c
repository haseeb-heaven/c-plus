/* Tests for the musl-copied exp10 family. */
#include "../install-include/cplus_libc.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

static int nearly_equal(double a, double b) {
    double d = fabs(a - b);
    return d <= 1e-12 * (1.0 + fabs(b));
}

int main(void) {
    assert(nearly_equal(cplus_exp10(0.0), 1.0));
    assert(nearly_equal(cplus_exp10(1.0), 10.0));
    assert(nearly_equal(cplus_exp10(2.5), pow(10.0, 2.5)));
    assert(nearly_equal(cplus_exp10(-3.0), 0.001));
    assert(isinf(cplus_exp10(400.0)));
    assert(cplus_exp10f(0.0f) == 1.0f);
    assert(fabs(cplus_exp10f(2.0f) - 100.0f) < 1e-4f);
    assert(nearly_equal((double)cplus_exp10l(1.0L), 10.0));
    puts("math_c23_musl: ok");
    return 0;
}
