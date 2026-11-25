/* Standalone differential tests for the platform-backed cplus_* long double
 * ISO C17 <math.h> wrappers in libc/src/math/math_ldouble.c.
 *
 * Every result is compared against the identically named host function using
 * an LDBL_EPSILON-scaled tolerance. Only defined inputs are used. */
#include <assert.h>
#include <math.h>
#include <float.h>
#include <errno.h>
#include <string.h>

/* Trigonometric */
long double cplus_sinl(long double);
long double cplus_cosl(long double);
long double cplus_tanl(long double);
long double cplus_asinl(long double);
long double cplus_acosl(long double);
long double cplus_atanl(long double);
long double cplus_atan2l(long double, long double);
/* Hyperbolic */
long double cplus_sinhl(long double);
long double cplus_coshl(long double);
long double cplus_tanhl(long double);
long double cplus_asinhl(long double);
long double cplus_acoshl(long double);
long double cplus_atanhl(long double);
/* Exponential / logarithmic */
long double cplus_expl(long double);
long double cplus_exp2l(long double);
long double cplus_expm1l(long double);
long double cplus_logl(long double);
long double cplus_log2l(long double);
long double cplus_log10l(long double);
long double cplus_log1pl(long double);
/* Power */
long double cplus_powl(long double, long double);
long double cplus_sqrtl(long double);
long double cplus_cbrtl(long double);
long double cplus_hypotl(long double, long double);
/* Rounding */
long double cplus_floorl(long double);
long double cplus_ceill(long double);
long double cplus_truncl(long double);
long double cplus_roundl(long double);
long double cplus_nearbyintl(long double);
long double cplus_rintl(long double);
long cplus_lrintl(long double);
long long cplus_llrintl(long double);
long cplus_lroundl(long double);
long long cplus_llroundl(long double);
/* Error / gamma */
long double cplus_erfl(long double);
long double cplus_erfcl(long double);
long double cplus_tgammal(long double);
long double cplus_lgammal(long double);
/* Manipulation */
long double cplus_fabsl(long double);
long double cplus_fmodl(long double, long double);
long double cplus_remainderl(long double, long double);
long double cplus_remquol(long double, long double, int *);
long double cplus_fdiml(long double, long double);
long double cplus_fminl(long double, long double);
long double cplus_fmaxl(long double, long double);
long double cplus_fmal(long double, long double, long double);
long double cplus_copysignl(long double, long double);
long double cplus_ldexpl(long double, int);
long double cplus_frexpl(long double, int *);
long double cplus_modfl(long double, long double *);
long double cplus_scalbnl(long double, int);
long double cplus_scalblnl(long double, long);
int cplus_ilogbl(long double);
long double cplus_logbl(long double);
long double cplus_nextafterl(long double, long double);
long double cplus_nexttowardl(long double, long double);
long double cplus_nanl(const char *);

static void eq(long double a, long double b){
    long double ab = b < 0 ? -b : b;
    long double tol = 1024.0L * LDBL_EPSILON * (1.0L + ab);
    long double d = a - b;
    if(d < 0) d = -d;
    assert(d <= tol);
}

static void same_class(long double a, long double b){
    assert(isnan(a) == isnan(b));
    assert(isinf(a) == isinf(b));
}

#define DIFF1(f, x)           do { eq(cplus_##f(x),    f(x));    } while(0)
#define DIFF2(f, x, y)        do { eq(cplus_##f(x, y), f(x, y)); } while(0)
#define DIFF3(f, x, y, z)     do { eq(cplus_##f(x, y, z), f(x, y, z)); } while(0)

