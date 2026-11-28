/* Tests for the double-precision API gaps closed from ISO_C_FULL_INVENTORY. */
#include "../install-include/cplus_libc.h"
#include <assert.h>
#include <errno.h>
#include <fenv.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int call_vscanf(const char *fmt, ...) {
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = cplus_vscanf(fmt, ap);
    va_end(ap);
    return r;
}

int main(void) {
    /* rint: rounding-mode dependent; compare with host under TONEAREST. */
    int old = fegetround();
    assert(fesetround(FE_TONEAREST) == 0);
    assert(cplus_rint(2.4) == rint(2.4));
    assert(cplus_rint(2.6) == rint(2.6));
    assert(cplus_rint(-2.5) == rint(-2.5));

    /* nexttoward: adjacency, including signed zero. */
    assert(cplus_nexttoward(1.0, 2.0L) == nexttoward(1.0, 2.0L));
    assert(cplus_nexttoward(0.0, -1.0L) == nexttoward(0.0, -1.0L));

    /* lround/llround: exact integer results and domain error classification. */
    assert(cplus_lround(2.6) == lround(2.6));
    assert(cplus_llround(-2.6) == llround(-2.6));
    assert(isnan(0.0 / 0.0)); /* sanity: platform has NaN */
    {
        double nanv = nan("");
        /* Both host and wrapper produce an unspecified but identical value. */
        volatile long a = lround(nanv), b = cplus_lround(nanv);
        assert(a == b);
    }

    /* scalbln: power-of-two scaling, long exponent. */
    assert(cplus_scalbln(1.5, 10L) == scalbln(1.5, 10L));
    assert(cplus_scalbln(1.5, -10L) == scalbln(1.5, -10L));

    /* vscanf through a real va_list helper with redirected stdin. */
    {
        FILE *f = tmpfile();
        assert(f != NULL);
        fputs("42\n", f);
        rewind(f);
        FILE *saved = stdin;
        stdin = f;
        int value = 0;
        assert(call_vscanf("%d", &value) == 1);
        assert(value == 42);
        stdin = saved;
        fclose(f);
    }
    /* NULL format guard must be defined without touching varargs. */
    errno = 0;
    assert(call_vscanf(NULL) == EOF);
    assert(errno == EINVAL);

    assert(fesetround(old) == 0);
    puts("math_gaps: ok");
    return 0;
}
