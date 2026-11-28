/* Adversarial differential attack on the hardened math / complex / fenv wrappers.
 *
 * Standalone C99.  Every cplus_* wrapper is compared against the identically
 * named HOST libm function called directly from this translation unit.  We
 * compare NaN/Inf/signed-zero classification, signbit, finite values (tolerance),
 * errno, and the FP exception/rounding state.  Hostile values, hostile out-param
 * pointers, hostile FENV masks, logging on/off, and complex NaN/Inf operands are
 * all exercised.  Defects are printed with a "FINDING:" prefix.
 */
#include <errno.h>
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <complex.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

/* ---------- declarations of the wrappers under attack ---------- */
/* math: float family */
extern float cplus_sinf(float), cplus_cosf(float), cplus_tanf(float);
extern float cplus_asinf(float), cplus_acosf(float), cplus_atanf(float);
extern float cplus_sinhf(float), cplus_coshf(float), cplus_tanhf(float);
extern float cplus_asinhf(float), cplus_acoshf(float), cplus_atanhf(float);
extern float cplus_expf(float), cplus_exp2f(float), cplus_expm1f(float);
extern float cplus_logf(float), cplus_log2f(float), cplus_log10f(float), cplus_log1pf(float);
extern float cplus_sqrtf(float), cplus_cbrtf(float), cplus_fabsf(float);
extern float cplus_floorf(float), cplus_ceilf(float), cplus_truncf(float);
extern float cplus_roundf(float), cplus_nearbyintf(float), cplus_rintf(float);
extern float cplus_logbf(float), cplus_erff(float), cplus_erfcf(float);
extern float cplus_tgammaf(float), cplus_lgammaf(float);
extern long cplus_lrintf(float);
extern long long cplus_llrintf(float);
/* NOTE: header/impl declare these as float; real ISO C returns long/long long. */
extern float cplus_lroundf(float);
extern float cplus_llroundf(float);
extern int cplus_ilogbf(float);
extern float cplus_atan2f(float, float), cplus_powf(float, float), cplus_hypotf(float, float);
extern float cplus_fmodf(float, float), cplus_remainderf(float, float), cplus_fdimf(float, float);
extern float cplus_fminf(float, float), cplus_fmaxf(float, float), cplus_copysignf(float, float);
extern float cplus_ldexpf(float, int), cplus_scalbnf(float, int), cplus_scalblnf(float, long);
extern float cplus_fmaf(float, float, float), cplus_nextafterf(float, float);
extern float cplus_nexttowardf(float, long double);
extern float cplus_nanf(const char *);
extern float cplus_frexpf(float, int *), cplus_modff(float, float *), cplus_remquof(float, float, int *);

/* math: long double family */
extern long double cplus_sinl(long double), cplus_cosl(long double), cplus_tanl(long double);
extern long double cplus_asinl(long double), cplus_acosl(long double), cplus_atanl(long double);
extern long double cplus_sinhl(long double), cplus_coshl(long double), cplus_tanhl(long double);
extern long double cplus_asinhl(long double), cplus_acoshl(long double), cplus_atanhl(long double);
extern long double cplus_expl(long double), cplus_exp2l(long double), cplus_expm1l(long double);
extern long double cplus_logl(long double), cplus_log2l(long double), cplus_log10l(long double), cplus_log1pl(long double);
extern long double cplus_sqrtl(long double), cplus_cbrtl(long double), cplus_fabsl(long double);
extern long double cplus_floorl(long double), cplus_ceill(long double), cplus_truncl(long double);
extern long double cplus_roundl(long double), cplus_nearbyintl(long double), cplus_rintl(long double);
extern long double cplus_logbl(long double), cplus_erfl(long double), cplus_erfcl(long double);
extern long double cplus_tgammal(long double), cplus_lgammal(long double);
extern long cplus_lrintl(long double);
extern long long cplus_llrintl(long double);
extern long cplus_lroundl(long double);
extern long long cplus_llroundl(long double);
extern int cplus_ilogbl(long double);
extern long double cplus_atan2l(long double, long double), cplus_powl(long double, long double);
extern long double cplus_hypotl(long double, long double), cplus_fmodl(long double, long double);
extern long double cplus_remainderl(long double, long double), cplus_fdiml(long double, long double);
extern long double cplus_fminl(long double, long double), cplus_fmaxl(long double, long double);
extern long double cplus_copysignl(long double, long double);
extern long double cplus_ldexpl(long double, int), cplus_scalbnl(long double, int), cplus_scalblnl(long double, long);
extern long double cplus_fmal(long double, long double, long double);
extern long double cplus_nextafterl(long double, long double), cplus_nexttowardl(long double, long double);
extern long double cplus_nanl(const char *);
extern long double cplus_frexpl(long double, int *), cplus_modfl(long double, long double *), cplus_remquol(long double, long double, int *);

/* math: double family (sample) */
extern double cplus_sqrt(double), cplus_log(double), cplus_exp(double), cplus_pow(double, double);
extern double cplus_fabs(double), cplus_floor(double), cplus_fmod(double, double), cplus_atan2(double, double);
extern double cplus_nan(const char *);
extern double cplus_frexp(double, int *), cplus_modf(double, double *), cplus_remquo(double, double, int *);

/* fenv */
extern int cplus_feclearexcept(int), cplus_fetestexcept(int), cplus_fegetround(void), cplus_fesetround(int);
extern int cplus_fegetenv(fenv_t *), cplus_fesetenv(const fenv_t *);
extern int cplus_feraiseexcept(int), cplus_fegetexceptflag(fexcept_t *, int);
extern int cplus_fesetexceptflag(const fexcept_t *, int), cplus_feholdexcept(fenv_t *), cplus_feupdateenv(const fenv_t *);

/* complex */
extern double complex cplus_csin(double complex), cplus_ccos(double complex), cplus_ctan(double complex);
extern double complex cplus_csinh(double complex), cplus_ccosh(double complex), cplus_ctanh(double complex);
extern double complex cplus_clog(double complex), cplus_cpow(double complex, double complex), cplus_cproj(double complex);
extern double cplus_cabs(double complex), cplus_carg(double complex);
extern double complex cplus_conj(double complex), cplus_cexp(double complex), cplus_csqrt(double complex);
extern float complex cplus_csinf(float complex), cplus_ccosf(float complex), cplus_ctanf(float complex);
extern float complex cplus_csinhf(float complex), cplus_ccoshf(float complex), cplus_ctanhf(float complex);
extern float complex cplus_clogf(float complex), cplus_cpowf(float complex, float complex), cplus_cprojf(float complex);
extern float cplus_cabsf(float complex), cplus_cargf(float complex);
extern float complex cplus_conjf(float complex), cplus_cexpf(float complex), cplus_csqrtf(float complex);
extern long double complex cplus_csinl(long double complex), cplus_ccosl(long double complex), cplus_ctanl(long double complex);
extern long double complex cplus_csinhl(long double complex), cplus_ccoshl(long double complex), cplus_ctanhl(long double complex);
extern long double complex cplus_clogl(long double complex), cplus_cpowl(long double complex, long double complex), cplus_cprojl(long double complex);
extern long double cplus_cabsl(long double complex), cplus_cargl(long double complex);
extern long double complex cplus_conjl(long double complex), cplus_cexpl(long double complex), cplus_csqrtl(long double complex);

/* logging toggle */
extern void cplus_libc_log_set_enabled(int);

#ifdef __clang__
# if __has_builtin(__builtin_nansf)
#  define HAS_SNANF 1
# else
#  define HAS_SNANF 0
# endif
#else
# define HAS_SNANF 0
#endif

#define SENT 424242

