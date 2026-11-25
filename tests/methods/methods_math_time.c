/* Per-method unit + logging tests for the C+ hardened libc math, time,
 * locale and fenv wrappers.
 *
 * Standalone C99.  Each method is m_mt_<n>_<slug> and is executed twice by a
 * generic runner: once with C+ logging OFF (authoritative: checks are counted
 * and FINDING lines are printed) and once with logging ON (silent).  A per-run
 * signature of every check outcome plus the low bytes of every numeric result,
 * errno and FP-flag word is compared between the two runs, so any behaviour
 * change caused purely by enabling logging is reported as a FINDING.
 *
 * Every numeric scenario is also differentially compared against the host libm
 * function of the same name (result bits / value class, errno, and raised FP
 * exception flags).  The FP environment is saved/restored around every method
 * and every scenario.  No genuine undefined behaviour is relied upon: NULL
 * out-parameters are only passed to wrappers that explicitly guard them.
 *
 * Build (from repo root):
 *   clang -std=c99 -O1 -Wall -Wextra -pedantic -I. \
 *     -fsanitize=undefined -fno-sanitize-recover=all \
 *     -fno-sanitize=float-divide-by-zero \
 *     tests/methods/methods_math_time.c \
 *     build/libc/libcplus_hardened.a -lm -pthread -o mmt
 */

#if defined(__has_include)
#  if __has_include("libc/install-include/cplus_libc.h")
#    include "libc/install-include/cplus_libc.h"
#  else
#    include "cplus_libc.h"
#  endif
#else
#  include "libc/install-include/cplus_libc.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <locale.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ------------------------------------------------------------------ utils */

static int  g_checks   = 0;
static int  g_fails    = 0;
static int  g_silent   = 0;              /* parity (logging ON) run            */
static int  g_sig_len  = 0;
static char g_sig[16384];
static char g_msg[1024];

/* Record one outcome.  In the authoritative run this counts + prints; in the
 * silent parity run it only contributes to the signature. */
static void ok(int cond, const char *what) {
    if (g_sig_len < (int)sizeof g_sig - 1)
        g_sig[g_sig_len++] = cond ? '1' : '0';
    if (!g_silent) {
        ++g_checks;
        if (!cond) { ++g_fails; printf("FINDING: %s\n", what); }
    }
}

static void okf(int cond, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_msg, sizeof g_msg, fmt, ap);
    va_end(ap);
    ok(cond, g_msg);
}

/* Append a raw byte to the signature (used for bit-level result parity). */
static void sig_byte(unsigned v) {
    if (g_sig_len < (int)sizeof g_sig - 1)
        g_sig[g_sig_len++] = (char)(v & 0xffu);
}
static void sig_u64(unsigned long long v) {
    int i;
    for (i = 0; i < 8; ++i) sig_byte((unsigned)(v >> (i * 8)));
}

static unsigned long long bits_d(double v) {
    unsigned long long u; memcpy(&u, &v, sizeof u); return u;
}
static unsigned long long bits_f(float v) {
    unsigned long long u = 0; memcpy(&u, &v, sizeof v); return u;
}
static unsigned long long bits_l(long double v) {
    unsigned long long u = 0;
    memcpy(&u, &v, (sizeof v < sizeof u) ? sizeof v : sizeof u);
    return u;
}

/* NaN-aware 'same value' comparison, with signed-zero discrimination. */
static int same_d(double a, double b) {
    if (isnan(a) || isnan(b)) return isnan(a) && isnan(b);
    if (a == b) return signbit(a) == signbit(b);
    return 0;
}
static int same_f(float a, float b) {
    if (isnan(a) || isnan(b)) return isnan(a) && isnan(b);
    if (a == b) return signbit(a) == signbit(b);
    return 0;
}
static int same_l(long double a, long double b) {
    if (isnan(a) || isnan(b)) return isnan(a) && isnan(b);
    if (a == b) return signbit(a) == signbit(b);
    return 0;
}
static int same_cd(double complex a, double complex b) {
    return same_d(creal(a), creal(b)) && same_d(cimag(a), cimag(b));
}

/* ------------------------------------------------------------- quiet stderr */

static int g_saved_stderr = -1;
static void quiet_begin(void) {
    int fd;
    g_saved_stderr = dup(STDERR_FILENO);
    fd = open("/dev/null", O_WRONLY);
    if (fd >= 0) { (void)dup2(fd, STDERR_FILENO); (void)close(fd); }
}
static void quiet_end(void) {
    if (g_saved_stderr >= 0) {
        (void)dup2(g_saved_stderr, STDERR_FILENO);
        (void)close(g_saved_stderr);
        g_saved_stderr = -1;
    }
}

/* --------------------------------------------------------- differential fns */

typedef double (*fn_d1)(double);
typedef double (*fn_d2)(double, double);
typedef double (*fn_d3)(double, double, double);
typedef float  (*fn_f1)(float);
typedef float  (*fn_f2)(float, float);
typedef float  (*fn_f3)(float, float, float);
typedef long double (*fn_l1)(long double);
typedef long double (*fn_l2)(long double, long double);
typedef long double (*fn_l3)(long double, long double, long double);
typedef double complex (*fn_c1)(double complex);
typedef double complex (*fn_c2)(double complex, double complex);

#define FP_PREAMBLE do { errno = 0; feclearexcept(FE_ALL_EXCEPT); } while (0)

static void chk_d1(const char *nm, fn_d1 fn, fn_d1 host, double x) {
    fenv_t env; fegetenv(&env);
    double ro, rh; int eo, eh, fo, fh;
    FP_PREAMBLE; ro = fn(x);  eo = errno; fo = fetestexcept(FE_ALL_EXCEPT);
    sig_u64(bits_d(ro)); sig_byte((unsigned)fo); sig_byte((unsigned)eo);
    FP_PREAMBLE; rh = host(x); eh = errno; fh = fetestexcept(FE_ALL_EXCEPT);
    okf(same_d(ro, rh), "%s(%g) value differs from host", nm, x);
    okf(bits_d(ro) == bits_d(rh) || (isnan(ro) && isnan(rh)),
        "%s(%g) bits differ from host", nm, x);
    okf(fo == fh, "%s(%g) fp-flags cplus=%d host=%d", nm, x, fo, fh);
    okf(eo == eh, "%s(%g) errno cplus=%d host=%d", nm, x, eo, eh);
    okf(fegetround() == FE_TONEAREST, "%s(%g) rounding mode changed", nm, x);
    fesetenv(&env);
}
static void chk_d2(const char *nm, fn_d2 fn, fn_d2 host, double x, double y) {
    fenv_t env; fegetenv(&env);
    double ro, rh; int eo, eh, fo, fh;
    FP_PREAMBLE; ro = fn(x, y);  eo = errno; fo = fetestexcept(FE_ALL_EXCEPT);
    sig_u64(bits_d(ro)); sig_byte((unsigned)fo); sig_byte((unsigned)eo);
    FP_PREAMBLE; rh = host(x, y); eh = errno; fh = fetestexcept(FE_ALL_EXCEPT);
    okf(same_d(ro, rh), "%s(%g,%g) value differs from host", nm, x, y);
    okf(bits_d(ro) == bits_d(rh) || (isnan(ro) && isnan(rh)),
        "%s(%g,%g) bits differ from host", nm, x, y);
    okf(fo == fh, "%s(%g,%g) fp-flags cplus=%d host=%d", nm, x, y, fo, fh);
    okf(eo == eh, "%s(%g,%g) errno cplus=%d host=%d", nm, x, y, eo, eh);
    okf(fegetround() == FE_TONEAREST, "%s(%g,%g) rounding mode changed", nm, x, y);
    fesetenv(&env);
}
static void chk_d3(const char *nm, fn_d3 fn, fn_d3 host,
                   double x, double y, double z) {
    fenv_t env; fegetenv(&env);
    double ro, rh; int eo, eh, fo, fh;
    FP_PREAMBLE; ro = fn(x, y, z);  eo = errno; fo = fetestexcept(FE_ALL_EXCEPT);
    sig_u64(bits_d(ro)); sig_byte((unsigned)fo); sig_byte((unsigned)eo);
    FP_PREAMBLE; rh = host(x, y, z); eh = errno; fh = fetestexcept(FE_ALL_EXCEPT);
    okf(same_d(ro, rh), "%s(%g,%g,%g) value differs from host", nm, x, y, z);
    okf(fo == fh, "%s(%g,%g,%g) fp-flags cplus=%d host=%d", nm, x, y, z, fo, fh);
    okf(eo == eh, "%s(%g,%g,%g) errno cplus=%d host=%d", nm, x, y, z, eo, eh);
    okf(fegetround() == FE_TONEAREST, "%s(...) rounding mode changed", nm);
    fesetenv(&env);
}

