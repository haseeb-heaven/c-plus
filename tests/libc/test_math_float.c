/* Standalone differential test for the cplus_* float-suffix math wrappers.
 * Each wrapper is compared against the corresponding HOST <math.h> function
 * called directly here. NaN/Inf are compared by classification, finite values
 * with an FLT_EPSILON-scaled tolerance. No undefined inputs are used. */
#include <assert.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Local declarations: the public header is owned by another agent. */
extern float cplus_sinf(float);
extern float cplus_cosf(float);
extern float cplus_tanf(float);
extern float cplus_asinf(float);
extern float cplus_acosf(float);
extern float cplus_atanf(float);
extern float cplus_atan2f(float, float);
extern float cplus_sinhf(float);
extern float cplus_coshf(float);
extern float cplus_tanhf(float);
extern float cplus_asinhf(float);
extern float cplus_acoshf(float);
extern float cplus_atanhf(float);
extern float cplus_expf(float);
extern float cplus_exp2f(float);
extern float cplus_expm1f(float);
extern float cplus_logf(float);
extern float cplus_log2f(float);
extern float cplus_log10f(float);
extern float cplus_log1pf(float);
extern float cplus_powf(float, float);
extern float cplus_sqrtf(float);
extern float cplus_cbrtf(float);
extern float cplus_hypotf(float, float);
extern float cplus_floorf(float);
extern float cplus_ceilf(float);
extern float cplus_truncf(float);
extern float cplus_roundf(float);
extern float cplus_nearbyintf(float);
extern float cplus_rintf(float);
extern long cplus_lrintf(float);
extern long long cplus_llrintf(float);
extern float cplus_lroundf(float);
extern float cplus_llroundf(float);
extern float cplus_fabsf(float);
extern float cplus_fmodf(float, float);
extern float cplus_remainderf(float, float);
extern float cplus_remquof(float, float, int *);
extern float cplus_fdimf(float, float);
extern float cplus_fminf(float, float);
extern float cplus_fmaxf(float, float);
extern float cplus_fmaf(float, float, float);
extern float cplus_copysignf(float, float);
extern float cplus_ldexpf(float, int);
extern float cplus_frexpf(float, int *);
extern float cplus_modff(float, float *);
extern float cplus_scalbnf(float, int);
extern float cplus_scalblnf(float, long);
extern int cplus_ilogbf(float);
extern float cplus_logbf(float);
extern float cplus_erff(float);
extern float cplus_erfcf(float);
extern float cplus_tgammaf(float);
extern float cplus_lgammaf(float);
extern float cplus_nextafterf(float, float);
extern float cplus_nexttowardf(float, long double);
extern float cplus_nanf(const char *);

/* 0 = NaN, 1 = +Inf, 2 = -Inf, 3 = finite. */
static int classifyf(float v) {
    if (isnan(v)) return 0;
    if (isinf(v)) return signbit(v) ? 2 : 1;
    return 3;
}

static int closef(float a, float b) {
    if (isnan(a) || isnan(b)) return isnan(a) && isnan(b);
    if (isinf(a) || isinf(b))
        return isinf(a) && isinf(b) && signbit(a) == signbit(b);
    {
        float m = fabsf(a) > fabsf(b) ? fabsf(a) : fabsf(b);
        float tol = FLT_EPSILON * (m > 1.0f ? m : 1.0f) * 16.0f;
        return fabsf(a - b) <= tol;
    }
}

#define CHECK(got, expect) assert(closef((got), (expect)))
#define CHECK_CLASS(got, expect) assert(classifyf((got)) == classifyf((expect)))

