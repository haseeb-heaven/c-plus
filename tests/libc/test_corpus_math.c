/* High-volume deterministic differential corpus for the C+ hardened libc
 * math / FP surface.  Every cplus_* wrapper is compared against the host
 * <math.h> / <complex.h> function it delegates to.  This file is standalone
 * C99 and links only build/libc/libcplus_hardened.a plus the host libm.
 *
 * Coverage per case:
 *   - NaN / Inf / finite classification
 *   - signbit (where meaningful, i.e. neither side is NaN)
 *   - value within an epsilon-scaled tolerance
 *   - errno classification (domain / range / none), including explicit
 *     domain-error tuples such as sqrt(-1), log(0), log(-1), atanhl(1),
 *     pow(-2, 0.5)
 * Plus:
 *   - 200 successful calls with an errno sentinel (no clobber)
 *   - 100 differential FP-flag calls after FE_INVALID is pre-raised
 */
#include <math.h>
#include <float.h>
#include <errno.h>
#include <fenv.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <complex.h>

/* ---- prototypes of the symbols under test (header-free standalone build) -- */
extern float cplus_sinf(float);
extern float cplus_cosf(float);
extern float cplus_tanf(float);
extern float cplus_expf(float);
extern float cplus_logf(float);
extern float cplus_sqrtf(float);
extern float cplus_powf(float, float);
extern float cplus_fmodf(float, float);
extern float cplus_floorf(float);
extern float cplus_ceilf(float);
extern float cplus_roundf(float);
extern float cplus_fabsf(float);
extern float cplus_fminf(float, float);
extern float cplus_fmaxf(float, float);
extern float cplus_fmaf(float, float, float);
extern float cplus_remainderf(float, float);
extern float cplus_fdimf(float, float);

extern long double cplus_sinl(long double);
extern long double cplus_cosl(long double);
extern long double cplus_tanl(long double);
extern long double cplus_expl(long double);
extern long double cplus_logl(long double);
extern long double cplus_sqrtl(long double);
extern long double cplus_powl(long double, long double);
extern long double cplus_fmodl(long double, long double);
extern long double cplus_floorl(long double);
extern long double cplus_ceill(long double);
extern long double cplus_roundl(long double);
extern long double cplus_fabsl(long double);
extern long double cplus_fminl(long double, long double);
extern long double cplus_fmaxl(long double, long double);
extern long double cplus_fmal(long double, long double, long double);
extern long double cplus_remainderl(long double, long double);
extern long double cplus_fdiml(long double, long double);
extern long double cplus_atanhl(long double);

extern double complex cplus_csin(double complex);
extern double complex cplus_clog(double complex);
extern double complex cplus_ccosh(double complex);
extern double complex cplus_cpow(double complex, double complex);

/* ---- function table ----------------------------------------------------- */
#define FN_LIST(X)                                                            \
    X(SINF, "cplus_sinf")       X(COSF, "cplus_cosf")                         \
    X(TANF, "cplus_tanf")       X(EXPF, "cplus_expf")                         \
    X(LOGF, "cplus_logf")       X(SQRTF, "cplus_sqrtf")                       \
    X(POWF, "cplus_powf")       X(FMODF, "cplus_fmodf")                       \
    X(FLOORF, "cplus_floorf")   X(CEILF, "cplus_ceilf")                       \
    X(ROUNDF, "cplus_roundf")   X(FABSF, "cplus_fabsf")                       \
    X(FMINF, "cplus_fminf")     X(FMAXF, "cplus_fmaxf")                       \
    X(FMAF, "cplus_fmaf")       X(REMAINDERF, "cplus_remainderf")             \
    X(FDIMF, "cplus_fdimf")                                                   \
    X(SINL, "cplus_sinl")       X(COSL, "cplus_cosl")                         \
    X(TANL, "cplus_tanl")       X(EXPL, "cplus_expl")                         \
    X(LOGL, "cplus_logl")       X(SQRTL, "cplus_sqrtl")                       \
    X(POWL, "cplus_powl")       X(FMODL, "cplus_fmodl")                       \
    X(FLOORL, "cplus_floorl")   X(CEILL, "cplus_ceill")                       \
    X(ROUNDL, "cplus_roundl")   X(FABSL, "cplus_fabsl")                       \
    X(FMINL, "cplus_fminl")     X(FMAXL, "cplus_fmaxl")                       \
    X(FMAL, "cplus_fmal")       X(REMAINDERL, "cplus_remainderl")             \
    X(FDIML, "cplus_fdiml")     X(ATANHL, "cplus_atanhl")                     \
    X(CSIN, "cplus_csin")       X(CLOG, "cplus_clog")                         \
    X(CCOSH, "cplus_ccosh")     X(CPOW, "cplus_cpow")