static void chk_f1(const char *nm, fn_f1 fn, fn_f1 host, float x) {
    fenv_t env; fegetenv(&env);
    float ro, rh; int eo, eh, fo, fh;
    FP_PREAMBLE; ro = fn(x);  eo = errno; fo = fetestexcept(FE_ALL_EXCEPT);
    sig_u64(bits_f(ro)); sig_byte((unsigned)fo); sig_byte((unsigned)eo);
    FP_PREAMBLE; rh = host(x); eh = errno; fh = fetestexcept(FE_ALL_EXCEPT);
    okf(same_f(ro, rh), "%sf(%g) value differs from host", nm, (double)x);
    okf(bits_f(ro) == bits_f(rh) || (isnan(ro) && isnan(rh)),
        "%sf(%g) bits differ from host", nm, (double)x);
    okf(fo == fh, "%sf(%g) fp-flags cplus=%d host=%d", nm, (double)x, fo, fh);
    okf(eo == eh, "%sf(%g) errno cplus=%d host=%d", nm, (double)x, eo, eh);
    okf(fegetround() == FE_TONEAREST, "%sf(%g) rounding mode changed", nm, (double)x);
    fesetenv(&env);
}
static void chk_f2(const char *nm, fn_f2 fn, fn_f2 host, float x, float y) {
    fenv_t env; fegetenv(&env);
    float ro, rh; int eo, eh, fo, fh;
    FP_PREAMBLE; ro = fn(x, y);  eo = errno; fo = fetestexcept(FE_ALL_EXCEPT);
    sig_u64(bits_f(ro)); sig_byte((unsigned)fo); sig_byte((unsigned)eo);
    FP_PREAMBLE; rh = host(x, y); eh = errno; fh = fetestexcept(FE_ALL_EXCEPT);
    okf(same_f(ro, rh), "%sf(%g,%g) value differs from host", nm, (double)x, (double)y);
    okf(fo == fh, "%sf(...) fp-flags cplus=%d host=%d", nm, fo, fh);
    okf(eo == eh, "%sf(...) errno cplus=%d host=%d", nm, eo, eh);
    okf(fegetround() == FE_TONEAREST, "%sf(...) rounding mode changed", nm);
    fesetenv(&env);
}
static void chk_f3(const char *nm, fn_f3 fn, fn_f3 host,
                   float x, float y, float z) {
    fenv_t env; fegetenv(&env);
    float ro, rh; int eo, eh, fo, fh;
    FP_PREAMBLE; ro = fn(x, y, z);  eo = errno; fo = fetestexcept(FE_ALL_EXCEPT);
    sig_u64(bits_f(ro)); sig_byte((unsigned)fo); sig_byte((unsigned)eo);
    FP_PREAMBLE; rh = host(x, y, z); eh = errno; fh = fetestexcept(FE_ALL_EXCEPT);
    okf(same_f(ro, rh), "%sf(... ) value differs from host", nm);
    okf(fo == fh, "%sf(...) fp-flags cplus=%d host=%d", nm, fo, fh);
    okf(eo == eh, "%sf(...) errno cplus=%d host=%d", nm, eo, eh);
    fesetenv(&env);
}

static void chk_l1(const char *nm, fn_l1 fn, fn_l1 host, long double x) {
    fenv_t env; fegetenv(&env);
    long double ro, rh; int eo, eh, fo, fh;
    FP_PREAMBLE; ro = fn(x);  eo = errno; fo = fetestexcept(FE_ALL_EXCEPT);
    sig_u64(bits_l(ro)); sig_byte((unsigned)fo); sig_byte((unsigned)eo);
    FP_PREAMBLE; rh = host(x); eh = errno; fh = fetestexcept(FE_ALL_EXCEPT);
    okf(same_l(ro, rh), "%sl(%Lg) value differs from host", nm, x);
    okf(fo == fh, "%sl(%Lg) fp-flags cplus=%d host=%d", nm, x, fo, fh);
    okf(eo == eh, "%sl(%Lg) errno cplus=%d host=%d", nm, x, eo, eh);
    okf(fegetround() == FE_TONEAREST, "%sl(%Lg) rounding mode changed", nm, x);
    fesetenv(&env);
}
static void chk_l2(const char *nm, fn_l2 fn, fn_l2 host, long double x, long double y) {
    fenv_t env; fegetenv(&env);
    long double ro, rh; int eo, eh, fo, fh;
    FP_PREAMBLE; ro = fn(x, y);  eo = errno; fo = fetestexcept(FE_ALL_EXCEPT);
    sig_u64(bits_l(ro)); sig_byte((unsigned)fo); sig_byte((unsigned)eo);
    FP_PREAMBLE; rh = host(x, y); eh = errno; fh = fetestexcept(FE_ALL_EXCEPT);
    okf(same_l(ro, rh), "%sl(%Lg,%Lg) value differs from host", nm, x, y);
    okf(fo == fh, "%sl(...) fp-flags cplus=%d host=%d", nm, fo, fh);
    okf(eo == eh, "%sl(...) errno cplus=%d host=%d", nm, eo, eh);
    okf(fegetround() == FE_TONEAREST, "%sl(...) rounding mode changed", nm);
    fesetenv(&env);
}
static void chk_l3(const char *nm, fn_l3 fn, fn_l3 host,
                   long double x, long double y, long double z) {
    fenv_t env; fegetenv(&env);
    long double ro, rh; int eo, eh, fo, fh;
    FP_PREAMBLE; ro = fn(x, y, z);  eo = errno; fo = fetestexcept(FE_ALL_EXCEPT);
    sig_u64(bits_l(ro)); sig_byte((unsigned)fo); sig_byte((unsigned)eo);
    FP_PREAMBLE; rh = host(x, y, z); eh = errno; fh = fetestexcept(FE_ALL_EXCEPT);
    okf(same_l(ro, rh), "%sl(... ) value differs from host", nm);
    okf(fo == fh, "%sl(...) fp-flags cplus=%d host=%d", nm, fo, fh);
    okf(eo == eh, "%sl(...) errno cplus=%d host=%d", nm, eo, eh);
    fesetenv(&env);
}

static void chk_c1(const char *nm, fn_c1 fn, fn_c1 host, double complex z) {
    fenv_t env; fegetenv(&env);
    double complex ro, rh; int eo, eh, fo, fh;
    FP_PREAMBLE; ro = fn(z);  eo = errno; fo = fetestexcept(FE_ALL_EXCEPT);
    sig_u64(bits_d(creal(ro))); sig_u64(bits_d(cimag(ro)));
    sig_byte((unsigned)fo); sig_byte((unsigned)eo);
    FP_PREAMBLE; rh = host(z); eh = errno; fh = fetestexcept(FE_ALL_EXCEPT);
    okf(same_cd(ro, rh), "%s(%g%+gi) value differs from host",
        nm, creal(z), cimag(z));
    okf(fo == fh, "%s(...) fp-flags cplus=%d host=%d", nm, fo, fh);
    okf(eo == eh, "%s(...) errno cplus=%d host=%d", nm, eo, eh);
    okf(fegetround() == FE_TONEAREST, "%s(...) rounding mode changed", nm);
    fesetenv(&env);
}
static void chk_abs(const char *nm, double (*fn)(double complex),
                    double (*host)(double complex), double complex z) {
    fenv_t env; fegetenv(&env);
    double ro, rh; int eo, eh, fo, fh;
    FP_PREAMBLE; ro = fn(z);  eo = errno; fo = fetestexcept(FE_ALL_EXCEPT);
    sig_u64(bits_d(ro)); sig_byte((unsigned)fo); sig_byte((unsigned)eo);
    FP_PREAMBLE; rh = host(z); eh = errno; fh = fetestexcept(FE_ALL_EXCEPT);
    okf(same_d(ro, rh), "%s(%g%+gi) value differs from host", nm, creal(z), cimag(z));
    okf(fo == fh, "%s(...) fp-flags cplus=%d host=%d", nm, fo, fh);
    okf(eo == eh, "%s(...) errno cplus=%d host=%d", nm, eo, eh);
    fesetenv(&env);
}
static void chk_c2(const char *nm, fn_c2 fn, fn_c2 host,
                   double complex x, double complex y) {
    fenv_t env; fegetenv(&env);
    double complex ro, rh; int eo, eh, fo, fh;
    FP_PREAMBLE; ro = fn(x, y);  eo = errno; fo = fetestexcept(FE_ALL_EXCEPT);
    sig_u64(bits_d(creal(ro))); sig_u64(bits_d(cimag(ro)));
    sig_byte((unsigned)fo); sig_byte((unsigned)eo);
    FP_PREAMBLE; rh = host(x, y); eh = errno; fh = fetestexcept(FE_ALL_EXCEPT);
    okf(same_cd(ro, rh), "%s(...) value differs from host", nm);
    okf(fo == fh, "%s(...) fp-flags cplus=%d host=%d", nm, fo, fh);
    okf(eo == eh, "%s(...) errno cplus=%d host=%d", nm, eo, eh);
    fesetenv(&env);
}

/* ------------------------------------------------------- generated methods */

#define MT_D1(n, slug, cname, hname, ...)                                    \
static void m_mt_##n##_##slug(void) {                                        \
    static const double xs[] = { __VA_ARGS__ };                               \
    size_t i_;                                                               \
    for (i_ = 0; i_ < sizeof xs / sizeof xs[0]; ++i_)                         \
        chk_d1(#cname, cplus_##cname, hname, xs[i_]);                         \
}