int main(void) {
    float x, y;
    int i, j;
    float fi, fj;
    long l;
    long long ll;

    /* --- normal inputs, unary --- */
    x = 0.5f;
    CHECK(cplus_sinf(x), sinf(x));
    CHECK(cplus_cosf(x), cosf(x));
    CHECK(cplus_tanf(0.3f), tanf(0.3f));
    CHECK(cplus_asinf(x), asinf(x));
    CHECK(cplus_acosf(x), acosf(x));
    CHECK(cplus_atanf(x), atanf(x));
    CHECK(cplus_sinhf(x), sinhf(x));
    CHECK(cplus_coshf(x), coshf(x));
    CHECK(cplus_tanhf(x), tanhf(x));
    CHECK(cplus_asinhf(x), asinhf(x));
    CHECK(cplus_acoshf(2.0f), acoshf(2.0f));
    CHECK(cplus_atanhf(x), atanhf(x));
    CHECK(cplus_expf(1.0f), expf(1.0f));
    CHECK(cplus_exp2f(3.0f), exp2f(3.0f));
    CHECK(cplus_expm1f(0.25f), expm1f(0.25f));
    CHECK(cplus_logf(2.0f), logf(2.0f));
    CHECK(cplus_log2f(8.0f), log2f(8.0f));
    CHECK(cplus_log10f(100.0f), log10f(100.0f));
    CHECK(cplus_log1pf(0.5f), log1pf(0.5f));
    CHECK(cplus_sqrtf(2.0f), sqrtf(2.0f));
    CHECK(cplus_cbrtf(27.0f), cbrtf(27.0f));
    CHECK(cplus_floorf(1.9f), floorf(1.9f));
    CHECK(cplus_ceilf(1.1f), ceilf(1.1f));
    CHECK(cplus_truncf(-1.9f), truncf(-1.9f));
    CHECK(cplus_roundf(1.5f), roundf(1.5f));
    CHECK(cplus_nearbyintf(1.5f), nearbyintf(1.5f));
    CHECK(cplus_rintf(2.5f), rintf(2.5f));
    CHECK(cplus_fabsf(-3.0f), fabsf(-3.0f));
    CHECK(cplus_erff(x), erff(x));
    CHECK(cplus_erfcf(x), erfcf(x));
    CHECK(cplus_tgammaf(3.0f), tgammaf(3.0f));
    CHECK(cplus_lgammaf(3.0f), lgammaf(3.0f));
    CHECK(cplus_logbf(8.0f), logbf(8.0f));
    assert(cplus_ilogbf(8.0f) == ilogbf(8.0f));

    /* --- normal inputs, binary / integer-returning --- */
    CHECK(cplus_powf(2.0f, 3.0f), powf(2.0f, 3.0f));
    CHECK(cplus_atan2f(1.0f, 2.0f), atan2f(1.0f, 2.0f));
    CHECK(cplus_hypotf(3.0f, 4.0f), hypotf(3.0f, 4.0f));
    CHECK(cplus_fmodf(5.5f, 2.0f), fmodf(5.5f, 2.0f));
    CHECK(cplus_remainderf(5.0f, 3.0f), remainderf(5.0f, 3.0f));
    CHECK(cplus_fdimf(5.0f, 3.0f), fdimf(5.0f, 3.0f));
    CHECK(cplus_fminf(1.5f, -2.0f), fminf(1.5f, -2.0f));
    CHECK(cplus_fmaxf(1.5f, -2.0f), fmaxf(1.5f, -2.0f));
    CHECK(cplus_fminf(NAN, 2.0f), fminf(NAN, 2.0f));
    CHECK(cplus_fmaxf(NAN, 2.0f), fmaxf(NAN, 2.0f));
    CHECK(cplus_fmaf(2.0f, 3.0f, 4.0f), fmaf(2.0f, 3.0f, 4.0f));
    CHECK(cplus_copysignf(1.0f, -2.0f), copysignf(1.0f, -2.0f));
    CHECK(cplus_ldexpf(0.5f, 3), ldexpf(0.5f, 3));
    CHECK(cplus_scalbnf(0.5f, 3), scalbnf(0.5f, 3));
    CHECK(cplus_scalblnf(0.5f, 3L), scalblnf(0.5f, 3L));
    l = cplus_lrintf(2.0f); i = lrintf(2.0f); assert(l == i);
    ll = cplus_llrintf(2.0f); { long long h = llrintf(2.0f); assert(ll == h); }
    CHECK(cplus_lroundf(2.6f), lroundf(2.6f));
    CHECK(cplus_llroundf(2.6f), llroundf(2.6f));

    /* --- NaN / infinity: classify, not exact bits --- */
    CHECK_CLASS(cplus_sinf(NAN), sinf(NAN));
    CHECK_CLASS(cplus_cosf(INFINITY), cosf(INFINITY));
    CHECK_CLASS(cplus_expf(INFINITY), expf(INFINITY));   /* +Inf */
    CHECK_CLASS(cplus_expf(-INFINITY), expf(-INFINITY)); /* +0 */
    CHECK_CLASS(cplus_logf(0.0f), logf(0.0f));           /* -Inf */
    CHECK_CLASS(cplus_sqrtf(-1.0f), sqrtf(-1.0f));       /* NaN */
    CHECK_CLASS(cplus_sqrtf(INFINITY), sqrtf(INFINITY)); /* +Inf */
    CHECK_CLASS(cplus_log10f(0.0f), log10f(0.0f));
    CHECK_CLASS(cplus_tanhf(INFINITY), tanhf(INFINITY));
    CHECK_CLASS(cplus_atanf(INFINITY), atanf(INFINITY));
    CHECK_CLASS(cplus_powf(0.0f, -1.0f), powf(0.0f, -1.0f)); /* +Inf */

    /* --- signed zero --- */
    x = -0.0f;
    assert(signbit(cplus_copysignf(1.0f, x)) == signbit(copysignf(1.0f, x)));
    assert(signbit(cplus_sqrtf(x)) == signbit(sqrtf(x)));
    assert(signbit(cplus_floorf(x)) == signbit(floorf(x)));
    assert(signbit(cplus_ceilf(x)) == signbit(ceilf(x)));
    assert(signbit(cplus_roundf(x)) == signbit(roundf(x)));
    assert(signbit(cplus_fabsf(x)) == signbit(fabsf(x)));

    /* --- domain errors: compare host classification / return --- */
    errno = 0;
    CHECK_CLASS(cplus_sqrtf(-1.0f), sqrtf(-1.0f));
    errno = 0;
    CHECK_CLASS(cplus_logf(0.0f), logf(0.0f));
    errno = 0;
    CHECK_CLASS(cplus_logf(-1.0f), logf(-1.0f));
    errno = 0;
    CHECK_CLASS(cplus_acosf(2.0f), acosf(2.0f));
    errno = 0;
    assert(cplus_ilogbf(0.0f) == ilogbf(0.0f));

    /* --- out-parameter success paths vs host --- */
    i = 0; j = 0;
    x = cplus_frexpf(8.0f, &i);
    y = frexpf(8.0f, &j);
    CHECK(x, y); assert(i == j); assert(i == 4); /* 8 = 0.5 * 2^4 */

    fi = 0.0f; fj = 0.0f;
    x = cplus_modff(-3.75f, &fi);
    y = modff(-3.75f, &fj);
    CHECK(x, y); CHECK(fi, fj); assert(fi == -3.0f && x == -0.75f);

    i = 0; j = 0;
    x = cplus_remquof(5.0f, 3.0f, &i);
    y = remquof(5.0f, 3.0f, &j);
    CHECK(x, y); assert(i == j);

    /* --- NULL out-parameter: hardened EINVAL / 0.0f path --- */
    errno = 0;
    assert(cplus_frexpf(8.0f, NULL) == 0.0f); assert(errno == EINVAL);
    errno = 0;
    assert(cplus_modff(3.75f, NULL) == 0.0f); assert(errno == EINVAL);
    errno = 0;
    assert(cplus_remquof(5.0f, 3.0f, NULL) == 0.0f); assert(errno == EINVAL);

    /* --- nextafter / nexttoward adjacency --- */
    CHECK(cplus_nextafterf(1.0f, 2.0f), nextafterf(1.0f, 2.0f));
    assert(cplus_nextafterf(1.0f, 2.0f) > 1.0f);
    CHECK(cplus_nextafterf(1.0f, 0.0f), nextafterf(1.0f, 0.0f));
    assert(cplus_nextafterf(1.0f, 0.0f) < 1.0f);
    CHECK(cplus_nexttowardf(1.0f, 2.0L), nexttowardf(1.0f, 2.0L));
    assert(cplus_nexttowardf(1.0f, 2.0L) > 1.0f);

    /* --- nanf --- */
    assert(isnan(cplus_nanf("")));
    assert(classifyf(cplus_nanf("")) == classifyf(nanf("")));

    printf("test_math_float: PASS\n");
    return 0;
}