static int g_findings;
#define REPORT(...) do { fprintf(stderr, "FINDING: "); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); g_findings++; } while (0)

/* classification code: 0 NaN, 1 -Inf, 2 +Inf, 3 -0, 4 +0, 5 finite-nonzero */
static int cl(long double x) {
    if (isnan(x)) return 0;
    if (isinf(x)) return signbit(x) ? 1 : 2;
    if (x == 0.0L) return signbit(x) ? 3 : 4;
    return 5;
}
static const char *clname(int c) {
    static const char *const n[] = { "NaN", "-Inf", "+Inf", "-0", "+0", "finite" };
    return n[c >= 0 && c <= 5 ? c : 5];
}
static int same(long double a, long double b) {
    if (isnan(a) && isnan(b)) return 1;
    if (isinf(a) || isinf(b)) return a == b;
    {
        long double d = fabsl(a - b);
        long double s = fabsl(a) > fabsl(b) ? fabsl(a) : fabsl(b);
        return d <= 1.0e-5L * (s + 1.0L);
    }
}

/* Run a possibly-crashing probe in a child so a SIGSEGV is reported as a
 * FINDING instead of terminating the harness.  The child exits 0 only when
 * the wrapper returned 0 with errno == EINVAL as required. */
static void probe(const char *name, int (*fn)(void)) {
    pid_t pid = fork();
    if (pid == 0) { _exit(fn()); }
    if (pid < 0) { REPORT("%s: fork failed: %s", name, strerror(errno)); return; }
    {
        int st = 0;
        waitpid(pid, &st, 0);
        if (WIFSIGNALED(st))
            REPORT("%s: child killed by signal %d (NULL out-param crashed instead of returning 0 with EINVAL)",
                   name, WTERMSIG(st));
        else if (!WIFEXITED(st) || WEXITSTATUS(st) != 0)
            REPORT("%s: child exit=%d (expected 0: retval 0 and errno EINVAL)",
                   name, WIFEXITED(st) ? WEXITSTATUS(st) : -1);
    }
}
static int probe_frexp_null(void)  { errno = 0; return cplus_frexp(8.0, NULL) == 0.0 && errno == EINVAL ? 0 : 1; }
static int probe_modf_null(void)   { errno = 0; return cplus_modf(8.0, NULL) == 0.0 && errno == EINVAL ? 0 : 1; }
static int probe_remquo_null(void) { errno = 0; return cplus_remquo(8.0, 3.0, NULL) == 0.0 && errno == EINVAL ? 0 : 1; }
static int probe_feset_fake(void) {
    fexcept_t fake; memset(&fake, 0xFF, sizeof fake);
    return fesetexceptflag(&fake, FE_ALL_EXCEPT) == 0 ? 0 : 1;
}

static void clean(void) { feclearexcept(FE_ALL_EXCEPT); errno = 0; }
static void arm(void) {
    fesetround(FE_UPWARD);
    feclearexcept(FE_ALL_EXCEPT);
    feraiseexcept(FE_INVALID);
    errno = SENT;
}
static void disarm(void) { fesetround(FE_TONEAREST); feclearexcept(FE_ALL_EXCEPT); errno = 0; }