#define MT_D2(n, slug, cname, hname, ...)                                    \
static void m_mt_##n##_##slug(void) {                                        \
    static const double xs[][2] = { __VA_ARGS__ };                           \
    size_t i_;                                                               \
    for (i_ = 0; i_ < sizeof xs / sizeof xs[0]; ++i_)                         \
        chk_d2(#cname, cplus_##cname, hname, xs[i_][0], xs[i_][1]);           \
}

#define MT_F1(n, slug, cname, hname, ...)                                    \
static void m_mt_##n##_##slug(void) {                                        \
    static const float xs[] = { __VA_ARGS__ };                               \
    size_t i_;                                                               \
    for (i_ = 0; i_ < sizeof xs / sizeof xs[0]; ++i_)                         \
        chk_f1(#cname, cplus_##cname, hname, xs[i_]);                         \
}

#define MT_F2(n, slug, cname, hname, ...)                                    \
static void m_mt_##n##_##slug(void) {                                        \
    static const float xs[][2] = { __VA_ARGS__ };                            \
    size_t i_;                                                               \
    for (i_ = 0; i_ < sizeof xs / sizeof xs[0]; ++i_)                         \
        chk_f2(#cname, cplus_##cname, hname, xs[i_][0], xs[i_][1]);           \
}

#define MT_L1(n, slug, cname, hname, ...)                                    \
static void m_mt_##n##_##slug(void) {                                        \
    static const long double xs[] = { __VA_ARGS__ };                         \
    size_t i_;                                                               \
    for (i_ = 0; i_ < sizeof xs / sizeof xs[0]; ++i_)                         \
        chk_l1(#cname, cplus_##cname, hname, xs[i_]);                         \
}

#define MT_L2(n, slug, cname, hname, ...)                                    \
static void m_mt_##n##_##slug(void) {                                        \
    static const long double xs[][2] = { __VA_ARGS__ };                      \
    size_t i_;                                                               \
    for (i_ = 0; i_ < sizeof xs / sizeof xs[0]; ++i_)                         \
        chk_l2(#cname, cplus_##cname, hname, xs[i_][0], xs[i_][1]);           \
}

/* ---- double family ------------------------------------------------------ */
/* METHOD: sqrt(double) - normal, signed zero, domain error, Inf, NaN. */
MT_D1(1, sqrt, sqrt, sqrt, 2.0, 0.0, -0.0, -1.0, INFINITY, NAN, 1e-300)
/* METHOD: fabs(double) - sign removal incl signed zero/Inf/NaN. */
MT_D1(2, fabs, fabs, fabs, 3.5, -0.0, -7.25, INFINITY, -INFINITY, NAN)
/* METHOD: floor(double) - rounding down incl negatives/zero/Inf/NaN. */
MT_D1(3, floor, floor, floor, 2.7, -2.7, -0.0, 0.0, INFINITY, -INFINITY, NAN)
/* METHOD: ceil(double) - rounding up incl negatives/zero/Inf/NaN. */
MT_D1(4, ceil, ceil, ceil, 2.1, -2.1, -0.0, 0.0, INFINITY, -INFINITY, NAN)
/* METHOD: fmod(double,double) - remainder incl divide-by-zero domain error. */
MT_D2(5, fmod, fmod, fmod,
      {5.0,3.0}, {-5.0,3.0}, {5.0,0.0}, {INFINITY,2.0}, {5.0,INFINITY}, {NAN,2.0})
/* METHOD: pow(double,double) - normal, negative base, domain error, Inf/NaN. */
MT_D2(6, pow, pow, pow,
      {2.0,3.0}, {-2.0,3.0}, {-2.0,0.5}, {0.0,-1.0}, {NAN,2.0}, {2.0,INFINITY})
/* METHOD: exp(double) - normal, overflow, underflow, Inf, NaN. */
MT_D1(7, exp, exp, exp, 0.0, 1.0, -1.0, 1000.0, -1000.0, INFINITY, NAN)
/* METHOD: log(double) - normal, zero, negative domain error, Inf, NaN. */
MT_D1(8, log, log, log, 1.0, 2.718281828459045, 0.0, -1.0, INFINITY, NAN)
/* METHOD: sin(double) - normal, signed zero, Inf, NaN. */
MT_D1(9, sin, sin, sin, 0.0, -0.0, 1.0, 1.5707963267948966, INFINITY, NAN)
/* METHOD: cos(double) - normal, signed zero, Inf, NaN. */
MT_D1(10, cos, cos, cos, 0.0, -0.0, 1.0, 3.141592653589793, INFINITY, NAN)
/* METHOD: tan(double) - normal, signed zero, Inf, NaN. */
MT_D1(11, tan, tan, tan, 0.0, -0.0, 1.0, 1.5707963267948966, INFINITY, NAN)
/* METHOD: fmin(double,double) - min incl NaN propagation and signed zero. */
MT_D2(12, fmin, fmin, fmin,
      {1.0,2.0}, {-0.0,0.0}, {NAN,2.0}, {2.0,NAN}, {INFINITY,1.0}, {-INFINITY,3.0})
/* METHOD: fmax(double,double) - max incl NaN propagation and signed zero. */
MT_D2(13, fmax, fmax, fmax,
      {1.0,2.0}, {-0.0,0.0}, {NAN,2.0}, {2.0,NAN}, {INFINITY,1.0}, {-INFINITY,3.0})
/* METHOD: fma(double,double,double) - fused multiply-add incl Inf/NaN. */
static void m_mt_14_fma(void) {
    chk_d3("fma", cplus_fma, fma, 2.0, 3.0, 4.0);
    chk_d3("fma", cplus_fma, fma, -0.0, -0.0, -0.0);
    chk_d3("fma", cplus_fma, fma, 1.0, INFINITY, -INFINITY);
    chk_d3("fma", cplus_fma, fma, NAN, 2.0, 1.0);
}

/* METHOD: frexp(double,int*) - mantissa/exponent + NULL out-param guard. */
static void m_mt_15_frexp(void) {
    fenv_t env; fegetenv(&env);
    int e = 0, he = 0, e2 = 12345;
    double r, hr;
    const double vals[] = {8.0, -0.0, 0.0, INFINITY, NAN};
    size_t i;
    for (i = 0; i < sizeof vals / sizeof vals[0]; ++i) {
        errno = 0; feclearexcept(FE_ALL_EXCEPT);
        r = cplus_frexp(vals[i], &e);
        sig_u64(bits_d(r)); sig_byte((unsigned)e);
        errno = 0; feclearexcept(FE_ALL_EXCEPT);
        hr = frexp(vals[i], &he);
        okf(same_d(r, hr) && e == he, "frexp(%g) mantissa/exponent differs from host", vals[i]);
        okf(fegetround() == FE_TONEAREST, "frexp(%g) rounding mode changed", vals[i]);
    }
    /* NULL out-param is a defined hardening extension: EINVAL + 0.0. */
    errno = 0; r = cplus_frexp(8.0, NULL);
    okf(r == 0.0 && errno == EINVAL, "frexp(NULL) must return 0/EINVAL (r=%g errno=%d)", r, errno);
    sig_byte((unsigned)errno);
    /* Guard must not clobber a caller FP environment. */
    ok(fegetround() == FE_TONEAREST, "frexp(NULL) changed rounding mode");
    (void)e2;
    fesetenv(&env);
}
/* METHOD: modf(double,double*) - fractional/int parts + NULL guard. */
static void m_mt_16_modf(void) {
    fenv_t env; fegetenv(&env);
    double ip = 9, hip = 9, r, hr;
    const double vals[] = {3.75, -3.75, -0.0, INFINITY, NAN};
    size_t i;
    for (i = 0; i < sizeof vals / sizeof vals[0]; ++i) {
        errno = 0; feclearexcept(FE_ALL_EXCEPT);
        r = cplus_modf(vals[i], &ip);
        sig_u64(bits_d(r)); sig_u64(bits_d(ip));
        errno = 0; feclearexcept(FE_ALL_EXCEPT);
        hr = modf(vals[i], &hip);
        okf(same_d(r, hr) && same_d(ip, hip), "modf(%g) parts differ from host", vals[i]);
    }
    errno = 0; r = cplus_modf(3.75, NULL);
    okf(r == 0.0 && errno == EINVAL, "modf(NULL) must return 0/EINVAL (r=%g errno=%d)", r, errno);
    sig_byte((unsigned)errno);
    ok(fegetround() == FE_TONEAREST, "modf(NULL) changed rounding mode");
    fesetenv(&env);
}
/* METHOD: remquo(double,double,int*) - quotient/remainder + NULL guard. */
static void m_mt_17_remquo(void) {
    fenv_t env; fegetenv(&env);
    int q = 77, hq = 77;
    double r, hr;
    static const double xy[][2] = {
        {5.0, 3.0}, {-5.0, 3.0}, {7.0, 0.0}, {INFINITY, 2.0}, {NAN, 2.0}
    };
    size_t i;
    for (i = 0; i < sizeof xy / sizeof xy[0]; ++i) {
        errno = 0; feclearexcept(FE_ALL_EXCEPT);
        r = cplus_remquo(xy[i][0], xy[i][1], &q);
        sig_u64(bits_d(r)); sig_byte((unsigned)q);
        errno = 0; feclearexcept(FE_ALL_EXCEPT);
        hr = remquo(xy[i][0], xy[i][1], &hq);
        okf(same_d(r, hr) && (isnan(r) || q == hq),
            "remquo(%g,%g) remainder/quotient differs from host", xy[i][0], xy[i][1]);
    }
    errno = 0; r = cplus_remquo(5.0, 3.0, NULL);
    okf(r == 0.0 && errno == EINVAL, "remquo(NULL) must return 0/EINVAL (r=%g errno=%d)", r, errno);
    sig_byte((unsigned)errno);
    ok(fegetround() == FE_TONEAREST, "remquo(NULL) changed rounding mode");
    fesetenv(&env);
}