int main(void){
    /* Normal inputs: every wrapper must match its host backend. */
    DIFF1(sinl, 0.3L);  DIFF1(cosl, 0.3L);  DIFF1(tanl, 0.3L);
    DIFF1(asinl, 0.3L); DIFF1(acosl, 0.3L); DIFF1(atanl, 0.3L);
    DIFF2(atan2l, 0.3L, 1.2L);

    DIFF1(sinhl, 0.3L); DIFF1(coshl, 0.3L); DIFF1(tanhl, 0.3L);
    DIFF1(asinhl, 0.3L); DIFF1(acoshl, 1.5L); DIFF1(atanhl, 0.3L);

    DIFF1(expl, 0.3L);  DIFF1(exp2l, 0.3L);  DIFF1(expm1l, 0.3L);
    DIFF1(logl, 1.3L);  DIFF1(log2l, 1.3L);  DIFF1(log10l, 1.3L);
    DIFF1(log1pl, 0.3L);

    DIFF2(powl, 1.3L, 2.5L);
    DIFF1(sqrtl, 1.3L); DIFF1(cbrtl, 2.0L); DIFF2(hypotl, 3.0L, 4.0L);

    DIFF1(floorl, 2.7L); DIFF1(ceill, 2.3L); DIFF1(truncl, 2.7L);
    DIFF1(roundl, 2.5L); DIFF1(nearbyintl, 2.4L); DIFF1(rintl, 2.4L);
    assert(cplus_lrintl(2.4L) == lrintl(2.4L));
    assert(cplus_llrintl(2.4L) == llrintl(2.4L));
    assert(cplus_lroundl(2.5L) == lroundl(2.5L));
    assert(cplus_llroundl(2.5L) == llroundl(2.5L));

    DIFF1(erfl, 0.3L); DIFF1(erfcl, 0.3L);
    DIFF1(tgammal, 1.5L); DIFF1(lgammal, 1.5L);

    DIFF1(fabsl, -2.7L);
    DIFF2(fmodl, 5.3L, 2.0L);
    DIFF2(remainderl, 5.3L, 2.0L);
    DIFF3(fmal, 1.1L, 2.2L, 3.3L);
    DIFF2(fdiml, 5.0L, 2.0L);
    DIFF2(fminl, 1.5L, -2.0L); DIFF2(fmaxl, 1.5L, -2.0L);
    DIFF2(fminl, NAN, 2.0L); DIFF2(fmaxl, NAN, 2.0L);
    DIFF2(copysignl, 1.0L, -2.0L);
    DIFF2(ldexpl, 0.75L, 4);
    DIFF2(scalbnl, 1.3L, 4);
    DIFF2(scalblnl, 1.3L, 4L);
    DIFF1(logbl, 8.0L);
    assert(cplus_ilogbl(8.0L) == ilogbl(8.0L));

    /* NaN / Inf classification matches the host backend. */
    same_class(cplus_sinl(INFINITY),  sinl(INFINITY));
    same_class(cplus_coshl(INFINITY), coshl(INFINITY));
    same_class(cplus_expl(INFINITY),  expl(INFINITY));
    same_class(cplus_logl(0.0L),      logl(0.0L));
    same_class(cplus_logl(-1.0L),     logl(-1.0L));
    same_class(cplus_sqrtl(-1.0L),    sqrtl(-1.0L));

    /* Signed zero is preserved for direction-sensitive operations. */
    assert(signbit(cplus_copysignl(1.0L, -0.0L)) ==
           signbit(copysignl(1.0L, -0.0L)));
    assert(signbit(cplus_copysignl(1.0L, -0.0L)) != 0);
    assert(signbit(cplus_fabsl(-0.0L)) == 0);
    assert(signbit(cplus_copysignl(1.0L, -2.0L)) ==
           signbit(copysignl(1.0L, -2.0L)));

    /* Out-parameter paths. */
    {
        int e1 = 0, e2 = 0;
        long double m1 = cplus_frexpl(3.75L, &e1);
        long double m2 = frexpl(3.75L, &e2);
        eq(m1, m2); assert(e1 == e2);
        assert(m1 >= 0.5L && m1 < 1.0L);

        long double i1 = 0.0L, i2 = 0.0L;
        long double f1 = cplus_modfl(2.75L, &i1);
        long double f2 = modfl(2.75L, &i2);
        eq(f1, f2); eq(i1, i2);
        assert(i1 == 2.0L && f1 == 0.75L);

        int q1 = 0, q2 = 0;
        long double r1 = cplus_remquol(5.3L, 2.0L, &q1);
        long double r2 = remquol(5.3L, 2.0L, &q2);
        eq(r1, r2); assert(q1 == q2);
    }

    /* NULL out-parameters: defined hardening -> EINVAL and 0.0L. */
    errno = 0;
    assert(cplus_frexpl(1.0L, NULL) == 0.0L && errno == EINVAL);
    errno = 0;
    assert(cplus_modfl(1.0L, NULL) == 0.0L && errno == EINVAL);
    errno = 0;
    assert(cplus_remquol(1.0L, 2.0L, NULL) == 0.0L && errno == EINVAL);

    /* Adjacency for the nextafter family. */
    {
        long double a = cplus_nextafterl(1.0L, 2.0L);
        long double b = nextafterl(1.0L, 2.0L);
        assert(a == b && a > 1.0L && a - 1.0L <= 2.0L * LDBL_EPSILON);
        a = cplus_nexttowardl(1.0L, 2.0L);
        b = nexttowardl(1.0L, 2.0L);
        assert(a == b && a > 1.0L);
    }

    /* nanl produces a NaN for NULL and for a supplied tag. */
    assert(isnan(cplus_nanl(NULL)));
    assert(isnan(cplus_nanl("42")));
    assert(isnan(nanl("42")));

    (void)strcmp; /* string.h included per test convention; no string data used. */
    return 0;
}