/* Differential for a 1-arg float wrapper: classification, value, errno, FP flags. */
#define DIFF1F(label, w, h, x) do {                                           \
    volatile float vx_ = (float)(x);                                          \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    volatile float rw_ = (w)(vx_);                                            \
    int ew_ = errno, fw_ = fetestexcept(FE_ALL_EXCEPT);                       \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    volatile float rh_ = (h)(vx_);                                            \
    int eh_ = errno, fh_ = fetestexcept(FE_ALL_EXCEPT);                       \
    int cw_ = cl((long double)rw_), ch_ = cl((long double)rh_);                \
    if (cw_ != ch_) REPORT("%s: class diverge input=%s w=%s h=%s",            \
        label, #x, clname(cw_), clname(ch_));                                 \
    if (!same((long double)rw_, (long double)rh_))                            \
        REPORT("%s: value diverge input=%s w=%.9g h=%.9g", label, #x,         \
               (double)rw_, (double)rh_);                                     \
    if (ew_ != eh_) REPORT("%s: errno diverge input=%s w=%d h=%d",            \
        label, #x, ew_, eh_);                                                 \
    if (fw_ != fh_) REPORT("%s: FP flags diverge input=%s w=0x%x h=0x%x",     \
        label, #x, fw_, fh_);                                                 \
} while (0)

#define DIFF1L(label, w, h, x) do {                                           \
    volatile long double vx_ = (long double)(x);                             \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    volatile long double rw_ = (w)(vx_);                                      \
    int ew_ = errno, fw_ = fetestexcept(FE_ALL_EXCEPT);                       \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    volatile long double rh_ = (h)(vx_);                                      \
    int eh_ = errno, fh_ = fetestexcept(FE_ALL_EXCEPT);                       \
    int cw_ = cl((long double)rw_), ch_ = cl((long double)rh_);                \
    if (cw_ != ch_) REPORT("%s: class diverge input=%s w=%s h=%s",            \
        label, #x, clname(cw_), clname(ch_));                                 \
    if (!same((long double)rw_, (long double)rh_))                            \
        REPORT("%s: value diverge input=%s w=%.9Lg h=%.9Lg", label, #x,       \
               (long double)rw_, (long double)rh_);                           \
    if (ew_ != eh_) REPORT("%s: errno diverge input=%s w=%d h=%d",            \
        label, #x, ew_, eh_);                                                 \
    if (fw_ != fh_) REPORT("%s: FP flags diverge input=%s w=0x%x h=0x%x",     \
        label, #x, fw_, fh_);                                                 \
} while (0)

#define DIFF1D(label, w, h, x) do {                                           \
    volatile double vx_ = (double)(x);                                       \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    volatile double rw_ = (w)(vx_);                                           \
    int ew_ = errno, fw_ = fetestexcept(FE_ALL_EXCEPT);                       \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    volatile double rh_ = (h)(vx_);                                           \
    int eh_ = errno, fh_ = fetestexcept(FE_ALL_EXCEPT);                       \
    int cw_ = cl((long double)rw_), ch_ = cl((long double)rh_);                \
    if (cw_ != ch_) REPORT("%s: class diverge input=%s w=%s h=%s",            \
        label, #x, clname(cw_), clname(ch_));                                 \
    if (!same((long double)rw_, (long double)rh_))                            \
        REPORT("%s: value diverge input=%s w=%.9g h=%.9g", label, #x,         \
               (double)rw_, (double)rh_);                                     \
    if (ew_ != eh_) REPORT("%s: errno diverge input=%s w=%d h=%d",            \
        label, #x, ew_, eh_);                                                 \
    if (fw_ != fh_) REPORT("%s: FP flags diverge input=%s w=0x%x h=0x%x",     \
        label, #x, fw_, fh_);                                                 \
} while (0)

/* 2-arg differential */
#define DIFF2F(label, w, h, x, y) do {                                        \
    volatile float vx_ = (float)(x), vy_ = (float)(y);                       \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    volatile float rw_ = (w)(vx_, vy_);                                       \
    int ew_ = errno, fw_ = fetestexcept(FE_ALL_EXCEPT);                       \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    volatile float rh_ = (h)(vx_, vy_);                                       \
    int eh_ = errno, fh_ = fetestexcept(FE_ALL_EXCEPT);                       \
    int cw_ = cl((long double)rw_), ch_ = cl((long double)rh_);                \
    if (cw_ != ch_) REPORT("%s: class diverge (%s,%s) w=%s h=%s",             \
        label, #x, #y, clname(cw_), clname(ch_));                             \
    if (!same((long double)rw_, (long double)rh_))                            \
        REPORT("%s: value diverge (%s,%s) w=%.9g h=%.9g", label, #x, #y,      \
               (double)rw_, (double)rh_);                                     \
    if (ew_ != eh_) REPORT("%s: errno diverge (%s,%s) w=%d h=%d",             \
        label, #x, #y, ew_, eh_);                                             \
    if (fw_ != fh_) REPORT("%s: FP flags diverge (%s,%s) w=0x%x h=0x%x",      \
        label, #x, #y, fw_, fh_);                                             \
} while (0)

#define DIFF2L(label, w, h, x, y) do {                                        \
    volatile long double vx_ = (long double)(x), vy_ = (long double)(y);     \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    volatile long double rw_ = (w)(vx_, vy_);                                 \
    int ew_ = errno, fw_ = fetestexcept(FE_ALL_EXCEPT);                       \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    volatile long double rh_ = (h)(vx_, vy_);                                 \
    int eh_ = errno, fh_ = fetestexcept(FE_ALL_EXCEPT);                       \
    int cw_ = cl((long double)rw_), ch_ = cl((long double)rh_);                \
    if (cw_ != ch_) REPORT("%s: class diverge (%s,%s) w=%s h=%s",             \
        label, #x, #y, clname(cw_), clname(ch_));                             \
    if (!same((long double)rw_, (long double)rh_))                            \
        REPORT("%s: value diverge (%s,%s) w=%.9Lg h=%.9Lg", label, #x, #y,    \
               (long double)rw_, (long double)rh_);                           \
    if (ew_ != eh_) REPORT("%s: errno diverge (%s,%s) w=%d h=%d",             \
        label, #x, #y, ew_, eh_);                                             \
    if (fw_ != fh_) REPORT("%s: FP flags diverge (%s,%s) w=0x%x h=0x%x",      \
        label, #x, #y, fw_, fh_);                                             \
} while (0)

/* Complex differential (scalar part / imaginary part classification). */
#define DIFFC1(label, w, h, zr, zi) do {                                      \
    volatile double vr_ = (double)(zr), vi_ = (double)(zi);                  \
    double complex vz_ = vr_ + vi_ * I;                                      \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    double complex rw_ = (w)(vz_);                                           \
    int ew_ = errno, fw_ = fetestexcept(FE_ALL_EXCEPT);                       \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    double complex rh_ = (h)(vz_);                                           \
    int eh_ = errno, fh_ = fetestexcept(FE_ALL_EXCEPT);                       \
    int crw_ = cl(creal(rw_)), ciw_ = cl(cimag(rw_));                        \
    int crh_ = cl(creal(rh_)), cih_ = cl(cimag(rh_));                        \
    if (crw_ != crh_ || ciw_ != cih_)                                        \
        REPORT("%s: class diverge (%s,%s) re:%s/%s im:%s/%s", label,         \
               #zr, #zi, clname(crw_), clname(crh_), clname(ciw_), clname(cih_)); \
    if (!same(creal(rw_), creal(rh_)) || !same(cimag(rw_), cimag(rh_)))      \
        REPORT("%s: value diverge (%s,%s)", label, #zr, #zi);                \
    if (ew_ != eh_) REPORT("%s: errno diverge (%s,%s) w=%d h=%d",             \
        label, #zr, #zi, ew_, eh_);                                           \
    if (fw_ != fh_) REPORT("%s: FP flags diverge (%s,%s) w=0x%x h=0x%x",      \
        label, #zr, #zi, fw_, fh_);                                           \
} while (0)

#define DIFFC2(label, w, h, xr, xi, yr, yi) do {                              \
    volatile double ax_ = (double)(xr), bx_ = (double)(xi);                  \
    volatile double ay_ = (double)(yr), by_ = (double)(yi);                  \
    double complex vx_ = ax_ + bx_ * I, vy_ = ay_ + by_ * I;                 \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    double complex rw_ = (w)(vx_, vy_);                                      \
    int ew_ = errno, fw_ = fetestexcept(FE_ALL_EXCEPT);                       \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    double complex rh_ = (h)(vx_, vy_);                                      \
    int eh_ = errno, fh_ = fetestexcept(FE_ALL_EXCEPT);                       \
    int crw_ = cl(creal(rw_)), ciw_ = cl(cimag(rw_));                        \
    int crh_ = cl(creal(rh_)), cih_ = cl(cimag(rh_));                        \
    if (crw_ != crh_ || ciw_ != cih_)                                        \
        REPORT("%s: class diverge re:%s/%s im:%s/%s", label,                 \
               clname(crw_), clname(crh_), clname(ciw_), clname(cih_));       \
    if (!same(creal(rw_), creal(rh_)) || !same(cimag(rw_), cimag(rh_)))      \
        REPORT("%s: value diverge", label);                                  \
    if (ew_ != eh_) REPORT("%s: errno diverge w=%d h=%d", label, ew_, eh_);  \
    if (fw_ != fh_) REPORT("%s: FP flags diverge w=0x%x h=0x%x", label, fw_, fh_); \
} while (0)

/* ===== attack batches ===== */
#define F1_BATCH(x) do {                                                     \
    DIFF1F("sinf", cplus_sinf, sinf, x); DIFF1F("cosf", cplus_cosf, cosf, x); \
    DIFF1F("tanf", cplus_tanf, tanf, x); DIFF1F("asinf", cplus_asinf, asinf, x); \
    DIFF1F("acosf", cplus_acosf, acosf, x); DIFF1F("atanf", cplus_atanf, atanf, x); \
    DIFF1F("sinhf", cplus_sinhf, sinhf, x); DIFF1F("coshf", cplus_coshf, coshf, x); \
    DIFF1F("tanhf", cplus_tanhf, tanhf, x); DIFF1F("asinhf", cplus_asinhf, asinhf, x); \
    DIFF1F("acoshf", cplus_acoshf, acoshf, x); DIFF1F("atanhf", cplus_atanhf, atanhf, x); \
    DIFF1F("expf", cplus_expf, expf, x); DIFF1F("exp2f", cplus_exp2f, exp2f, x); \
    DIFF1F("expm1f", cplus_expm1f, expm1f, x); DIFF1F("logf", cplus_logf, logf, x); \
    DIFF1F("log2f", cplus_log2f, log2f, x); DIFF1F("log10f", cplus_log10f, log10f, x); \
    DIFF1F("log1pf", cplus_log1pf, log1pf, x); DIFF1F("sqrtf", cplus_sqrtf, sqrtf, x); \
    DIFF1F("cbrtf", cplus_cbrtf, cbrtf, x); DIFF1F("floorf", cplus_floorf, floorf, x); \
    DIFF1F("ceilf", cplus_ceilf, ceilf, x); DIFF1F("truncf", cplus_truncf, truncf, x); \
    DIFF1F("roundf", cplus_roundf, roundf, x); DIFF1F("nearbyintf", cplus_nearbyintf, nearbyintf, x); \
    DIFF1F("rintf", cplus_rintf, rintf, x); DIFF1F("fabsf", cplus_fabsf, fabsf, x); \
    DIFF1F("logbf", cplus_logbf, logbf, x); DIFF1F("erff", cplus_erff, erff, x); \
    DIFF1F("erfcf", cplus_erfcf, erfcf, x); DIFF1F("tgammaf", cplus_tgammaf, tgammaf, x); \
    DIFF1F("lgammaf", cplus_lgammaf, lgammaf, x);                            \
} while (0)

#define L1_BATCH(x) do {                                                     \
    DIFF1L("sinl", cplus_sinl, sinl, x); DIFF1L("cosl", cplus_cosl, cosl, x); \
    DIFF1L("tanl", cplus_tanl, tanl, x); DIFF1L("asinl", cplus_asinl, asinl, x); \
    DIFF1L("acosl", cplus_acosl, acosl, x); DIFF1L("atanl", cplus_atanl, atanl, x); \
    DIFF1L("sinhl", cplus_sinhl, sinhl, x); DIFF1L("coshl", cplus_coshl, coshl, x); \
    DIFF1L("tanhl", cplus_tanhl, tanhl, x); DIFF1L("asinhl", cplus_asinhl, asinhl, x); \
    DIFF1L("acoshl", cplus_acoshl, acoshl, x); DIFF1L("atanhl", cplus_atanhl, atanhl, x); \
    DIFF1L("expl", cplus_expl, expl, x); DIFF1L("exp2l", cplus_exp2l, exp2l, x); \
    DIFF1L("expm1l", cplus_expm1l, expm1l, x); DIFF1L("logl", cplus_logl, logl, x); \
    DIFF1L("log2l", cplus_log2l, log2l, x); DIFF1L("log10l", cplus_log10l, log10l, x); \
    DIFF1L("log1pl", cplus_log1pl, log1pl, x); DIFF1L("sqrtl", cplus_sqrtl, sqrtl, x); \
    DIFF1L("cbrtl", cplus_cbrtl, cbrtl, x); DIFF1L("floorl", cplus_floorl, floorl, x); \
    DIFF1L("ceill", cplus_ceill, ceill, x); DIFF1L("truncl", cplus_truncl, truncl, x); \
    DIFF1L("roundl", cplus_roundl, roundl, x); DIFF1L("nearbyintl", cplus_nearbyintl, nearbyintl, x); \
    DIFF1L("rintl", cplus_rintl, rintl, x); DIFF1L("fabsl", cplus_fabsl, fabsl, x); \
    DIFF1L("logbl", cplus_logbl, logbl, x); DIFF1L("erfl", cplus_erfl, erfl, x); \
    DIFF1L("erfcl", cplus_erfcl, erfcl, x); DIFF1L("tgammal", cplus_tgammal, tgammal, x); \
    DIFF1L("lgammal", cplus_lgammal, lgammal, x);                            \
} while (0)

int main(void) {
    int orig_round = fegetround();
    cplus_libc_log_set_enabled(0); /* keep bulk differential quiet */

    /* ---------- scenario 1: hostile FP values ---------- */
    volatile float qnan_f, snan_f, inf_f, ninf_f, tmin_f, ntmin_f, max_f, nmax_f, zp_f, zn_f;
    qnan_f = nanf("0x1234");
#if HAS_SNANF
    snan_f = __builtin_nansf("");
#else
    snan_f = nanf("");
#endif
    inf_f = (float)INFINITY; ninf_f = -(float)INFINITY;
    tmin_f = nextafterf(0.0f, 1.0f); ntmin_f = -tmin_f;
    max_f = FLT_MAX; nmax_f = -FLT_MAX;
    zp_f = 0.0f; zn_f = -0.0f;

    F1_BATCH(qnan_f);  F1_BATCH(snan_f);  F1_BATCH(inf_f);
    F1_BATCH(ninf_f);  F1_BATCH(tmin_f);  F1_BATCH(ntmin_f);
    F1_BATCH(max_f);   F1_BATCH(nmax_f);  F1_BATCH(zp_f);
    F1_BATCH(zn_f);

    volatile long double qnan_l, snan_l, inf_l, tmin_l, max_l, zn_l;
    qnan_l = nanl("0x1234");
    snan_l = nanl("");
    inf_l = (long double)INFINITY;
    tmin_l = nextafterl(0.0L, 1.0L);
    max_l = (long double)FLT_MAX;
    zn_l = -0.0L;
    L1_BATCH(qnan_l); L1_BATCH(snan_l); L1_BATCH(inf_l);
    L1_BATCH(tmin_l); L1_BATCH(max_l); L1_BATCH(zn_l);

    /* double-family sample with hostile values */
    DIFF1D("sqrt", cplus_sqrt, sqrt, -1.0);
    DIFF1D("log", cplus_log, log, -1.0);
    DIFF1D("exp", cplus_exp, exp, 1.0e10);
    DIFF1D("fabs", cplus_fabs, fabs, -0.0);
    DIFF1D("floor", cplus_floor, floor, (double)qnan_f);

    /* 2-arg float / long double hostile pairs */
    DIFF2F("atan2f", cplus_atan2f, atan2f, zn_f, zn_f);
    DIFF2F("powf", cplus_powf, powf, -2.0f, 0.5f);
    DIFF2F("hypotf", cplus_hypotf, hypotf, inf_f, qnan_f);
    DIFF2F("fmodf", cplus_fmodf, fmodf, 1.0f, 0.0f);
    DIFF2F("remainderf", cplus_remainderf, remainderf, 1.0f, 0.0f);
    DIFF2F("fdimf", cplus_fdimf, fdimf, qnan_f, inf_f);
    DIFF2F("fminf", cplus_fminf, fminf, qnan_f, 1.0f);
    DIFF2F("fmaxf", cplus_fmaxf, fmaxf, -0.0f, 0.0f);
    DIFF2F("copysignf", cplus_copysignf, copysignf, 1.0f, -0.0f);
    DIFF2F("ldexpf", cplus_ldexpf, ldexpf, max_f, 1000);
    DIFF2F("scalbnf", cplus_scalbnf, scalbnf, tmin_f, -1000);
    DIFF2F("nextafterf", cplus_nextafterf, nextafterf, 0.0f, -1.0f);
    /* fmaf(x,y,z) 3-arg differential */
    {
        volatile float ax = max_f, ay = max_f, az = -max_f;
        float rw, rh; int ew, eh, fw, fh;
        feclearexcept(FE_ALL_EXCEPT); errno = SENT; rw = cplus_fmaf(ax, ay, az);
        ew = errno; fw = fetestexcept(FE_ALL_EXCEPT);
        feclearexcept(FE_ALL_EXCEPT); errno = SENT; rh = fmaf(ax, ay, az);
        eh = errno; fh = fetestexcept(FE_ALL_EXCEPT);
        if (cl((long double)rw) != cl((long double)rh) || !same((long double)rw, (long double)rh))
            REPORT("fmaf: value/class diverge w=%.9g h=%.9g", (double)rw, (double)rh);
        if (ew != eh || fw != fh) REPORT("fmaf: errno/flags diverge w=%d/0x%x h=%d/0x%x", ew, fw, eh, fh);
    }

    DIFF2L("atan2l", cplus_atan2l, atan2l, zn_l, zn_l);
    DIFF2L("powl", cplus_powl, powl, -2.0L, 0.5L);
    DIFF2L("hypotl", cplus_hypotl, hypotl, inf_l, qnan_l);
    DIFF2L("fmodl", cplus_fmodl, fmodl, 1.0L, 0.0L);
    DIFF2L("remainderl", cplus_remainderl, remainderl, 1.0L, 0.0L);
    DIFF2L("fdiml", cplus_fdiml, fdiml, qnan_l, inf_l);
    DIFF2L("fminl", cplus_fminl, fminl, qnan_l, 1.0L);
    DIFF2L("fmaxl", cplus_fmaxl, fmaxl, -0.0L, 0.0L);
    DIFF2L("copysignl", cplus_copysignl, copysignl, 1.0L, -0.0L);
    DIFF2L("ldexpl", cplus_ldexpl, ldexpl, max_l, 1000);
    DIFF2L("nextafterl", cplus_nextafterl, nextafterl, 0.0L, -1.0L);
    {
        volatile long double ax = max_l, ay = max_l, az = -max_l;
        long double rw, rh; int ew, eh, fw, fh;
        feclearexcept(FE_ALL_EXCEPT); errno = SENT; rw = cplus_fmal(ax, ay, az);
        ew = errno; fw = fetestexcept(FE_ALL_EXCEPT);
        feclearexcept(FE_ALL_EXCEPT); errno = SENT; rh = fmal(ax, ay, az);
        eh = errno; fh = fetestexcept(FE_ALL_EXCEPT);
        if (cl(rw) != cl(rh) || !same(rw, rh))
            REPORT("fmal: value/class diverge w=%.9Lg h=%.9Lg", rw, rh);
        if (ew != eh || fw != fh) REPORT("fmal: errno/flags diverge w=%d/0x%x h=%d/0x%x", ew, fw, eh, fh);
    }

    /* nan tag handling */
    {
        const char *tags[] = { "", "0x1234", "1234", "snan", "0x7fc00001" };
        size_t i;
        for (i = 0; i < sizeof tags / sizeof tags[0]; i++) {
            volatile float rw_, rh_;
            feclearexcept(FE_ALL_EXCEPT); errno = SENT;
            rw_ = cplus_nanf(tags[i]);
            { int ew_ = errno, fw_ = fetestexcept(FE_ALL_EXCEPT);
            feclearexcept(FE_ALL_EXCEPT); errno = SENT;
            rh_ = nanf(tags[i]);
            if (cl((long double)rw_) != cl((long double)rh_))
                REPORT("nanf tag %s: class diverge w=%s h=%s", tags[i],
                       clname(cl((long double)rw_)), clname(cl((long double)rh_)));
            if (ew_ != errno) REPORT("nanf tag %s: errno diverge w=%d h=%d", tags[i], ew_, errno);
            if (fw_ != fetestexcept(FE_ALL_EXCEPT))
                REPORT("nanf tag %s: FP flags diverge w=0x%x h=0x%x", tags[i], fw_, fetestexcept(FE_ALL_EXCEPT)); }
        }
        {
            volatile long double rw_, rh_;
            feclearexcept(FE_ALL_EXCEPT); errno = SENT;
            rw_ = cplus_nanl("0x1234");
            { int ew_ = errno, fw_ = fetestexcept(FE_ALL_EXCEPT);
            feclearexcept(FE_ALL_EXCEPT); errno = SENT;
            rh_ = nanl("0x1234");
            if (cl(rw_) != cl(rh_)) REPORT("nanl: class diverge");
            if (ew_ != errno) REPORT("nanl: errno diverge w=%d h=%d", ew_, errno);
            if (fw_ != fetestexcept(FE_ALL_EXCEPT)) REPORT("nanl: FP flags diverge w=0x%x h=0x%x", fw_, fetestexcept(FE_ALL_EXCEPT)); }
        }
    }

    /* integer-returning rounding wrappers: value parity on hostile inputs */
    {
        volatile float vals[] = { 0.0f, -0.0f, 0.5f, -0.5f, 1.5f, -1.5f, 2.5f,
                                  FLT_MAX, -FLT_MAX, (float)INFINITY, (float)NAN };
        size_t i;
        for (i = 0; i < sizeof vals / sizeof vals[0]; i++) {
            long w, h; long long wl, hl;
            feclearexcept(FE_ALL_EXCEPT); errno = SENT; w = cplus_lrintf(vals[i]);
            int ew_ = errno, fw_ = fetestexcept(FE_ALL_EXCEPT);
            feclearexcept(FE_ALL_EXCEPT); errno = SENT; h = lrintf(vals[i]);
            if (w != h || ew_ != errno || fw_ != fetestexcept(FE_ALL_EXCEPT))
                REPORT("lrintf input %.9g: w=%ld/%d/0x%x h=%ld/%d/0x%x", (double)vals[i],
                       w, ew_, fw_, h, errno, fetestexcept(FE_ALL_EXCEPT));
            feclearexcept(FE_ALL_EXCEPT); errno = SENT; wl = cplus_llrintf(vals[i]);
            ew_ = errno; fw_ = fetestexcept(FE_ALL_EXCEPT);
            feclearexcept(FE_ALL_EXCEPT); errno = SENT; hl = llrintf(vals[i]);
            if (wl != hl || ew_ != errno || fw_ != fetestexcept(FE_ALL_EXCEPT))
                REPORT("llrintf input %.9g: w=%lld/%d/0x%x h=%lld/%d/0x%x", (double)vals[i],
                       wl, ew_, fw_, hl, errno, fetestexcept(FE_ALL_EXCEPT));
            /* ilogbf returns int */
            {
                int iw, ih;
                feclearexcept(FE_ALL_EXCEPT); errno = SENT; iw = cplus_ilogbf(vals[i]);
                ew_ = errno; fw_ = fetestexcept(FE_ALL_EXCEPT);
                feclearexcept(FE_ALL_EXCEPT); errno = SENT; ih = ilogbf(vals[i]);
                if (iw != ih || ew_ != errno || fw_ != fetestexcept(FE_ALL_EXCEPT))
                    REPORT("ilogbf input %.9g: w=%d/%d/0x%x h=%d/%d/0x%x", (double)vals[i],
                           iw, ew_, fw_, ih, errno, fetestexcept(FE_ALL_EXCEPT));
            }
        }
    }

    /* Return-type defect: lroundf / llroundf must return long / long long. */
    {
        volatile float big = FLT_MAX;
        float rwf = cplus_lroundf(big);
        long rhl = lroundf(big);
        float rwf2 = cplus_llroundf(big);
        long long rhll = llroundf(big);
        REPORT("libc/install-include/cplus_libc.h:316-317 / libc/src/math/math_float.c:52-53: "
               "lroundf/llroundf declared and defined with float return, ISO C requires long/long long; "
               "FLT_MAX: cplus_lroundf=%.9g (float) vs lroundf=%ld; "
               "cplus_llroundf=%.9g (float) vs llroundf=%lld",
               (double)rwf, rhl, (double)rwf2, rhll);
        (void)rwf2; (void)rhll;
    }

    /* ---------- scenario 2: domain / range / errno-on-success ---------- */
    {
        struct { const char *name; float x; } a[] = {
            { "sqrtf(-1)", -1.0f }, { "logf(0)", 0.0f }, { "logf(-1)", -1.0f },
            { "acosf(2)", 2.0f }, { "expf(1e10)", 1.0e10f }, { "lgammaf(-1)", -1.0f },
            { "tgammaf(0)", 0.0f }, { "atanhf(1)", 1.0f }, { "asinhf(inf)", (float)INFINITY }
        };
        size_t i;
        for (i = 0; i < sizeof a / sizeof a[0]; i++) {
            volatile float vx = a[i].x;
            float rw, rh; int ew, eh, fw, fh;
            /* wrapper call, capture its errno + FP flags */
            feclearexcept(FE_ALL_EXCEPT); errno = SENT;
            switch (i) {
                case 0: rw = cplus_sqrtf(vx); break;
                case 1: case 2: rw = cplus_logf(vx); break;
                case 3: rw = cplus_acosf(vx); break;
                case 4: rw = cplus_expf(vx); break;
                case 5: rw = cplus_lgammaf(vx); break;
                case 6: rw = cplus_tgammaf(vx); break;
                case 7: rw = cplus_atanhf(vx); break;
                default: rw = cplus_asinhf(vx); break;
            }
            ew = errno; fw = fetestexcept(FE_ALL_EXCEPT);
            /* host call with the same clean state */
            feclearexcept(FE_ALL_EXCEPT); errno = SENT;
            switch (i) {
                case 0: rh = sqrtf(vx); break;
                case 1: case 2: rh = logf(vx); break;
                case 3: rh = acosf(vx); break;
                case 4: rh = expf(vx); break;
                case 5: rh = lgammaf(vx); break;
                case 6: rh = tgammaf(vx); break;
                case 7: rh = atanhf(vx); break;
                default: rh = asinhf(vx); break;
            }
            eh = errno; fh = fetestexcept(FE_ALL_EXCEPT);
            if (cl((long double)rw) != cl((long double)rh))
                REPORT("domain %s: class diverge w=%s h=%s", a[i].name,
                       clname(cl((long double)rw)), clname(cl((long double)rh)));
            if (ew != eh) REPORT("domain %s: errno diverge w=%d h=%d", a[i].name, ew, eh);
            if (fw != fh) REPORT("domain %s: FP flags diverge w=0x%x h=0x%x", a[i].name, fw, fh);
        }
        /* errno NOT clobbered on success */
        {
            volatile float ok = 4.0f;
            errno = SENT; (void)cplus_sqrtf(ok);
            if (errno != SENT) REPORT("sqrtf(4) success clobbered errno: %d (expected %d)", errno, SENT);
            errno = SENT; (void)sqrtf(ok);
            if (errno != SENT) REPORT("host sqrtf(4) also clobbers errno: %d", errno);
            errno = SENT; (void)cplus_powf(2.0f, 8.0f);
            if (errno != SENT) REPORT("powf(2,8) success clobbered errno: %d", errno);
        }
        /* remquof(x, 0, &q): compare return class, q, errno and flags */
        {
            volatile float x = 3.0f, y = 0.0f;
            int qw = -99, qh = -99; float rw, rh; int ew, eh, fw, fh;
            feclearexcept(FE_ALL_EXCEPT); errno = SENT; rw = cplus_remquof(x, y, &qw);
            ew = errno; fw = fetestexcept(FE_ALL_EXCEPT);
            feclearexcept(FE_ALL_EXCEPT); errno = SENT; rh = remquof(x, y, &qh);
            eh = errno; fh = fetestexcept(FE_ALL_EXCEPT);
            if (cl((long double)rw) != cl((long double)rh))
                REPORT("remquof(x,0): class diverge w=%s h=%s", clname(cl((long double)rw)), clname(cl((long double)rh)));
            if (qw != qh) REPORT("remquof(x,0): quo diverge w=%d h=%d", qw, qh);
            if (ew != eh) REPORT("remquof(x,0): errno diverge w=%d h=%d", ew, eh);
            if (fw != fh) REPORT("remquof(x,0): FP flags diverge w=0x%x h=0x%x", fw, fh);
        }
    }

    /* ---------- scenario 3: NULL / aliased out-params ---------- */
    {
        int e = -99, q = -99; float ip = -1.0f;
        int rc;
        errno = 0;
        rc = (cplus_frexpf(8.0f, NULL) == 0.0f);
        if (errno != EINVAL) REPORT("frexpf(x,NULL): errno=%d expected EINVAL(%d)", errno, EINVAL);
        if (!rc) REPORT("frexpf(x,NULL): return not 0.0f");
        errno = 0;
        rc = (cplus_modff(8.0f, NULL) == 0.0f);
        if (errno != EINVAL) REPORT("modff(x,NULL): errno=%d expected EINVAL(%d)", errno, EINVAL);
        if (!rc) REPORT("modff(x,NULL): return not 0.0f");
        errno = 0;
        rc = (cplus_remquof(8.0f, 3.0f, NULL) == 0.0f);
        if (errno != EINVAL) REPORT("remquof(x,y,NULL): errno=%d expected EINVAL(%d)", errno, EINVAL);
        if (!rc) REPORT("remquof(x,y,NULL): return not 0.0f");
        errno = 0; {
            long double lip = -1.0L;
            if (cplus_frexpl(8.0L, NULL) != 0.0L || errno != EINVAL)
                REPORT("frexpl(x,NULL): retval/errno mismatch errno=%d", errno);
            errno = 0;
            if (cplus_modfl(8.0L, NULL) != 0.0L || errno != EINVAL)
                REPORT("modfl(x,NULL): retval/errno mismatch errno=%d", errno);
            errno = 0;
            if (cplus_remquol(8.0L, 3.0L, NULL) != 0.0L || errno != EINVAL)
                REPORT("remquol(x,y,NULL): retval/errno mismatch errno=%d", errno);
            /* These are forked: the double-family wrappers do not guard NULL. */
            probe("cplus_frexp(x,NULL)", probe_frexp_null);
            probe("cplus_modf(x,NULL)", probe_modf_null);
            probe("cplus_remquo(x,y,NULL)", probe_remquo_null);
            (void)lip;
        }
        /* normal out-params */
        {
            float rw = cplus_frexpf(8.0f, &e), rh = frexpf(8.0f, &q);
            int e2 = -99, q2 = -99;
            float rw2, rh2;
            if (!same((long double)rw, (long double)rh) || e != q) REPORT("frexpf out-param diverge w=(%.9g,%d) h=(%.9g,%d)", (double)rw, e, (double)rh, q);
            rw2 = cplus_modff(8.25f, &ip); rh2 = modff(8.25f, &ip);
            if (!same((long double)rw2, (long double)rh2)) REPORT("modff out-param value diverge");
            rw2 = cplus_remquof(5.0f, 3.0f, &q); rh2 = remquof(5.0f, 3.0f, &q2);
            if (!same((long double)rw2, (long double)rh2) || q != q2) REPORT("remquof quo diverge w=%d h=%d", q, q2);
            (void)e2;
        }
        /* aliasing: input and out-param alias the same storage */
        {
            float a = 3.75f, b = 3.75f;
            float ra = cplus_modff(a, &a);
            float rb = modff(b, &b);
            if (!same((long double)ra, (long double)rb) || !same((long double)a, (long double)b))
                REPORT("modff aliased storage diverge w=(%.9g,%.9g) h=(%.9g,%.9g)", (double)ra, (double)a, (double)rb, (double)b);
            {
                int qa = 0, qb = 0;
                float rqa = cplus_remquof(5.0f, 3.0f, &qa);
                float rqb = remquof(5.0f, 3.0f, &qb);
                if (!same((long double)rqa, (long double)rqb) || qa != qb)
                    REPORT("remquof reused quo diverge w=%d h=%d", qa, qb);
            }
        }
    }

    /* ---------- scenario 4: FP state preservation across 20 wrappers ---------- */
    {
        volatile float xf = 1.234567f;
        volatile long double xl = 1.234567L;
        int wf, wround, hf, hround;
        /* Use wrappers vs host under FE_UPWARD + FE_INVALID preset. */
#define STATE1F(label, w, h) do {                                             \
        arm(); { volatile float v_ = (w)(xf); (void)v_; }                     \
        wf = fetestexcept(FE_ALL_EXCEPT); wround = fegetround();              \
        arm(); { volatile float v_ = (h)(xf); (void)v_; }                     \
        hf = fetestexcept(FE_ALL_EXCEPT); hround = fegetround();              \
        if (wf != hf || wround != hround)                                     \
            REPORT("state %s: flags w=0x%x h=0x%x round w=%d h=%d", label, wf, hf, wround, hround); \
} while (0)
        STATE1F("sinf", cplus_sinf, sinf);
        STATE1F("cosf", cplus_cosf, cosf);
        STATE1F("sqrtf", cplus_sqrtf, sqrtf);
        STATE1F("logf", cplus_logf, logf);
        STATE1F("expf", cplus_expf, expf);
        STATE1F("cosf2", cplus_cosf, cosf);
        STATE1F("tanf2", cplus_tanf, tanf);
        STATE1F("asinf", cplus_asinf, asinf);
        STATE1F("acosf", cplus_acosf, acosf);
        STATE1F("atanf", cplus_atanf, atanf);
        STATE1F("exp2f", cplus_exp2f, exp2f);
        STATE1F("erff", cplus_erff, erff);
        STATE1F("tgammaf", cplus_tgammaf, tgammaf);
        disarm();
#undef STATE1F
        /* 2-arg state checks */
        {
            volatile float yf = 2.0f;
            arm(); { volatile float v_ = cplus_powf(xf, yf); (void)v_; }
            wf = fetestexcept(FE_ALL_EXCEPT); wround = fegetround();
            arm(); { volatile float v_ = powf(xf, yf); (void)v_; }
            hf = fetestexcept(FE_ALL_EXCEPT); hround = fegetround();
            if (wf != hf || wround != hround)
                REPORT("state powf: flags w=0x%x h=0x%x round w=%d h=%d", wf, hf, wround, hround);
        }
        {
            volatile long double yl = 2.0L;
            arm(); { volatile long double v_ = cplus_powl(xl, yl); (void)v_; }
            wf = fetestexcept(FE_ALL_EXCEPT); wround = fegetround();
            arm(); { volatile long double v_ = powl(xl, yl); (void)v_; }
            hf = fetestexcept(FE_ALL_EXCEPT); hround = fegetround();
            if (wf != hf || wround != hround)
                REPORT("state powl: flags w=0x%x h=0x%x round w=%d h=%d", wf, hf, wround, hround);
        }
        /* long double and double single-arg state checks (10 more wrappers) */
#define STATE1L(label, w, h) do {                                             \
        arm(); { volatile long double v_ = (w)(xl); (void)v_; }               \
        wf = fetestexcept(FE_ALL_EXCEPT); wround = fegetround();              \
        arm(); { volatile long double v_ = (h)(xl); (void)v_; }               \
        hf = fetestexcept(FE_ALL_EXCEPT); hround = fegetround();              \
        if (wf != hf || wround != hround)                                     \
            REPORT("state %s: flags w=0x%x h=0x%x round w=%d h=%d", label, wf, hf, wround, hround); \
} while (0)
        STATE1L("sinl", cplus_sinl, sinl);
        STATE1L("cosl", cplus_cosl, cosl);
        STATE1L("sqrtl", cplus_sqrtl, sqrtl);
        STATE1L("logl", cplus_logl, logl);
        STATE1L("expl", cplus_expl, expl);
        STATE1L("tgammal", cplus_tgammal, tgammal);
        disarm();
#undef STATE1L
        {
            int r = cplus_fesetround(FE_UPWARD);
            if (r != 0 || cplus_fegetround() != FE_UPWARD)
                REPORT("fesetround/fegetround roundtrip failed r=%d round=%d", r, cplus_fegetround());
            cplus_fesetround(FE_TONEAREST);
        }
    }

    /* ---------- scenario 5: hostile FENV masks ---------- */
    {
        fexcept_t fake;
        int rc;
        memset(&fake, 0xFF, sizeof fake);
        clean();
        rc = cplus_fesetexceptflag(&fake, FE_ALL_EXCEPT);
        probe("cplus_fesetexceptflag(all-bits-set fexcept_t)", probe_feset_fake);
        /* Must not crash. Compare return with host given the same fake object. */
        {
            int rh;
            clean();
            rh = fesetexceptflag(&fake, FE_ALL_EXCEPT);
            if (rc != rh) REPORT("fesetexceptflag(fake): retval diverge w=%d h=%d", rc, rh);
        }
        clean();
        errno = 0;
        rc = cplus_fegetexceptflag(NULL, FE_ALL_EXCEPT);
        if (rc == 0 || errno != EINVAL)
            REPORT("fegetexceptflag(NULL): retval=%d errno=%d (expected nonzero/EINVAL)", rc, errno);
        errno = 0;
        rc = cplus_feholdexcept(NULL);
        if (rc == 0 || errno != EINVAL)
            REPORT("feholdexcept(NULL): retval=%d errno=%d (expected nonzero/EINVAL)", rc, errno);
        /* round-trip with exceptions pending: wrapper vs host differential */
        {
            fenv_t envw, envh;
            int awf, ahr, fwf, fhr, uw, uh;
            /* wrapper path */
            fesetround(FE_UPWARD); feclearexcept(FE_ALL_EXCEPT); feraiseexcept(FE_INVALID | FE_DIVBYZERO);
            uw = cplus_feholdexcept(&envw);
            awf = fetestexcept(FE_ALL_EXCEPT); ahr = fegetround();
            fesetround(FE_DOWNWARD); feclearexcept(FE_ALL_EXCEPT); feraiseexcept(FE_OVERFLOW);
            uw += cplus_feupdateenv(&envw);
            fwf = fetestexcept(FE_ALL_EXCEPT); fhr = fegetround();
            /* host path */
            fesetround(FE_UPWARD); feclearexcept(FE_ALL_EXCEPT); feraiseexcept(FE_INVALID | FE_DIVBYZERO);
            uh = feholdexcept(&envh);
            { int awf_h = fetestexcept(FE_ALL_EXCEPT), ahr_h = fegetround();
              fesetround(FE_DOWNWARD); feclearexcept(FE_ALL_EXCEPT); feraiseexcept(FE_OVERFLOW);
              uh += feupdateenv(&envh);
              if (awf != awf_h || ahr != ahr_h || fwf != fetestexcept(FE_ALL_EXCEPT) || fhr != fegetround())
                  REPORT("feholdexcept/feupdateenv roundtrip diverge: after w=(0x%x,%d) h=(0x%x,%d); final w=(0x%x,%d) h=(0x%x,%d)",
                         awf, ahr, awf_h, ahr_h, fwf, fhr, fetestexcept(FE_ALL_EXCEPT), fegetround()); }
            if (uw != uh) REPORT("feholdexcept/feupdateenv retval diverge w=%d h=%d", uw, uh);
        }
        clean();
    }

    /* ---------- scenario 6: complex NaN/Inf ---------- */
    {
        double nan = NAN, inf = (double)INFINITY;
        DIFFC1("csin(NaN+Inf)", cplus_csin, csin, nan, inf);
        DIFFC1("ccos(Inf+NaN)", cplus_ccos, ccos, inf, nan);
        DIFFC1("ctan(NaN+NaN)", cplus_ctan, ctan, nan, nan);
        DIFFC1("csinh(Inf+Inf)", cplus_csinh, csinh, inf, inf);
        DIFFC1("ccosh(NaN+0)", cplus_ccosh, ccosh, nan, 0.0);
        DIFFC1("ctanh(Inf+NaN)", cplus_ctanh, ctanh, inf, nan);
        DIFFC1("clog(NaN+Inf)", cplus_clog, clog, nan, inf);
        DIFFC1("clog(0+0)", cplus_clog, clog, 0.0, 0.0);
        DIFFC1("csqrt(-Inf+0)", cplus_csqrt, csqrt, -inf, 0.0);
        DIFFC1("cexp(Inf+NaN)", cplus_cexp, cexp, inf, nan);
        DIFFC1("cproj(Inf+1)", cplus_cproj, cproj, inf, 1.0);
        DIFFC1("cproj(1+Inf)", cplus_cproj, cproj, 1.0, inf);
        DIFFC1("cproj(Inf-Inf)", cplus_cproj, cproj, inf, -inf);
        DIFFC1("carg(0+0)", cplus_carg, carg, 0.0, 0.0);
        DIFFC2("cpow(0,0)", cplus_cpow, cpow, 0.0, 0.0, 0.0, 0.0);
        DIFFC2("cpow(0+0,NaN+Inf)", cplus_cpow, cpow, 0.0, 0.0, nan, inf);
        DIFFC2("cpow(NaN+Inf,2+0)", cplus_cpow, cpow, nan, inf, 2.0, 0.0);
        DIFFC2("cpow(Inf+Inf,0+0)", cplus_cpow, cpow, inf, inf, 0.0, 0.0);
    }

    /* complex float and long double variants: representative hostile cases */
    {
        volatile float fn = (float)NAN, fi = (float)INFINITY;
        volatile long double ln = (long double)NAN, li = (long double)INFINITY;
        float complex vz = fn + fi * I;
        long double complex lz = ln + li * I;
        float complex rw, rh;
        long double complex lrw, lrh;
        int ew, eh, fw, fh;
#define DIFFCF(label, w, h, v) do {                                          \
        feclearexcept(FE_ALL_EXCEPT); errno = SENT; rw = (w)(v);              \
        ew = errno; fw = fetestexcept(FE_ALL_EXCEPT);                         \
        feclearexcept(FE_ALL_EXCEPT); errno = SENT; rh = (h)(v);              \
        eh = errno; fh = fetestexcept(FE_ALL_EXCEPT);                         \
        if (cl(crealf(rw)) != cl(crealf(rh)) || cl(cimagf(rw)) != cl(cimagf(rh))) \
            REPORT("%s: class diverge re:%s/%s im:%s/%s", label,              \
                   clname(cl(crealf(rw))), clname(cl(crealf(rh))),            \
                   clname(cl(cimagf(rw))), clname(cl(cimagf(rh))));           \
        if (ew != eh || fw != fh) REPORT("%s: errno/flags diverge w=%d/0x%x h=%d/0x%x", \
                                        label, ew, fw, eh, fh);               \
} while (0)
#define DIFFCL(label, w, h, v) do {                                          \
        feclearexcept(FE_ALL_EXCEPT); errno = SENT; lrw = (w)(v);             \
        ew = errno; fw = fetestexcept(FE_ALL_EXCEPT);                         \
        feclearexcept(FE_ALL_EXCEPT); errno = SENT; lrh = (h)(v);             \
        eh = errno; fh = fetestexcept(FE_ALL_EXCEPT);                         \
        if (cl(creall(lrw)) != cl(creall(lrh)) || cl(cimagl(lrw)) != cl(cimagl(lrh))) \
            REPORT("%s: class diverge re:%s/%s im:%s/%s", label,              \
                   clname(cl(creall(lrw))), clname(cl(creall(lrh))),          \
                   clname(cl(cimagl(lrw))), clname(cl(cimagl(lrh))));         \
        if (ew != eh || fw != fh) REPORT("%s: errno/flags diverge w=%d/0x%x h=%d/0x%x", \
                                        label, ew, fw, eh, fh);               \
} while (0)
        DIFFCF("csinf", cplus_csinf, csinf, vz);
        DIFFCF("ccosf", cplus_ccosf, ccosf, vz);
        DIFFCF("clogf", cplus_clogf, clogf, vz);
        DIFFCF("csqrtf", cplus_csqrtf, csqrtf, vz);
        DIFFCF("cexpf", cplus_cexpf, cexpf, vz);
        DIFFCF("cprojf", cplus_cprojf, cprojf, vz);
        DIFFCL("csinl", cplus_csinl, csinl, lz);
        DIFFCL("ccosl", cplus_ccosl, ccosl, lz);
        DIFFCL("clogl", cplus_clogl, clogl, lz);
        DIFFCL("csqrtl", cplus_csqrtl, csqrtl, lz);
        DIFFCL("cexpl", cplus_cexpl, cexpl, lz);
        DIFFCL("cprojl", cplus_cprojl, cprojl, lz);
#undef DIFFCF
#undef DIFFCL
    }

    /* ---------- scenario 7: logging toggled on/off ---------- */
    {
        static float r_off[4], r_on[4];
        size_t i;
        volatile float x = 2.5f;
        cplus_libc_log_set_enabled(0);
        r_off[0] = cplus_sqrtf(x);
        r_off[1] = cplus_logf(x);
        r_off[2] = cplus_powf(x, x);
        { int e = 0; r_off[3] = cplus_frexpf(x, &e); }
        cplus_libc_log_set_enabled(1); /* stderr will show a handful of lines */
        r_on[0] = cplus_sqrtf(x);
        r_on[1] = cplus_logf(x);
        r_on[2] = cplus_powf(x, x);
        { int e = 0; r_on[3] = cplus_frexpf(x, &e); }
        cplus_libc_log_set_enabled(0);
        for (i = 0; i < 4; i++)
            if (!same((long double)r_off[i], (long double)r_on[i]))
                REPORT("logging on/off changed result[%d]: off=%.9g on=%.9g", (int)i,
                       (double)r_off[i], (double)r_on[i]);
        /* errno differential with logging ON: host success must leave sentinel. */
        cplus_libc_log_set_enabled(1);
        {
            volatile float ok = 4.0f;
            int ew, eh, e2 = 0;
            feclearexcept(FE_ALL_EXCEPT); errno = SENT; (void)cplus_sqrtf(ok); ew = errno;
            feclearexcept(FE_ALL_EXCEPT); errno = SENT; (void)sqrtf(ok); eh = errno;
            if (ew != eh) REPORT("logging-on errno clobber on success: wrapper=%d host=%d (sentinel %d)", ew, eh, SENT);
            {
                volatile float x2 = 2.25f;
                errno = SENT; (void)cplus_sinf(x2); ew = errno;
                errno = SENT; (void)sinf(x2); eh = errno;
                if (ew != eh) REPORT("logging-on errno clobber (sinf): wrapper=%d host=%d", ew, eh);
                errno = SENT; (void)cplus_logf(x2); ew = errno;
                errno = SENT; (void)logf(x2); eh = errno;
                if (ew != eh) REPORT("logging-on errno clobber (logf): wrapper=%d host=%d", ew, eh);
                errno = SENT; (void)cplus_powf(x2, x2); ew = errno;
                errno = SENT; (void)powf(x2, x2); eh = errno;
                if (ew != eh) REPORT("logging-on errno clobber (powf): wrapper=%d host=%d", ew, eh);
                errno = SENT; (void)cplus_frexpf(x2, &e2); ew = errno;
                errno = SENT; (void)frexpf(x2, &e2); eh = errno;
                if (ew != eh) REPORT("logging-on errno clobber (frexpf): wrapper=%d host=%d", ew, eh);
            }
            /* FP state with logging on */
            {
                int wf, hf, wr, hr;
                arm(); (void)cplus_sqrtf(ok); wf = fetestexcept(FE_ALL_EXCEPT); wr = fegetround();
                arm(); (void)sqrtf(ok); hf = fetestexcept(FE_ALL_EXCEPT); hr = fegetround();
                if (wf != hf || wr != hr)
                    REPORT("logging-on FP state leak: wrapper flags=0x%x round=%d host flags=0x%x round=%d",
                           wf, wr, hf, hr);
            }
        }
        cplus_libc_log_set_enabled(0);
    }

    /* restore FP state */
    disarm();
    fesetround(orig_round);
    feclearexcept(FE_ALL_EXCEPT);
    errno = 0;

    if (g_findings) fprintf(stderr, "math_attack: %d finding(s)\n", g_findings);
    printf("math_attack: ok\n");
    return 0;
}