/* ---- float family ------------------------------------------------------- */
#define FV(x) x##f
/* METHOD: sqrtf(float). */
MT_F1(18, sqrtf, sqrtf, sqrtf, FV(2.0), FV(0.0), FV(-0.0), FV(-1.0), INFINITY, NAN)
/* METHOD: fabsf(float). */
MT_F1(19, fabsf, fabsf, fabsf, FV(3.5), FV(-0.0), FV(-7.25), INFINITY, -INFINITY, NAN)
/* METHOD: floorf(float). */
MT_F1(20, floorf, floorf, floorf, FV(2.7), FV(-2.7), FV(-0.0), INFINITY, NAN)
/* METHOD: ceilf(float). */
MT_F1(21, ceilf, ceilf, ceilf, FV(2.1), FV(-2.1), FV(-0.0), INFINITY, NAN)
/* METHOD: fmodf(float,float). */
MT_F2(22, fmodf, fmodf, fmodf,
      {FV(5.0),FV(3.0)}, {FV(-5.0),FV(3.0)}, {FV(5.0),FV(0.0)}, {INFINITY,FV(2.0)})
/* METHOD: powf(float,float). */
MT_F2(23, powf, powf, powf,
      {FV(2.0),FV(3.0)}, {FV(-2.0),FV(0.5)}, {FV(0.0),FV(-1.0)}, {FV(2.0),INFINITY})
/* METHOD: expf(float). */
MT_F1(24, expf, expf, expf, FV(0.0), FV(1.0), FV(-1.0), FV(1000.0), INFINITY, NAN)
/* METHOD: logf(float). */
MT_F1(25, logf, logf, logf, FV(1.0), FV(0.0), FV(-1.0), INFINITY, NAN)
/* METHOD: sinf(float). */
MT_F1(26, sinf, sinf, sinf, FV(0.0), FV(-0.0), FV(1.0), INFINITY, NAN)
/* METHOD: cosf(float). */
MT_F1(27, cosf, cosf, cosf, FV(0.0), FV(-0.0), FV(1.0), INFINITY, NAN)
/* METHOD: tanf(float). */
MT_F1(28, tanf, tanf, tanf, FV(0.0), FV(-0.0), FV(1.0), INFINITY, NAN)
/* METHOD: fminf(float,float). */
MT_F2(29, fminf, fminf, fminf,
      {FV(1.0),FV(2.0)}, {FV(-0.0),FV(0.0)}, {NAN,FV(2.0)}, {FV(2.0),NAN})
/* METHOD: fmaxf(float,float). */
MT_F2(30, fmaxf, fmaxf, fmaxf,
      {FV(1.0),FV(2.0)}, {FV(-0.0),FV(0.0)}, {NAN,FV(2.0)}, {FV(2.0),NAN})
/* METHOD: fmaf(float,float,float). */
static void m_mt_31_fmaf(void) {
    chk_f3("fmaf", cplus_fmaf, fmaf, 2.0f, 3.0f, 4.0f);
    chk_f3("fmaf", cplus_fmaf, fmaf, -0.0f, -0.0f, -0.0f);
    chk_f3("fmaf", cplus_fmaf, fmaf, 1.0f, INFINITY, -INFINITY);
}
/* METHOD: frexpf(float,int*) + NULL guard. */
static void m_mt_32_frexpf(void) {
    fenv_t env; fegetenv(&env);
    int e, he; float r, hr;
    const float vals[] = {8.0f, -0.0f, 0.0f, INFINITY, NAN};
    size_t i;
    for (i = 0; i < sizeof vals / sizeof vals[0]; ++i) {
        errno = 0; feclearexcept(FE_ALL_EXCEPT); r = cplus_frexpf(vals[i], &e);
        errno = 0; feclearexcept(FE_ALL_EXCEPT); hr = frexpf(vals[i], &he);
        okf(same_f(r, hr) && e == he, "frexpf(%g) differs from host", (double)vals[i]);
    }
    errno = 0; r = cplus_frexpf(8.0f, NULL);
    okf(r == 0.0f && errno == EINVAL, "frexpf(NULL) must return 0/EINVAL");
    sig_byte((unsigned)errno);
    fesetenv(&env);
}
/* METHOD: modff(float,float*) + NULL guard. */
static void m_mt_33_modff(void) {
    fenv_t env; fegetenv(&env);
    float ip, hip, r, hr;
    const float vals[] = {3.75f, -3.75f, -0.0f, INFINITY, NAN};
    size_t i;
    for (i = 0; i < sizeof vals / sizeof vals[0]; ++i) {
        errno = 0; feclearexcept(FE_ALL_EXCEPT); r = cplus_modff(vals[i], &ip);
        errno = 0; feclearexcept(FE_ALL_EXCEPT); hr = modff(vals[i], &hip);
        okf(same_f(r, hr) && same_f(ip, hip), "modff(%g) differs from host", (double)vals[i]);
    }
    errno = 0; r = cplus_modff(3.75f, NULL);
    okf(r == 0.0f && errno == EINVAL, "modff(NULL) must return 0/EINVAL");
    sig_byte((unsigned)errno);
    fesetenv(&env);
}
/* METHOD: remquof(float,float,int*) + NULL guard. */
static void m_mt_34_remquof(void) {
    fenv_t env; fegetenv(&env);
    int q, hq; float r, hr;
    static const float xy[][2] = {{5.0f,3.0f},{-5.0f,3.0f},{7.0f,0.0f},{INFINITY,2.0f}};
    size_t i;
    for (i = 0; i < sizeof xy / sizeof xy[0]; ++i) {
        errno = 0; feclearexcept(FE_ALL_EXCEPT); r = cplus_remquof(xy[i][0], xy[i][1], &q);
        errno = 0; feclearexcept(FE_ALL_EXCEPT); hr = remquof(xy[i][0], xy[i][1], &hq);
        okf(same_f(r, hr) && (isnan(r) || q == hq), "remquof(...) differs from host");
    }
    errno = 0; r = cplus_remquof(5.0f, 3.0f, NULL);
    okf(r == 0.0f && errno == EINVAL, "remquof(NULL) must return 0/EINVAL");
    sig_byte((unsigned)errno);
    fesetenv(&env);
}

/* ---- long-double family ------------------------------------------------- */
#define LV(x) x##L
/* METHOD: sqrtl(long double). */
MT_L1(35, sqrtl, sqrtl, sqrtl, LV(2.0), LV(0.0), LV(-0.0), LV(-1.0), INFINITY, NAN)
/* METHOD: fabsl(long double). */
MT_L1(36, fabsl, fabsl, fabsl, LV(3.5), LV(-0.0), LV(-7.25), INFINITY, -INFINITY, NAN)
/* METHOD: floorl(long double). */
MT_L1(37, floorl, floorl, floorl, LV(2.7), LV(-2.7), LV(-0.0), INFINITY, NAN)
/* METHOD: ceill(long double). */
MT_L1(38, ceill, ceill, ceill, LV(2.1), LV(-2.1), LV(-0.0), INFINITY, NAN)
/* METHOD: fmodl(long double,long double). */
MT_L2(39, fmodl, fmodl, fmodl,
      {LV(5.0),LV(3.0)}, {LV(-5.0),LV(3.0)}, {LV(5.0),LV(0.0)}, {INFINITY,LV(2.0)})
/* METHOD: powl(long double,long double). */
MT_L2(40, powl, powl, powl,
      {LV(2.0),LV(3.0)}, {LV(-2.0),LV(0.5)}, {LV(0.0),LV(-1.0)}, {LV(2.0),INFINITY})
/* METHOD: expl(long double). */
MT_L1(41, expl, expl, expl, LV(0.0), LV(1.0), LV(-1.0), LV(1000.0), INFINITY, NAN)
/* METHOD: logl(long double). */
MT_L1(42, logl, logl, logl, LV(1.0), LV(0.0), LV(-1.0), INFINITY, NAN)
/* METHOD: sinl(long double). */
MT_L1(43, sinl, sinl, sinl, LV(0.0), LV(-0.0), LV(1.0), INFINITY, NAN)
/* METHOD: cosl(long double). */
MT_L1(44, cosl, cosl, cosl, LV(0.0), LV(-0.0), LV(1.0), INFINITY, NAN)
/* METHOD: tanl(long double). */
MT_L1(45, tanl, tanl, tanl, LV(0.0), LV(-0.0), LV(1.0), INFINITY, NAN)
/* METHOD: fminl(long double,long double). */
MT_L2(46, fminl, fminl, fminl,
      {LV(1.0),LV(2.0)}, {LV(-0.0),LV(0.0)}, {NAN,LV(2.0)}, {LV(2.0),NAN})