#define FN_ENUM(id, name) FN_##id,
#define FN_NAME(id, name) name,
typedef enum { FN_LIST(FN_ENUM) FN_COUNT } FnId;
static const char *const fn_names[FN_COUNT] = { FN_LIST(FN_NAME) };

/* ---- case representation ------------------------------------------------ */
typedef struct {
    long double a, b, c, d; /* up to 4 real args; complex uses (a,b),(c,d) */
} Tuple;

typedef struct {
    int fn;
    Tuple t;
} Case;

typedef struct {
    long double re, im;
    int is_cplx;
} Res;

static double complex mk(long double re, long double im)
{
    return (double)re + (double)im * I;
}
static void set_real(Res *o, long double v)
{
    o->re = v; o->im = 0.0L; o->is_cplx = 0;
}
static void set_cplx(Res *o, double complex v)
{
    o->re = creal(v); o->im = cimag(v); o->is_cplx = 1;
}

#define R1(id, host, wrap, T)                                                  \
    case FN_##id: {                                                            \
        T x = (T)t->a;                                                         \
        set_real(o, use_wrap ? (long double)(wrap)(x)                          \
                             : (long double)(host)(x));                        \
    } break;
#define R2(id, host, wrap, T)                                                  \
    case FN_##id: {                                                            \
        T x = (T)t->a, y = (T)t->b;                                            \
        set_real(o, use_wrap ? (long double)(wrap)(x, y)                       \
                             : (long double)(host)(x, y));                     \
    } break;
#define R3(id, host, wrap, T)                                                  \
    case FN_##id: {                                                            \
        T x = (T)t->a, y = (T)t->b, z = (T)t->c;                              \
        set_real(o, use_wrap ? (long double)(wrap)(x, y, z)                    \
                             : (long double)(host)(x, y, z));                  \
    } break;
#define C1(id, host, wrap)                                                     \
    case FN_##id: {                                                            \
        double complex z = mk(t->a, t->b);                                     \
        set_cplx(o, use_wrap ? (wrap)(z) : (host)(z));                         \
    } break;
#define C2(id, host, wrap)                                                     \
    case FN_##id: {                                                            \
        double complex z = mk(t->a, t->b), w = mk(t->c, t->d);                \
        set_cplx(o, use_wrap ? (wrap)(z, w) : (host)(z, w));                   \
    } break;