/* METHOD: fmaxl(long double,long double). */
MT_L2(47, fmaxl, fmaxl, fmaxl,
      {LV(1.0),LV(2.0)}, {LV(-0.0),LV(0.0)}, {NAN,LV(2.0)}, {LV(2.0),NAN})
/* METHOD: fmal(long double,long double,long double). */
static void m_mt_48_fmal(void) {
    chk_l3("fmal", cplus_fmal, fmal, 2.0L, 3.0L, 4.0L);
    chk_l3("fmal", cplus_fmal, fmal, -0.0L, -0.0L, -0.0L);
    chk_l3("fmal", cplus_fmal, fmal, 1.0L, INFINITY, -INFINITY);
}
/* METHOD: frexpl(long double,int*) + NULL guard. */
static void m_mt_49_frexpl(void) {
    fenv_t env; fegetenv(&env);
    int e, he; long double r, hr;
    const long double vals[] = {8.0L, -0.0L, 0.0L, INFINITY, NAN};
    size_t i;
    for (i = 0; i < sizeof vals / sizeof vals[0]; ++i) {
        errno = 0; feclearexcept(FE_ALL_EXCEPT); r = cplus_frexpl(vals[i], &e);
        errno = 0; feclearexcept(FE_ALL_EXCEPT); hr = frexpl(vals[i], &he);
        okf(same_l(r, hr) && e == he, "frexpl(%Lg) differs from host", vals[i]);
    }
    errno = 0; r = cplus_frexpl(8.0L, NULL);
    okf(r == 0.0L && errno == EINVAL, "frexpl(NULL) must return 0/EINVAL");
    sig_byte((unsigned)errno);
    fesetenv(&env);
}
/* METHOD: modfl(long double,long double*) + NULL guard. */
static void m_mt_50_modfl(void) {
    fenv_t env; fegetenv(&env);
    long double ip, hip, r, hr;
    const long double vals[] = {3.75L, -3.75L, -0.0L, INFINITY, NAN};
    size_t i;
    for (i = 0; i < sizeof vals / sizeof vals[0]; ++i) {
        errno = 0; feclearexcept(FE_ALL_EXCEPT); r = cplus_modfl(vals[i], &ip);
        errno = 0; feclearexcept(FE_ALL_EXCEPT); hr = modfl(vals[i], &hip);
        okf(same_l(r, hr) && same_l(ip, hip), "modfl(%Lg) differs from host", vals[i]);
    }
    errno = 0; r = cplus_modfl(3.75L, NULL);
    okf(r == 0.0L && errno == EINVAL, "modfl(NULL) must return 0/EINVAL");
    sig_byte((unsigned)errno);
    fesetenv(&env);
}
/* METHOD: remquol(long double,long double,int*) + NULL guard. */
static void m_mt_51_remquol(void) {
    fenv_t env; fegetenv(&env);
    int q, hq; long double r, hr;
    static const long double xy[][2] = {
        {5.0L,3.0L}, {-5.0L,3.0L}, {7.0L,0.0L}, {INFINITY,2.0L}
    };
    size_t i;
    for (i = 0; i < sizeof xy / sizeof xy[0]; ++i) {
        errno = 0; feclearexcept(FE_ALL_EXCEPT); r = cplus_remquol(xy[i][0], xy[i][1], &q);
        errno = 0; feclearexcept(FE_ALL_EXCEPT); hr = remquol(xy[i][0], xy[i][1], &hq);
        okf(same_l(r, hr) && (isnan(r) || q == hq), "remquol(...) differs from host");
    }
    errno = 0; r = cplus_remquol(5.0L, 3.0L, NULL);
    okf(r == 0.0L && errno == EINVAL, "remquol(NULL) must return 0/EINVAL");
    sig_byte((unsigned)errno);
    fesetenv(&env);
}

/* ---- complex family ----------------------------------------------------- */
#define C(x, y) ((double complex)(x) + (double complex)(y) * I)
/* METHOD: cabs(double complex) - magnitude incl Inf/NaN components. */
static void m_mt_52_cabs(void) {
    chk_abs("cabs", cplus_cabs, cabs, C(3.0, 4.0));
    chk_abs("cabs", cplus_cabs, cabs, C(-0.0, -0.0));
    chk_abs("cabs", cplus_cabs, cabs, C(INFINITY, 1.0));
    chk_abs("cabs", cplus_cabs, cabs, C(NAN, 1.0));
}
/* METHOD: csin(double complex) - normal, zero, Inf, NaN. */
static void m_mt_53_csin(void) {
    chk_c1("csin", cplus_csin, csin, C(1.0, 1.0));
    chk_c1("csin", cplus_csin, csin, C(0.0, 0.0));
    chk_c1("csin", cplus_csin, csin, C(INFINITY, 1.0));
    chk_c1("csin", cplus_csin, csin, C(NAN, 1.0));
}
/* METHOD: clog(double complex) - branch cut, zero, Inf, NaN. */
static void m_mt_54_clog(void) {
    chk_c1("clog", cplus_clog, clog, C(1.0, 0.0));
    chk_c1("clog", cplus_clog, clog, C(-1.0, 0.0));
    chk_c1("clog", cplus_clog, clog, C(0.0, 0.0));
    chk_c1("clog", cplus_clog, clog, C(INFINITY, 1.0));
    chk_c1("clog", cplus_clog, clog, C(NAN, 1.0));
}
/* METHOD: cpow(double complex,double complex) - normal/singular. */
static void m_mt_55_cpow(void) {
    chk_c2("cpow", cplus_cpow, cpow, C(2.0, 0.0), C(3.0, 0.0));
    chk_c2("cpow", cplus_cpow, cpow, C(0.0, 0.0), C(-1.0, 0.0));
    chk_c2("cpow", cplus_cpow, cpow, C(2.0, 1.0), C(3.0, -2.0));
}
/* METHOD: cproj(double complex) - finite, Inf and NaN projection. */
static void m_mt_56_cproj(void) {
    chk_c1("cproj", cplus_cproj, cproj, C(1.0, 2.0));
    chk_c1("cproj", cplus_cproj, cproj, C(INFINITY, 1.0));
    chk_c1("cproj", cplus_cproj, cproj, C(1.0, INFINITY));
    chk_c1("cproj", cplus_cproj, cproj, C(NAN, 1.0));
}

/* ---- time family -------------------------------------------------------- */
/* METHOD: time(time_t*) - return/pointer parity with host. */
static void m_mt_57_time(void) {
    time_t before, after, t1, r1, r2;
    fenv_t env; fegetenv(&env);
    errno = 0; before = time(NULL);
    errno = 0; r1 = cplus_time(&t1);
    errno = 0; after = time(NULL);
    ok(r1 == t1, "time(out) return must equal stored value");
    ok(before <= r1 && r1 <= after, "time(out) value out of host bounds");
    errno = 0; r2 = cplus_time(NULL);
    ok(r2 >= before - 1 && r2 <= after + 1, "time(NULL) value out of bounds");
    ok(fegetround() == FE_TONEAREST, "time() changed rounding mode");
    fesetenv(&env);
}
/* METHOD: difftime(time_t,time_t) - exact host differential. */
static void m_mt_58_difftime(void) {
    fenv_t env; fegetenv(&env);
    static const long pairs[][2] = {{100,250},{250,100},{0,0},{-5,5},{1000000,999999}};
    size_t i;
    for (i = 0; i < sizeof pairs / sizeof pairs[0]; ++i) {
        double r, hr;
        errno = 0; r = cplus_difftime(pairs[i][0], pairs[i][1]);
        sig_u64(bits_d(r));
        errno = 0; hr = difftime((time_t)pairs[i][0], (time_t)pairs[i][1]);
        okf(r == hr, "difftime(%ld,%ld) differs from host", pairs[i][0], pairs[i][1]);
    }
    fesetenv(&env);
}
/* METHOD: clock(void) - non-negative, monotonic, host class parity. */
static void m_mt_59_clock(void) {
    fenv_t env; fegetenv(&env);
    clock_t c1, c2, h1;
    errno = 0; c1 = (clock_t)cplus_clock();
    errno = 0; h1 = clock();
    errno = 0; c2 = (clock_t)cplus_clock();
    ok(c1 >= 0, "clock() must be non-negative");
    ok(h1 >= 0, "host clock() must be non-negative");
    ok(c2 >= c1, "clock() must be monotonic across calls");
    ok(fegetround() == FE_TONEAREST, "clock() changed rounding mode");
    fesetenv(&env);
}
/* METHOD: gmtime(const time_t*) - field-wise host differential. */
static void m_mt_60_gmtime(void) {
    fenv_t env; fegetenv(&env);
    time_t t = (time_t)0;
    struct tm ca, cb, *a, *b;
    errno = 0; a = cplus_gmtime(&t);
    ok(a != NULL, "gmtime(0) returned NULL");
    if (a) memcpy(&ca, a, sizeof ca);
    errno = 0; b = gmtime(&t);
    ok(b != NULL, "host gmtime(0) returned NULL");
    if (b) memcpy(&cb, b, sizeof cb);
    if (a && b) {
        ok(ca.tm_year == cb.tm_year && ca.tm_mon == cb.tm_mon &&
           ca.tm_mday == cb.tm_mday && ca.tm_hour == cb.tm_hour &&
           ca.tm_min == cb.tm_min && ca.tm_sec == cb.tm_sec,
           "gmtime(0) fields differ from host");
        sig_byte((unsigned)ca.tm_year); sig_byte((unsigned)ca.tm_mon);
    }
    fesetenv(&env);
}
/* METHOD: localtime(const time_t*) - field-wise host differential. */
static void m_mt_61_localtime(void) {
    fenv_t env; fegetenv(&env);
    time_t t = (time_t)1000000000;
    struct tm ca, cb, *a, *b;
    errno = 0; a = cplus_localtime(&t);
    ok(a != NULL, "localtime() returned NULL");
    if (a) memcpy(&ca, a, sizeof ca);
    errno = 0; b = localtime(&t);
    ok(b != NULL, "host localtime() returned NULL");
    if (b) memcpy(&cb, b, sizeof cb);
    if (a && b) {
        ok(ca.tm_year == cb.tm_year && ca.tm_mon == cb.tm_mon &&
           ca.tm_mday == cb.tm_mday && ca.tm_hour == cb.tm_hour &&
           ca.tm_min == cb.tm_min && ca.tm_sec == cb.tm_sec,
           "localtime() fields differ from host");
        sig_byte((unsigned)ca.tm_year); sig_byte((unsigned)ca.tm_hour);
    }
    fesetenv(&env);
}
/* METHOD: mktime(struct tm*) - normalization + host differential. */
static void m_mt_62_mktime(void) {
    fenv_t env; fegetenv(&env);
    struct tm a, b;
    time_t ta, tb;
    memset(&a, 0, sizeof a);
    a.tm_year = 100; a.tm_mon = 0; a.tm_mday = 1;
    a.tm_hour = 12; a.tm_min = 0; a.tm_sec = 0; a.tm_isdst = -1;
    memcpy(&b, &a, sizeof b);
    errno = 0; ta = cplus_mktime(&a);
    errno = 0; tb = mktime(&b);
    ok(ta == tb, "mktime() epoch differs from host");
    ok(a.tm_year == b.tm_year && a.tm_mon == b.tm_mon &&
       a.tm_mday == b.tm_mday && a.tm_hour == b.tm_hour,
       "mktime() normalization differs from host");
    sig_byte((unsigned)(ta & 0xff));
    fesetenv(&env);
}
/* METHOD: strftime - formatted length/content + truncation + host parity. */
static void m_mt_63_strftime(void) {
    fenv_t env; fegetenv(&env);
    struct tm tmv;
    char a[64], b[64];
    size_t na, nb;
    memset(&tmv, 0, sizeof tmv);
    tmv.tm_year = 100; tmv.tm_mon = 0; tmv.tm_mday = 1;
    tmv.tm_hour = 12; tmv.tm_min = 34; tmv.tm_sec = 56; tmv.tm_wday = 6;
    errno = 0;
    na = cplus_strftime(a, sizeof a, "%Y-%m-%d %H:%M:%S", &tmv);
    nb = strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", &tmv);
    okf(na == nb && na > 0 && strcmp(a, b) == 0,
        "strftime() result differs from host ('%s' vs '%s')", a, b);
    okf(strcmp(a, "2000-01-01 12:34:56") == 0,
        "strftime() unexpected formatted value '%s'", a);
    /* Too-small buffer must return 0 (defined, no overflow). */
    errno = 0;
    ok(cplus_strftime(a, 2, "%Y-%m-%d", &tmv) == 0, "strftime() tiny buffer must return 0");
    sig_byte((unsigned)na); sig_byte((unsigned)a[0]);
    fesetenv(&env);
}
/* METHOD: gmtime_r - reentrant field parity + NULL guards. */
static void m_mt_64_gmtime_r(void) {
    fenv_t env; fegetenv(&env);
    time_t t = (time_t)0;
    struct tm a, b, *ra, *rb;
    errno = 0; ra = cplus_gmtime_r(&t, &a);
    errno = 0; rb = gmtime_r(&t, &b);
    ok(ra == &a && rb == &b, "gmtime_r() must return the result pointer");
    ok(a.tm_year == b.tm_year && a.tm_mon == b.tm_mon &&
       a.tm_mday == b.tm_mday && a.tm_hour == b.tm_hour,
       "gmtime_r() fields differ from host");
    errno = 0;
    ok(cplus_gmtime_r(NULL, &a) == NULL && errno == EINVAL, "gmtime_r(NULL time) EINVAL guard");
    errno = 0;
    ok(cplus_gmtime_r(&t, NULL) == NULL && errno == EINVAL, "gmtime_r(NULL out) EINVAL guard");
    sig_byte((unsigned)a.tm_year);
    fesetenv(&env);
}
/* METHOD: localtime_r - reentrant field parity + NULL guards. */
static void m_mt_65_localtime_r(void) {
    fenv_t env; fegetenv(&env);
    time_t t = (time_t)1000000000;
    struct tm a, b, *ra, *rb;
    errno = 0; ra = cplus_localtime_r(&t, &a);
    errno = 0; rb = localtime_r(&t, &b);
    ok(ra == &a && rb == &b, "localtime_r() must return the result pointer");
    ok(a.tm_year == b.tm_year && a.tm_mon == b.tm_mon &&
       a.tm_mday == b.tm_mday && a.tm_hour == b.tm_hour,
       "localtime_r() fields differ from host");
    errno = 0;
    ok(cplus_localtime_r(NULL, &a) == NULL && errno == EINVAL, "localtime_r(NULL time) EINVAL guard");
    errno = 0;
    ok(cplus_localtime_r(&t, NULL) == NULL && errno == EINVAL, "localtime_r(NULL out) EINVAL guard");
    sig_byte((unsigned)a.tm_hour);
    fesetenv(&env);
}
/* METHOD: asctime_r - ASCII conversion parity + NULL guards. */
static void m_mt_66_asctime_r(void) {
    fenv_t env; fegetenv(&env);
    struct tm tmv;
    char a[64] = {0}, b[64] = {0}, *ra, *rb;
    memset(&tmv, 0, sizeof tmv);
    tmv.tm_year = 100; tmv.tm_mon = 0; tmv.tm_mday = 1; tmv.tm_wday = 6;
    errno = 0; ra = cplus_asctime_r(&tmv, a);
    errno = 0; rb = asctime_r(&tmv, b);
    ok(ra == a && rb == b, "asctime_r() must return the buffer");
    okf(strcmp(a, b) == 0, "asctime_r() differs from host ('%s' vs '%s')", a, b);
    errno = 0;
    ok(cplus_asctime_r(NULL, a) == NULL && errno == EINVAL, "asctime_r(NULL tm) EINVAL guard");
    errno = 0;
    ok(cplus_asctime_r(&tmv, NULL) == NULL && errno == EINVAL, "asctime_r(NULL out) EINVAL guard");
    sig_byte((unsigned)a[0]);
    fesetenv(&env);
}
/* METHOD: ctime_r - ASCII conversion parity + NULL guards. */
static void m_mt_67_ctime_r(void) {
    fenv_t env; fegetenv(&env);
    time_t t = (time_t)1000000000;
    char a[64] = {0}, b[64] = {0}, *ra, *rb;
    errno = 0; ra = cplus_ctime_r(&t, a);
    errno = 0; rb = ctime_r(&t, b);
    ok(ra == a && rb == b, "ctime_r() must return the buffer");
    okf(strcmp(a, b) == 0, "ctime_r() differs from host ('%s' vs '%s')", a, b);
    errno = 0;
    ok(cplus_ctime_r(NULL, a) == NULL && errno == EINVAL, "ctime_r(NULL time) EINVAL guard");
    errno = 0;
    ok(cplus_ctime_r(&t, NULL) == NULL && errno == EINVAL, "ctime_r(NULL out) EINVAL guard");
    sig_byte((unsigned)a[0]);
    fesetenv(&env);
}