static void eval(int fn, const Tuple *t, int use_wrap, Res *o)
{
    switch (fn) {
        /* float family */
        R1(SINF, sinf, cplus_sinf, float)
        R1(COSF, cosf, cplus_cosf, float)
        R1(TANF, tanf, cplus_tanf, float)
        R1(EXPF, expf, cplus_expf, float)
        R1(LOGF, logf, cplus_logf, float)
        R1(SQRTF, sqrtf, cplus_sqrtf, float)
        R2(POWF, powf, cplus_powf, float)
        R2(FMODF, fmodf, cplus_fmodf, float)
        R1(FLOORF, floorf, cplus_floorf, float)
        R1(CEILF, ceilf, cplus_ceilf, float)
        R1(ROUNDF, roundf, cplus_roundf, float)
        R1(FABSF, fabsf, cplus_fabsf, float)
        R2(FMINF, fminf, cplus_fminf, float)
        R2(FMAXF, fmaxf, cplus_fmaxf, float)
        R3(FMAF, fmaf, cplus_fmaf, float)
        R2(REMAINDERF, remainderf, cplus_remainderf, float)
        R2(FDIMF, fdimf, cplus_fdimf, float)
        /* long double family */
        R1(SINL, sinl, cplus_sinl, long double)
        R1(COSL, cosl, cplus_cosl, long double)
        R1(TANL, tanl, cplus_tanl, long double)
        R1(EXPL, expl, cplus_expl, long double)
        R1(LOGL, logl, cplus_logl, long double)
        R1(SQRTL, sqrtl, cplus_sqrtl, long double)
        R2(POWL, powl, cplus_powl, long double)
        R2(FMODL, fmodl, cplus_fmodl, long double)
        R1(FLOORL, floorl, cplus_floorl, long double)
        R1(CEILL, ceill, cplus_ceill, long double)
        R1(ROUNDL, roundl, cplus_roundl, long double)
        R1(FABSL, fabsl, cplus_fabsl, long double)
        R2(FMINL, fminl, cplus_fminl, long double)
        R2(FMAXL, fmaxl, cplus_fmaxl, long double)
        R3(FMAL, fmal, cplus_fmal, long double)
        R2(REMAINDERL, remainderl, cplus_remainderl, long double)
        R2(FDIML, fdiml, cplus_fdiml, long double)
        R1(ATANHL, atanhl, cplus_atanhl, long double)
        /* complex family */
        C1(CSIN, csin, cplus_csin)
        C1(CLOG, clog, cplus_clog)
        C1(CCOSH, ccosh, cplus_ccosh)
        C2(CPOW, cpow, cplus_cpow)
        default: set_real(o, 0.0L); break;
    }
}

/* ---- input corpus ------------------------------------------------------- */
#if defined(__has_builtin)
#  if __has_builtin(__builtin_nanf)
#    define HAVE_NANF_BUILTIN 1
#  endif
#  if __has_builtin(__builtin_nansf)
#    define HAVE_NANSF_BUILTIN 1
#  endif
#endif
#ifndef HAVE_NANF_BUILTIN
#  define HAVE_NANF_BUILTIN 0
#endif
#ifndef HAVE_NANSF_BUILTIN
#  define HAVE_NANSF_BUILTIN 0
#endif

#define N_RANDOM 100
#define N_SPECIAL_SCALARS 42
#define N_SCALARS (N_SPECIAL_SCALARS + N_RANDOM)

static long double scalars[N_SCALARS];
static uint32_t rng_state = 0x1a2b3c4du;