/* ---- locale family ------------------------------------------------------ */
/* METHOD: setlocale - query/set + invalid-locale behaviour. */
static void m_mt_68_setlocale(void) {
    char *r;
    const char *q;
    (void)cplus_setlocale(LC_ALL, "C");
    r = cplus_setlocale(LC_ALL, "C");
    ok(r != NULL, "setlocale(LC_ALL,\"C\") must succeed");
    if (r) { sig_byte((unsigned)r[0]); sig_byte((unsigned)r[1]); }
    q = cplus_setlocale(LC_ALL, NULL);
    ok(q != NULL && strcmp(q, "C") == 0, "setlocale query must report 'C'");
    ok(cplus_setlocale(LC_ALL, "__cplus_no_such_locale__") == NULL,
       "setlocale(invalid) must return NULL");
    q = cplus_setlocale(LC_ALL, NULL);
    ok(q != NULL && strcmp(q, "C") == 0, "invalid setlocale must not change locale");
    /* Host differential on the query string. */
    {
        char hb[64] = {0};
        const char *h = setlocale(LC_ALL, NULL);
        if (h && strlen(h) < sizeof hb) memcpy(hb, h, strlen(h) + 1);
        okf(q != NULL && strcmp(q, hb) == 0, "setlocale query differs from host ('%s' vs '%s')",
            q ? q : "(null)", hb);
    }
    (void)cplus_setlocale(LC_ALL, "C");
}
/* METHOD: localeconv - decimal point / grouping under C locale. */
static void m_mt_69_localeconv(void) {
    struct lconv *lc, *h;
    (void)setlocale(LC_ALL, "C");
    lc = cplus_localeconv();
    h = localeconv();
    ok(lc != NULL, "localeconv() returned NULL");
    ok(h != NULL, "host localeconv() returned NULL");
    if (lc && h) {
        ok(lc->decimal_point != NULL && strcmp(lc->decimal_point, ".") == 0,
           "localeconv() C decimal_point must be '.'");
        ok(h->decimal_point != NULL && strcmp(lc->decimal_point, h->decimal_point) == 0,
           "localeconv() decimal_point differs from host");
        ok((lc->grouping == NULL && h->grouping == NULL) ||
           (lc->grouping && h->grouping && strcmp(lc->grouping, h->grouping) == 0),
           "localeconv() grouping differs from host");
        sig_byte((unsigned)lc->decimal_point[0]);
    }
}

/* ---- fenv family -------------------------------------------------------- */
/* METHOD: feclearexcept - clear individual and full masks. */
static void m_mt_70_feclearexcept(void) {
    fenv_t env; fegetenv(&env);
    ok(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "feclearexcept(ALL) == 0");
    ok(fetestexcept(FE_ALL_EXCEPT) == 0, "feclearexcept(ALL) clears flags");
    ok(cplus_feraiseexcept(FE_INVALID | FE_DIVBYZERO) == 0, "setup raise == 0");
    ok((fetestexcept(FE_INVALID | FE_DIVBYZERO) & (FE_INVALID | FE_DIVBYZERO)) ==
       (FE_INVALID | FE_DIVBYZERO), "setup flags present");
    ok(cplus_feclearexcept(FE_INVALID) == 0, "feclearexcept(FE_INVALID) == 0");
    ok((fetestexcept(FE_INVALID) & FE_INVALID) == 0, "FE_INVALID cleared");
    ok((fetestexcept(FE_DIVBYZERO) & FE_DIVBYZERO) != 0, "FE_DIVBYZERO preserved");
    cplus_feclearexcept(FE_ALL_EXCEPT);
    sig_byte((unsigned)fetestexcept(FE_ALL_EXCEPT));
    fesetenv(&env);
}
/* METHOD: fetestexcept - report raised flags accurately. */
static void m_mt_71_fetestexcept(void) {
    fenv_t env; fegetenv(&env);
    ok(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "fetestexcept setup clear");
    ok(cplus_fetestexcept(FE_ALL_EXCEPT) == 0, "no flags raised -> 0");
    ok(cplus_feraiseexcept(FE_OVERFLOW) == 0, "raise FE_OVERFLOW");
    ok((cplus_fetestexcept(FE_OVERFLOW) & FE_OVERFLOW) != 0, "FE_OVERFLOW reported");
    ok((cplus_fetestexcept(FE_INVALID) & FE_INVALID) == 0, "FE_INVALID not reported");
    ok(cplus_fetestexcept(FE_ALL_EXCEPT) == fetestexcept(FE_ALL_EXCEPT),
       "cplus_fetestexcept matches host view");
    sig_byte((unsigned)cplus_fetestexcept(FE_ALL_EXCEPT));
    cplus_feclearexcept(FE_ALL_EXCEPT);
    fesetenv(&env);
}
/* METHOD: fegetround - query matches host and is a valid mode. */
static void m_mt_72_fegetround(void) {
    fenv_t env; fegetenv(&env);
    int r = cplus_fegetround();
    ok(r == fegetround(), "fegetround() matches host");
    okf(r == FE_TONEAREST || r == FE_DOWNWARD || r == FE_UPWARD || r == FE_TOWARDZERO,
        "fegetround() returned a valid mode (got %d)", r);
    sig_byte((unsigned)r);
    fesetenv(&env);
}
/* METHOD: fesetround - install each mode then restore, preservation checks. */
static void m_mt_73_fesetround(void) {
    fenv_t env; fegetenv(&env);
    int orig = fegetround();
    static const int modes[] = {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
    size_t i;
    for (i = 0; i < sizeof modes / sizeof modes[0]; ++i) {
        int rc = cplus_fesetround(modes[i]);
        okf(rc == 0, "fesetround(mode %d) == 0", modes[i]);
        okf(cplus_fegetround() == modes[i], "fesetround(mode %d) installed", modes[i]);
        sig_byte((unsigned)cplus_fegetround());
    }
    ok(cplus_fesetround(orig) == 0, "fesetround restore original");
    ok(cplus_fegetround() == orig, "original rounding mode restored");
    fesetenv(&env);
}
/* METHOD: feholdexcept - save/clear env + NULL guard. */
static void m_mt_74_feholdexcept(void) {
    fenv_t env, held; fegetenv(&env);
    int orig = fegetround();
    ok(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "hold setup clear");
    ok(cplus_feraiseexcept(FE_INVALID) == 0, "hold setup raise");
    ok(cplus_fesetround(FE_UPWARD) == 0, "hold setup rounding");
    ok(cplus_feholdexcept(&held) == 0, "feholdexcept(&held) == 0");
    ok(fetestexcept(FE_ALL_EXCEPT) == 0, "feholdexcept clears exceptions");
    ok(fegetround() == FE_UPWARD, "feholdexcept preserves rounding mode");
    ok(cplus_feraiseexcept(FE_DIVBYZERO) == 0, "raise after hold");
    ok(cplus_fesetenv(&held) == 0, "restore held env");
    /* feholdexcept saves the hold-time environment (rounding included), so
     * restoring it must recover FE_UPWARD, not the pre-hold mode. */
    ok(cplus_fegetround() == FE_UPWARD, "held env restored hold-time rounding");
    errno = 0;
    ok(cplus_feholdexcept(NULL) != 0 && errno == EINVAL, "feholdexcept(NULL) EINVAL guard");
    sig_byte((unsigned)orig);
    cplus_feclearexcept(FE_ALL_EXCEPT);
    fesetenv(&env);
}
/* METHOD: feupdateenv - restore env and re-raise pending exceptions. */
static void m_mt_75_feupdateenv(void) {
    fenv_t env, held; fegetenv(&env);
    int orig = fegetround();
    ok(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "update setup clear");
    ok(cplus_fesetround(FE_DOWNWARD) == 0, "update setup rounding");
    ok(cplus_feholdexcept(&held) == 0, "update hold");
    ok(cplus_feraiseexcept(FE_INVALID) == 0, "raise pending FE_INVALID");
    ok((fetestexcept(FE_INVALID) & FE_INVALID) != 0, "pending flag set");
    ok(cplus_feupdateenv(&held) == 0, "feupdateenv(&held) == 0");
    ok((fetestexcept(FE_INVALID) & FE_INVALID) != 0, "pending flag re-raised by update");
    ok(fegetround() == FE_DOWNWARD, "feupdateenv restored held rounding");
    sig_byte((unsigned)fegetround());
    ok(cplus_fesetround(orig) == 0, "restore original rounding");
    cplus_feclearexcept(FE_ALL_EXCEPT);
    fesetenv(&env);
}
/* METHOD: fegetexceptflag - capture flags + NULL guard. */
static void m_mt_76_fegetexceptflag(void) {
    fenv_t env; fegetenv(&env);
    fexcept_t flags;
    memset(&flags, 0, sizeof flags);
    ok(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "getflag setup clear");
    ok(cplus_feraiseexcept(FE_INVALID | FE_DIVBYZERO) == 0, "getflag setup raise");
    ok(cplus_fegetexceptflag(&flags, FE_INVALID | FE_DIVBYZERO) == 0,
       "fegetexceptflag == 0");
    sig_byte((unsigned)flags);
    ok(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "getflag clear after capture");
    ok(cplus_fesetexceptflag(&flags, FE_INVALID | FE_DIVBYZERO) == 0,
       "fesetexceptflag round-trip");
    ok((fetestexcept(FE_INVALID) & FE_INVALID) != 0, "captured FE_INVALID restored");
    errno = 0;
    ok(cplus_fegetexceptflag(NULL, FE_ALL_EXCEPT) != 0 && errno == EINVAL,
       "fegetexceptflag(NULL) EINVAL guard");
    cplus_feclearexcept(FE_ALL_EXCEPT);
    fesetenv(&env);
}
/* METHOD: fesetexceptflag - restore flags from object incl hostile state. */
static void m_mt_77_fesetexceptflag(void) {
    fenv_t env; fegetenv(&env);
    fexcept_t flags;
    ok(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "setflag setup clear");
    ok(cplus_feraiseexcept(FE_OVERFLOW) == 0, "setflag setup raise");
    ok(cplus_fegetexceptflag(&flags, FE_OVERFLOW) == 0, "setflag capture");
    ok(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "setflag clear");
    ok(cplus_fesetexceptflag(&flags, FE_OVERFLOW) == 0, "fesetexceptflag == 0");
    ok((fetestexcept(FE_OVERFLOW) & FE_OVERFLOW) != 0, "FE_OVERFLOW set from flag object");
    ok((fetestexcept(FE_INVALID) & FE_INVALID) == 0, "unraised FE_INVALID not fabricated");
    /* Hostile (0xFF) flag object must be handled without crashing. */
    memset(&flags, 0xFF, sizeof flags);
    { int r = cplus_fesetexceptflag(&flags, FE_ALL_EXCEPT); okf(r == 0 || r == -1,
        "fesetexceptflag(hostile) returns 0 or -1 (got %d)", r); }
    sig_byte((unsigned)fegetround());
    cplus_feclearexcept(FE_ALL_EXCEPT);
    fesetenv(&env);
}
/* METHOD: feraiseexcept - raise each exception and verify via fetestexcept. */
static void m_mt_78_feraiseexcept(void) {
    fenv_t env; fegetenv(&env);
    static const int flags[] = {FE_INVALID, FE_DIVBYZERO, FE_OVERFLOW, FE_UNDERFLOW, FE_INEXACT};
    size_t i;
    for (i = 0; i < sizeof flags / sizeof flags[0]; ++i) {
        ok(cplus_feclearexcept(FE_ALL_EXCEPT) == 0, "raise setup clear");
        okf(cplus_feraiseexcept(flags[i]) == 0, "feraiseexcept(mask %d) == 0", flags[i]);
        okf((cplus_fetestexcept(flags[i]) & flags[i]) == flags[i],
            "feraiseexcept(mask %d) observed", flags[i]);
        sig_byte((unsigned)cplus_fetestexcept(FE_ALL_EXCEPT));
    }
    cplus_feclearexcept(FE_ALL_EXCEPT);
    ok(fetestexcept(FE_ALL_EXCEPT) == 0, "final flags cleared");
    fesetenv(&env);
}

/* ------------------------------------------------------------------ runner */

typedef void (*method_fn)(void);

/* Verify the opt-in logger really emits a line when enabled (preflight). */
static void preflight_logging(void) {
    FILE *tf = tmpfile();
    int saved;
    char line[256] = {0};
    int got;
    if (!tf) { ok(0, "tmpfile() for logging preflight"); return; }
    saved = dup(STDERR_FILENO);
    (void)dup2(fileno(tf), STDERR_FILENO);
    cplus_libc_log_set_enabled(1);
    (void)cplus_sqrt(4.0);
    cplus_libc_log_set_enabled(0);
    fflush(tf);
    rewind(tf);
    got = fgets(line, sizeof line, tf) != NULL;
    ok(got && strstr(line, "[CPLUS-LIBC] sqrt: enter") != NULL,
       "logging emits '[CPLUS-LIBC] sqrt: enter' when enabled");
    if (saved >= 0) { (void)dup2(saved, STDERR_FILENO); (void)close(saved); }
    fclose(tf);
}

/* Run fn with logging OFF (authoritative) and ON (silent), compare signatures. */
static void run_method(const char *name, method_fn fn) {
    fenv_t env_before;
    int len_off, len_on, parity;
    char sig_off[16384];

    fegetenv(&env_before);

    cplus_libc_log_set_enabled(0);
    g_sig_len = 0;
    fn();
    len_off = g_sig_len;
    if (len_off > (int)sizeof sig_off) len_off = (int)sizeof sig_off;
    memcpy(sig_off, g_sig, (size_t)len_off);

    cplus_libc_log_set_enabled(1);
    g_silent = 1;
    g_sig_len = 0;
    fn();
    len_on = g_sig_len;
    g_silent = 0;
    cplus_libc_log_set_enabled(0);

    parity = (len_off == len_on) && (memcmp(sig_off, g_sig, (size_t)len_off) == 0);
    okf(parity, "%s: logging ON/OFF parity (off_len=%d on_len=%d)", name, len_off, len_on);

    fesetenv(&env_before);
}

int main(void) {
    fenv_t startup_env;

    fegetenv(&startup_env);
    (void)setlocale(LC_ALL, "C");
    preflight_logging();
    quiet_begin();

    run_method("sqrt",      m_mt_1_sqrt);
    run_method("fabs",      m_mt_2_fabs);
    run_method("floor",     m_mt_3_floor);
    run_method("ceil",      m_mt_4_ceil);
    run_method("fmod",      m_mt_5_fmod);
    run_method("pow",       m_mt_6_pow);
    run_method("exp",       m_mt_7_exp);
    run_method("log",       m_mt_8_log);
    run_method("sin",       m_mt_9_sin);
    run_method("cos",       m_mt_10_cos);
    run_method("tan",       m_mt_11_tan);
    run_method("fmin",      m_mt_12_fmin);
    run_method("fmax",      m_mt_13_fmax);
    run_method("fma",       m_mt_14_fma);
    run_method("frexp",     m_mt_15_frexp);
    run_method("modf",      m_mt_16_modf);
    run_method("remquo",    m_mt_17_remquo);

    run_method("sqrtf",     m_mt_18_sqrtf);
    run_method("fabsf",     m_mt_19_fabsf);
    run_method("floorf",    m_mt_20_floorf);
    run_method("ceilf",     m_mt_21_ceilf);
    run_method("fmodf",     m_mt_22_fmodf);
    run_method("powf",      m_mt_23_powf);
    run_method("expf",      m_mt_24_expf);
    run_method("logf",      m_mt_25_logf);
    run_method("sinf",      m_mt_26_sinf);
    run_method("cosf",      m_mt_27_cosf);
    run_method("tanf",      m_mt_28_tanf);
    run_method("fminf",     m_mt_29_fminf);
    run_method("fmaxf",     m_mt_30_fmaxf);
    run_method("fmaf",      m_mt_31_fmaf);
    run_method("frexpf",    m_mt_32_frexpf);
    run_method("modff",     m_mt_33_modff);
    run_method("remquof",   m_mt_34_remquof);

    run_method("sqrtl",     m_mt_35_sqrtl);
    run_method("fabsl",     m_mt_36_fabsl);
    run_method("floorl",    m_mt_37_floorl);
    run_method("ceill",     m_mt_38_ceill);
    run_method("fmodl",     m_mt_39_fmodl);
    run_method("powl",      m_mt_40_powl);
    run_method("expl",      m_mt_41_expl);
    run_method("logl",      m_mt_42_logl);
    run_method("sinl",      m_mt_43_sinl);
    run_method("cosl",      m_mt_44_cosl);
    run_method("tanl",      m_mt_45_tanl);
    run_method("fminl",     m_mt_46_fminl);
    run_method("fmaxl",     m_mt_47_fmaxl);
    run_method("fmal",      m_mt_48_fmal);
    run_method("frexpl",    m_mt_49_frexpl);
    run_method("modfl",     m_mt_50_modfl);
    run_method("remquol",   m_mt_51_remquol);

    run_method("cabs",      m_mt_52_cabs);
    run_method("csin",      m_mt_53_csin);
    run_method("clog",      m_mt_54_clog);
    run_method("cpow",      m_mt_55_cpow);
    run_method("cproj",     m_mt_56_cproj);

    run_method("time",      m_mt_57_time);
    run_method("difftime",  m_mt_58_difftime);
    run_method("clock",     m_mt_59_clock);
    run_method("gmtime",    m_mt_60_gmtime);
    run_method("localtime", m_mt_61_localtime);
    run_method("mktime",    m_mt_62_mktime);
    run_method("strftime",  m_mt_63_strftime);
    run_method("gmtime_r",  m_mt_64_gmtime_r);
    run_method("localtime_r", m_mt_65_localtime_r);
    run_method("asctime_r", m_mt_66_asctime_r);
    run_method("ctime_r",   m_mt_67_ctime_r);

    run_method("setlocale",  m_mt_68_setlocale);
    run_method("localeconv", m_mt_69_localeconv);

    run_method("feclearexcept",  m_mt_70_feclearexcept);
    run_method("fetestexcept",   m_mt_71_fetestexcept);
    run_method("fegetround",     m_mt_72_fegetround);
    run_method("fesetround",     m_mt_73_fesetround);
    run_method("feholdexcept",   m_mt_74_feholdexcept);
    run_method("feupdateenv",    m_mt_75_feupdateenv);
    run_method("fegetexceptflag", m_mt_76_fegetexceptflag);
    run_method("fesetexceptflag", m_mt_77_fesetexceptflag);
    run_method("feraiseexcept",  m_mt_78_feraiseexcept);

    quiet_end();
    fesetenv(&startup_env);
    printf("methods_math_time: %d/%d checks ok\n", g_checks - g_fails, g_checks);
    return 0;
}