static uint32_t xorshift32(void)
{
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

/* Uniform in (-1,1), then exact power-of-two scaling: always finite. */
static long double gen_finite(void)
{
    uint32_t r = xorshift32();
    long double frac = (long double)(int32_t)(r >> 1) / 2147483648.0L;
    int e = (int)(xorshift32() % 221u) - 110;
    return ldexpl(frac, e);
}

static void init_scalars(void)
{
    int i = 0;
    long double flt_tiny = (long double)nextafterf(0.0f, 1.0f);
    long double dbl_tiny = nextafter(0.0, 1.0);
    long double nanf_payload, nansf_payload;
#if HAVE_NANF_BUILTIN
    nanf_payload = (long double)__builtin_nanf("1");
#else
    nanf_payload = (long double)nanf("1");
#endif
#if HAVE_NANSF_BUILTIN
    nansf_payload = (long double)__builtin_nansf("1");
#else
    nansf_payload = (long double)nanf("2");
#endif
    scalars[i++] = 0.0L;
    scalars[i++] = -0.0L;
    scalars[i++] = 1.0L;
    scalars[i++] = -1.0L;
    scalars[i++] = 2.0L;
    scalars[i++] = -2.0L;
    scalars[i++] = 0.5L;
    scalars[i++] = -0.5L;
    scalars[i++] = 3.0L;
    scalars[i++] = -3.0L;
    scalars[i++] = 10.0L;
    scalars[i++] = -10.0L;
    scalars[i++] = 100.0L;
    scalars[i++] = (long double)FLT_MIN;
    scalars[i++] = -(long double)FLT_MIN;
    scalars[i++] = flt_tiny;              /* FLT "true" min (subnormal) */
    scalars[i++] = -flt_tiny;
    scalars[i++] = (long double)DBL_MIN;
    scalars[i++] = -(long double)DBL_MIN;
    scalars[i++] = dbl_tiny;              /* DBL "true" min (subnormal) */
    scalars[i++] = -dbl_tiny;
    scalars[i++] = (long double)FLT_MAX;
    scalars[i++] = -(long double)FLT_MAX;
    scalars[i++] = (long double)DBL_MAX;
    scalars[i++] = -(long double)DBL_MAX;
    scalars[i++] = LDBL_MAX;
    scalars[i++] = -LDBL_MAX;
    scalars[i++] = INFINITY;
    scalars[i++] = -INFINITY;
    scalars[i++] = (long double)nan("");
    scalars[i++] = -(long double)nan("");
    scalars[i++] = nanf_payload;
    scalars[i++] = -nanf_payload;
    scalars[i++] = nansf_payload;
    scalars[i++] = 3.14159265358979323846L;
    scalars[i++] = -3.14159265358979323846L;
    scalars[i++] = 0.1L;
    scalars[i++] = -0.1L;
    scalars[i++] = 1e-30L;
    scalars[i++] = -1e-30L;
    scalars[i++] = 1e30L;
    scalars[i++] = -1e30L;
    assert(i == N_SPECIAL_SCALARS);
    for (i = 0; i < N_RANDOM; ++i)
        scalars[N_SPECIAL_SCALARS + i] = gen_finite();
}

/* Explicit tuples.  Several are ISO C domain/range errors:
 * sqrt(-1), log(0), log(-1), pow(-2,0.5), pow(0,-1), fmod(x,0),
 * remainder(x,0), atanhl(1), clog(0+0i), cpow(0+0i,-1+0i).
 * Filled at runtime because nan("") is not a constant initializer. */
#define N_SPECIAL_TUPLES 20
static Tuple special_tuples[N_SPECIAL_TUPLES];
#define N_CASES (FN_COUNT * (N_SCALARS + N_SPECIAL_TUPLES))

static Case cases[N_CASES];

static void init_special_tuples(void)
{
    long double qnan = (long double)nan("");
    special_tuples[0] = (Tuple){ -1.0L, 0.0L, 0.0L, 0.0L };
    special_tuples[1] = (Tuple){  0.0L, 0.0L, 0.0L, 0.0L };
    special_tuples[2] = (Tuple){  1.0L, 0.0L, 0.0L, 0.0L };
    special_tuples[3] = (Tuple){ -2.0L, 0.5L, 0.0L, 0.0L };
    special_tuples[4] = (Tuple){  0.0L, 0.0L, 0.0L, 0.0L };
    special_tuples[5] = (Tuple){  0.0L, -1.0L, 0.0L, 0.0L };
    special_tuples[6] = (Tuple){  1.0L, 0.0L, 0.0L, 0.0L };
    special_tuples[7] = (Tuple){  INFINITY, 0.0L, 0.0L, 0.0L };
    special_tuples[8] = (Tuple){ -INFINITY, 0.0L, 0.0L, 0.0L };
    special_tuples[9] = (Tuple){ qnan, 1.0L, 0.0L, 0.0L };
    special_tuples[10] = (Tuple){ 1.0L, qnan, 0.0L, 0.0L };
    special_tuples[11] = (Tuple){ (long double)FLT_MAX, (long double)FLT_MAX,
                                  -(long double)FLT_MAX, 0.0L };
    special_tuples[12] = (Tuple){ 1.57079632679489661923L, 0.0L, 0.0L, 0.0L };
    special_tuples[13] = (Tuple){ 1e30L, 1e30L, 0.0L, 0.0L };
    special_tuples[14] = (Tuple){ 0.0L, 0.0L, -1.0L, 0.0L };
    special_tuples[15] = (Tuple){ 0.0L, 0.0L, 0.0L, 0.0L };
    special_tuples[16] = (Tuple){ 0.0L, INFINITY, 0.0L, 0.0L };
    special_tuples[17] = (Tuple){ 1.0L, -0.0L, 0.0L, 0.0L };
    special_tuples[18] = (Tuple){ 1.0L, 0.0L, 0.0L, 0.0L };
    special_tuples[19] = (Tuple){ -1.0L, 1.0L, 0.0L, 0.0L };
}

static void init_cases(void)
{
    int f, i, k = 0;
    init_special_tuples();
    for (f = 0; f < FN_COUNT; ++f)
        for (i = 0; i < N_SCALARS; ++i) {
            cases[k].fn = f;
            cases[k].t.a = scalars[i];
            cases[k].t.b = scalars[i];
            cases[k].t.c = scalars[i];
            cases[k].t.d = scalars[i];
            ++k;
        }
    for (f = 0; f < FN_COUNT; ++f)
        for (i = 0; i < N_SPECIAL_TUPLES; ++i) {
            cases[k].fn = f;
            cases[k].t = special_tuples[i];
            ++k;
        }
    assert(k == N_CASES);
}

/* ---- comparison helpers ------------------------------------------------- */
static int findings;

static int cls(long double v)
{
    if (isnan(v)) return 0;   /* NaN */
    if (isinf(v)) return 1;   /* Inf */
    return 2;                 /* finite */
}
static const char *cls_name(int c)
{
    return c == 0 ? "NaN" : c == 1 ? "Inf" : "finite";
}
static int res_cls(const Res *r)
{
    if (r->is_cplx) {
        if (isnan(r->re) || isnan(r->im)) return 0;
        if (isinf(r->re) || isinf(r->im)) return 1;
        return 2;
    }
    return cls(r->re);
}
static int same_val(long double a, long double b, long double eps)
{
    long double d, s;
    if (cls(a) != cls(b)) return 0;
    if (isnan(a)) return 1;             /* both NaN; payload ignored */
    if (isinf(a)) return a == b;         /* both Inf; sign compared */
    d = fabsl(a - b);
    s = fabsl(a) > fabsl(b) ? fabsl(a) : fabsl(b);
    if (s < 1.0L) s = 1.0L;
    return d <= eps * s;
}
static int sign_ok(long double a, long double b)
{
    if (isnan(a) || isnan(b)) return 1;  /* signbit not meaningful for NaN */
    return signbit(a) == signbit(b);
}
static int ecls(int e)
{
    if (e == 0) return 0;
    if (e == EDOM) return 1;
    if (e == ERANGE) return 2;
    return 3;
}
static long double tol_for(int fn)
{
    if (fn < FN_SINL) return 64.0L * FLT_EPSILON;
    if (fn < FN_CSIN) return 64.0L * DBL_EPSILON;
    return 4096.0L * DBL_EPSILON; /* complex trig: allow a few hundred ulps */
}

static void report(const Case *c, const char *what,
                   const Res *h, const Res *w, int he, int we)
{
    ++findings;
    printf("FINDING: %s a=%.21Lg b=%.21Lg c=%.21Lg d=%.21Lg %s"
           " | host re=%.21Lg im=%.21Lg cls=%s errno=%d"
           " | wrapper re=%.21Lg im=%.21Lg cls=%s errno=%d\n",
           fn_names[c->fn], c->t.a, c->t.b, c->t.c, c->t.d, what,
           h->re, h->im, cls_name(res_cls(h)), he,
           w->re, w->im, cls_name(res_cls(w)), we);
}

/* ---- main --------------------------------------------------------------- */
int main(void)
{
    fenv_t saved_env;
    int i, he, we, sentinel_ok = 0, flag_cases = 0;
    Res hr, wr;

    fegetenv(&saved_env);
    init_scalars();
    init_cases();

    /* 1) full differential table */
    for (i = 0; i < N_CASES; ++i) {
        Case *c = &cases[i];
        long double eps = tol_for(c->fn);
        errno = 0;
        eval(c->fn, &c->t, 0, &hr);
        he = errno;
        errno = 0;
        eval(c->fn, &c->t, 1, &wr);
        we = errno;

        if (hr.is_cplx != wr.is_cplx) {
            report(c, "result-kind mismatch", &hr, &wr, he, we);
            continue;
        }
        if (res_cls(&hr) != res_cls(&wr)) {
            report(c, "classification", &hr, &wr, he, we);
            continue;
        }
        if (!same_val(hr.re, wr.re, eps)) {
            report(c, "real-value", &hr, &wr, he, we);
            continue;
        }
        if (hr.is_cplx && !same_val(hr.im, wr.im, eps)) {
            report(c, "imag-value", &hr, &wr, he, we);
            continue;
        }
        if (!sign_ok(hr.re, wr.re) || (hr.is_cplx && !sign_ok(hr.im, wr.im))) {
            report(c, "signbit", &hr, &wr, he, we);
            continue;
        }
        if (ecls(he) != ecls(we)) {
            report(c, "errno-class", &hr, &wr, he, we);
            continue;
        }
    }

    /* 2) a successful wrapper call must not clobber a sentinel errno (200) */
    for (i = 0; i < N_CASES && sentinel_ok < 200; ++i) {
        Case *c = &cases[i];
        errno = 0;
        eval(c->fn, &c->t, 0, &hr); /* host first: is this a successful call? */
        if (errno != 0) continue;
        errno = 0x5eed;
        eval(c->fn, &c->t, 1, &wr);
        if (errno != 0x5eed) {
            report(c, "errno-clobber-on-success", &hr, &wr, 0x5eed, errno);
        }
        ++sentinel_ok;
    }
    if (sentinel_ok < 200) {
        ++findings;
        printf("FINDING: errno-sentinel only %d/200 successful cases available\n",
               sentinel_ok);
    }

    /* 3) FP flags: differential vs host with FE_INVALID pre-raised (100) */
    for (i = 0; i < N_CASES && flag_cases < 100; ++i) {
        Case *c = &cases[i];
        int hf, wf;
        feclearexcept(FE_ALL_EXCEPT);
        feraiseexcept(FE_INVALID);
        eval(c->fn, &c->t, 0, &hr);
        hf = fetestexcept(FE_ALL_EXCEPT);

        feclearexcept(FE_ALL_EXCEPT);
        feraiseexcept(FE_INVALID);
        eval(c->fn, &c->t, 1, &wr);
        wf = fetestexcept(FE_ALL_EXCEPT);

        if (hf != wf || !(wf & FE_INVALID)) {
            report(c, "fp-flags-after-FE_INVALID", &hr, &wr, hf, wf);
        }
        ++flag_cases;
    }
    if (flag_cases < 100) {
        ++findings;
        printf("FINDING: fp-flag differential only %d/100 cases available\n",
               flag_cases);
    }

    /* 4) restore the caller's rounding mode and clear exceptions */
    fesetenv(&saved_env);
    feclearexcept(FE_ALL_EXCEPT);

    if (findings == 0)
        printf("corpus_math: %d cases ok\n", N_CASES);
    else
        printf("corpus_math: %d cases, %d FINDINGs\n", N_CASES, findings);
    return findings ? 1 : 0;
}
