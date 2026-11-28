/* hundred_math.c — 100 DISTINCT runtime scenarios for the math methods.
 *
 * Every scenario compares a cplus_* math wrapper against the SAME host libm
 * function called from this translation unit (class of result, finite value
 * within tolerance, errno, and FP exception flags).  On any divergence a
 * "FINDING: case <n> <method> <scenario>: ..." line is written to stderr.
 *
 * Output contract (one line per case, on stdout):
 *   hundred_math: case <n> <method> <scenario> <OUTCOME>
 * where OUTCOME is "ok" or "FINDING".  main() finishes with:
 *   hundred_math: <N>/100 scenarios ok
 * and always returns 0 so the owning harness can collect findings.
 *
 * Safety notes
 *  - No genuine UB: no integer casts of NaN/Inf, no integer / 0, no raw
 *    dereference of a NULL out-parameter (those are guarded by the wrapper
 *    and probed directly, never through host libm).
 *  - The incoming fenv_t and errno are saved/restored in main and around each
 *    case; state-mutating probes restore the environment they install.
 *  - Signaling NaNs are synthesised from a fixed IEEE-754 bit pattern via
 *    memcpy (no type-punning through a union), and are only ever passed to
 *    math functions — never converted to an integer.
 *
 * Build (mirrors tests/scenarios/run_scenarios.sh):
 *   clang -O2 -std=c99 -Wall -Wextra -pedantic \
 *     -I. -Ilibc -Ilibc/install-include \
 *     tests/methods/hundred_math.c \
 *     build/libc/libcplus_hardened.a -lm -o /tmp/hundred_math
 * UBSan: add -fsanitize=undefined -fno-sanitize-recover=all
 *        -fno-sanitize=float-divide-by-zero
 */
#include "cplus_libc.h"

#include <errno.h>
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <complex.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef DBL_TRUE_MIN
#define DBL_TRUE_MIN 0x1.0p-1074   /* smallest positive subnormal double */
#endif
#ifndef FLT_TRUE_MIN
#define FLT_TRUE_MIN 0x1.0p-149f   /* smallest positive subnormal float */
#endif

#define SENT 424242

static int g_case;
static const char *g_method;
static const char *g_scenario;
static int g_current_fail;
static int g_findings;
static int g_pass;

#define FIND(...) do {                                                       \
    fprintf(stderr, "FINDING: case %d %s %s: ",                              \
            g_case, g_method, g_scenario);                                   \
    fprintf(stderr, __VA_ARGS__);                                            \
    fputc('\n', stderr);                                                     \
    g_current_fail = 1;                                                      \
    g_findings++;                                                            \
} while (0)

/* Classification: 0 NaN, 1 -Inf, 2 +Inf, 3 -0, 4 +0, 5 finite-nonzero. */
static int cl(long double x)
{
    if (isnan(x)) return 0;
    if (isinf(x)) return signbit(x) ? 1 : 2;
    if (x == 0.0L) return signbit(x) ? 3 : 4;
    return 5;
}

static const char *clname(int c)
{
    static const char *const n[] = { "NaN", "-Inf", "+Inf", "-0", "+0", "finite" };
    return (c >= 0 && c <= 5) ? n[c] : "?";
}

static int same_ld(long double a, long double b)
{
    long double d, s;
    if (isnan(a) || isnan(b)) return isnan(a) && isnan(b);
    if (isinf(a) || isinf(b)) return a == b;
    d = fabsl(a - b);
    s = fabsl(a) > fabsl(b) ? fabsl(a) : fabsl(b);
    return d <= 1.0e-12L * (s + 1.0L);
}
static int same_d(double a, double b)   { return same_ld((long double)a, (long double)b); }
static int same_f(float a, float b)     { return same_ld((long double)a, (long double)b); }

/* A quiet/signaling NaN with the given IEEE-754 bit pattern (double only). */
static double bits_to_double(uint64_t bits)
{
    double d;
    memcpy(&d, &bits, sizeof d);
    return d;
}

/* ---- differential macros: wrapper vs host ------------------------------- */
#define DIFF1(label, w, h, x) do {                                           \
    volatile double vx_ = (double)(x);                                        \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    volatile double rw_ = (w)(vx_);                                           \
    int ew_ = errno, fw_ = fetestexcept(FE_ALL_EXCEPT);                       \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    volatile double rh_ = (h)(vx_);                                           \
    int eh_ = errno, fh_ = fetestexcept(FE_ALL_EXCEPT);                       \
    if (cl((long double)rw_) != cl((long double)rh_) || !same_d(rw_, rh_))    \
        FIND("%s(%s): class/value w=%s/%.17g h=%s/%.17g", label, #x,         \
             clname(cl((long double)rw_)), (double)rw_,                       \
             clname(cl((long double)rh_)), (double)rh_);                      \
    if (ew_ != eh_) FIND("%s(%s): errno w=%d h=%d", label, #x, ew_, eh_);     \
    if (fw_ != fh_) FIND("%s(%s): FP flags w=0x%x h=0x%x", label, #x, fw_, fh_); \
} while (0)

#define DIFF2(label, w, h, x, y) do {                                         \
    volatile double vx_ = (double)(x), vy_ = (double)(y);                     \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    volatile double rw_ = (w)(vx_, vy_);                                      \
    int ew_ = errno, fw_ = fetestexcept(FE_ALL_EXCEPT);                       \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    volatile double rh_ = (h)(vx_, vy_);                                      \
    int eh_ = errno, fh_ = fetestexcept(FE_ALL_EXCEPT);                       \
    if (cl((long double)rw_) != cl((long double)rh_) || !same_d(rw_, rh_))    \
        FIND("%s(%s,%s): class/value w=%s/%.17g h=%s/%.17g", label, #x, #y,   \
             clname(cl((long double)rw_)), (double)rw_,                       \
             clname(cl((long double)rh_)), (double)rh_);                      \
    if (ew_ != eh_) FIND("%s(%s,%s): errno w=%d h=%d", label, #x, #y, ew_, eh_); \
    if (fw_ != fh_) FIND("%s(%s,%s): FP flags w=0x%x h=0x%x", label, #x, #y, fw_, fh_); \
} while (0)

#define DIFF3(label, w, h, x, y, z) do {                                      \
    volatile double vx_ = (double)(x), vy_ = (double)(y), vz_ = (double)(z);  \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    volatile double rw_ = (w)(vx_, vy_, vz_);                                 \
    int ew_ = errno, fw_ = fetestexcept(FE_ALL_EXCEPT);                       \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    volatile double rh_ = (h)(vx_, vy_, vz_);                                 \
    int eh_ = errno, fh_ = fetestexcept(FE_ALL_EXCEPT);                       \
    if (cl((long double)rw_) != cl((long double)rh_) || !same_d(rw_, rh_))    \
        FIND("%s: class/value w=%.17g h=%.17g", label, (double)rw_, (double)rh_); \
    if (ew_ != eh_) FIND("%s: errno w=%d h=%d", label, ew_, eh_);             \
    if (fw_ != fh_) FIND("%s: FP flags w=0x%x h=0x%x", label, fw_, fh_);      \
} while (0)

#define DIFFF1(label, w, h, x) do {                                           \
    volatile float vx_ = (float)(x);                                          \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    volatile float rw_ = (w)(vx_);                                            \
    int ew_ = errno, fw_ = fetestexcept(FE_ALL_EXCEPT);                       \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    volatile float rh_ = (h)(vx_);                                            \
    int eh_ = errno, fh_ = fetestexcept(FE_ALL_EXCEPT);                       \
    if (cl((long double)rw_) != cl((long double)rh_) || !same_f(rw_, rh_))    \
        FIND("%s(%s): class/value w=%s/%.9g h=%s/%.9g", label, #x,            \
             clname(cl((long double)rw_)), (double)rw_,                       \
             clname(cl((long double)rh_)), (double)rh_);                      \
    if (ew_ != eh_) FIND("%s(%s): errno w=%d h=%d", label, #x, ew_, eh_);     \
    if (fw_ != fh_) FIND("%s(%s): FP flags w=0x%x h=0x%x", label, #x, fw_, fh_); \
} while (0)

#define DIFFF2(label, w, h, x, y) do {                                        \
    volatile float vx_ = (float)(x), vy_ = (float)(y);                        \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    volatile float rw_ = (w)(vx_, vy_);                                       \
    int ew_ = errno, fw_ = fetestexcept(FE_ALL_EXCEPT);                       \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    volatile float rh_ = (h)(vx_, vy_);                                       \
    int eh_ = errno, fh_ = fetestexcept(FE_ALL_EXCEPT);                       \
    if (cl((long double)rw_) != cl((long double)rh_) || !same_f(rw_, rh_))    \
        FIND("%s(%s,%s): class/value w=%s/%.9g h=%s/%.9g", label, #x, #y,     \
             clname(cl((long double)rw_)), (double)rw_,                       \
             clname(cl((long double)rh_)), (double)rh_);                      \
    if (ew_ != eh_) FIND("%s(%s,%s): errno w=%d h=%d", label, #x, #y, ew_, eh_); \
    if (fw_ != fh_) FIND("%s(%s,%s): FP flags w=0x%x h=0x%x", label, #x, #y, fw_, fh_); \
} while (0)

#define DIFFF3(label, w, h, x, y, z) do {                                     \
    volatile float vx_ = (float)(x), vy_ = (float)(y), vz_ = (float)(z);      \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    volatile float rw_ = (w)(vx_, vy_, vz_);                                  \
    int ew_ = errno, fw_ = fetestexcept(FE_ALL_EXCEPT);                       \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    volatile float rh_ = (h)(vx_, vy_, vz_);                                  \
    int eh_ = errno, fh_ = fetestexcept(FE_ALL_EXCEPT);                       \
    if (cl((long double)rw_) != cl((long double)rh_) || !same_f(rw_, rh_))    \
        FIND("%s: class/value w=%.9g h=%.9g", label, (double)rw_, (double)rh_); \
    if (ew_ != eh_) FIND("%s: errno w=%d h=%d", label, ew_, eh_);             \
    if (fw_ != fh_) FIND("%s: FP flags w=0x%x h=0x%x", label, fw_, fh_);      \
} while (0)

#define DIFFL1(label, w, h, x) do {                                           \
    volatile long double vx_ = (long double)(x);                              \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    volatile long double rw_ = (w)(vx_);                                      \
    int ew_ = errno, fw_ = fetestexcept(FE_ALL_EXCEPT);                       \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    volatile long double rh_ = (h)(vx_);                                      \
    int eh_ = errno, fh_ = fetestexcept(FE_ALL_EXCEPT);                       \
    if (cl(rw_) != cl(rh_) || !same_ld(rw_, rh_))                            \
        FIND("%s(%s): class/value w=%s/%.21Lg h=%s/%.21Lg", label, #x,        \
             clname(cl(rw_)), rw_, clname(cl(rh_)), rh_);                     \
    if (ew_ != eh_) FIND("%s(%s): errno w=%d h=%d", label, #x, ew_, eh_);     \
    if (fw_ != fh_) FIND("%s(%s): FP flags w=0x%x h=0x%x", label, #x, fw_, fh_); \
} while (0)

#define DIFFL2(label, w, h, x, y) do {                                        \
    volatile long double vx_ = (long double)(x), vy_ = (long double)(y);      \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    volatile long double rw_ = (w)(vx_, vy_);                                 \
    int ew_ = errno, fw_ = fetestexcept(FE_ALL_EXCEPT);                       \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    volatile long double rh_ = (h)(vx_, vy_);                                 \
    int eh_ = errno, fh_ = fetestexcept(FE_ALL_EXCEPT);                       \
    if (cl(rw_) != cl(rh_) || !same_ld(rw_, rh_))                            \
        FIND("%s(%s,%s): class/value w=%s/%.21Lg h=%s/%.21Lg", label, #x, #y, \
             clname(cl(rw_)), rw_, clname(cl(rh_)), rh_);                     \
    if (ew_ != eh_) FIND("%s(%s,%s): errno w=%d h=%d", label, #x, #y, ew_, eh_); \
    if (fw_ != fh_) FIND("%s(%s,%s): FP flags w=0x%x h=0x%x", label, #x, #y, fw_, fh_); \
} while (0)

#define DIFFL3(label, w, h, x, y, z) do {                                     \
    volatile long double vx_ = (long double)(x), vy_ = (long double)(y),     \
                       vz_ = (long double)(z);                               \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    volatile long double rw_ = (w)(vx_, vy_, vz_);                            \
    int ew_ = errno, fw_ = fetestexcept(FE_ALL_EXCEPT);                       \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    volatile long double rh_ = (h)(vx_, vy_, vz_);                            \
    int eh_ = errno, fh_ = fetestexcept(FE_ALL_EXCEPT);                       \
    if (cl(rw_) != cl(rh_) || !same_ld(rw_, rh_))                            \
        FIND("%s: class/value w=%.21Lg h=%.21Lg", label, rw_, rh_);           \
    if (ew_ != eh_) FIND("%s: errno w=%d h=%d", label, ew_, eh_);             \
    if (fw_ != fh_) FIND("%s: FP flags w=0x%x h=0x%x", label, fw_, fh_);      \
} while (0)

#define DIFFC1(label, w, h, zr, zi) do {                                      \
    volatile double vr_ = (double)(zr), vi_ = (double)(zi);                  \
    double complex vz_;                                                      \
    double svr_ = vr_, svi_ = vi_;                                           \
    memcpy(&vz_, &svr_, sizeof svr_);                                        \
    memcpy((char *)&vz_ + sizeof svr_, &svi_, sizeof svi_);                  \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    double complex rw_ = (w)(vz_);                                           \
    int ew_ = errno, fw_ = fetestexcept(FE_ALL_EXCEPT);                       \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    double complex rh_ = (h)(vz_);                                           \
    int eh_ = errno, fh_ = fetestexcept(FE_ALL_EXCEPT);                       \
    if (cl(creal(rw_)) != cl(creal(rh_)) || cl(cimag(rw_)) != cl(cimag(rh_)) || \
        !same_d(creal(rw_), creal(rh_)) || !same_d(cimag(rw_), cimag(rh_)))   \
        FIND("%s(%s,%s): complex w=(%.17g,%.17g) h=(%.17g,%.17g)",            \
             label, #zr, #zi, creal(rw_), cimag(rw_), creal(rh_), cimag(rh_)); \
    if (ew_ != eh_) FIND("%s(%s,%s): errno w=%d h=%d", label, #zr, #zi, ew_, eh_); \
    if (fw_ != fh_) FIND("%s(%s,%s): FP flags w=0x%x h=0x%x", label, #zr, #zi, fw_, fh_); \
} while (0)

#define DIFFC2(label, w, h, xr, xi, yr, yi) do {                              \
    volatile double ax_ = (double)(xr), bx_ = (double)(xi);                  \
    volatile double ay_ = (double)(yr), by_ = (double)(yi);                  \
    double complex vx_, vy_;                                                 \
    double sax_ = ax_, sbx_ = bx_, say_ = ay_, sby_ = by_;                   \
    memcpy(&vx_, &sax_, sizeof sax_);                                        \
    memcpy((char *)&vx_ + sizeof sax_, &sbx_, sizeof sbx_);                  \
    memcpy(&vy_, &say_, sizeof say_);                                        \
    memcpy((char *)&vy_ + sizeof say_, &sby_, sizeof sby_);                  \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    double complex rw_ = (w)(vx_, vy_);                                      \
    int ew_ = errno, fw_ = fetestexcept(FE_ALL_EXCEPT);                       \
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;                               \
    double complex rh_ = (h)(vx_, vy_);                                      \
    int eh_ = errno, fh_ = fetestexcept(FE_ALL_EXCEPT);                       \
    if (cl(creal(rw_)) != cl(creal(rh_)) || cl(cimag(rw_)) != cl(cimag(rh_)) || \
        !same_d(creal(rw_), creal(rh_)) || !same_d(cimag(rw_), cimag(rh_)))   \
        FIND("%s: complex w=(%.17g,%.17g) h=(%.17g,%.17g)",                   \
             label, creal(rw_), cimag(rw_), creal(rh_), cimag(rh_));          \
    if (ew_ != eh_) FIND("%s: errno w=%d h=%d", label, ew_, eh_);             \
    if (fw_ != fh_) FIND("%s: FP flags w=0x%x h=0x%x", label, fw_, fh_);      \
} while (0)

/* ---- differential helpers with out-parameters / int returns ------------- */
static void diff_frexp(const char *label, double x)
{
    int ew = -99, eh = -99, e1, e2, f1, f2;
    double mw, mh;
    volatile double vx = x;
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;
    mw = cplus_frexp(vx, &ew); e1 = errno; f1 = fetestexcept(FE_ALL_EXCEPT);
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;
    mh = frexp(vx, &eh);       e2 = errno; f2 = fetestexcept(FE_ALL_EXCEPT);
    if (!same_d(mw, mh) || ew != eh)
        FIND("%s frexp w=(%.17g,%d) h=(%.17g,%d)", label, mw, ew, mh, eh);
    if (e1 != e2 || f1 != f2)
        FIND("%s frexp errno/flags w=%d/0x%x h=%d/0x%x", label, e1, f1, e2, f2);
}

static void diff_modf(const char *label, double x)
{
    double iw = -99.0, ih = -99.0, rw, rh;
    int e1, e2, f1, f2;
    volatile double vx = x;
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;
    rw = cplus_modf(vx, &iw); e1 = errno; f1 = fetestexcept(FE_ALL_EXCEPT);
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;
    rh = modf(vx, &ih);       e2 = errno; f2 = fetestexcept(FE_ALL_EXCEPT);
    if (!same_d(rw, rh) || !same_d(iw, ih))
        FIND("%s modf w=(%.17g,%.17g) h=(%.17g,%.17g)", label, rw, iw, rh, ih);
    if (e1 != e2 || f1 != f2)
        FIND("%s modf errno/flags w=%d/0x%x h=%d/0x%x", label, e1, f1, e2, f2);
}

static void diff_remquo(const char *label, double x, double y)
{
    int qw = -999, qh = -999, e1, e2, f1, f2;
    double rw, rh;
    volatile double vx = x, vy = y;
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;
    rw = cplus_remquo(vx, vy, &qw); e1 = errno; f1 = fetestexcept(FE_ALL_EXCEPT);
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;
    rh = remquo(vx, vy, &qh);       e2 = errno; f2 = fetestexcept(FE_ALL_EXCEPT);
    if (!same_d(rw, rh) || qw != qh)
        FIND("%s remquo w=(%.17g,%d) h=(%.17g,%d)", label, rw, qw, rh, qh);
    if (e1 != e2 || f1 != f2)
        FIND("%s remquo errno/flags w=%d/0x%x h=%d/0x%x", label, e1, f1, e2, f2);
}

static void diff_ilogb(const char *label, double x)
{
    int iw, ih, e1, e2, f1, f2;
    volatile double vx = x;
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;
    iw = cplus_ilogb(vx); e1 = errno; f1 = fetestexcept(FE_ALL_EXCEPT);
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;
    ih = ilogb(vx);       e2 = errno; f2 = fetestexcept(FE_ALL_EXCEPT);
    if (iw != ih) FIND("%s ilogb w=%d h=%d", label, iw, ih);
    if (e1 != e2 || f1 != f2)
        FIND("%s ilogb errno/flags w=%d/0x%x h=%d/0x%x", label, e1, f1, e2, f2);
}

/* ===================== 100 DISTINCT scenarios =========================== */

/* --- double methods, primary scenario each (1-39) ------------------------ */
static void c01(void)  /* sqrt positive */
{
    volatile double x = 2.0;
    double r = cplus_sqrt(x);
    if (!same_d(r, 1.4142135623730951)) FIND("sqrt(2)=%.17g expected sqrt2", r);
    DIFF1("sqrt", cplus_sqrt, sqrt, 2.0);
}
static void c02(void)  /* fabs negative */
{
    volatile double x = -3.5;
    double r = cplus_fabs(x);
    if (signbit(r)) FIND("fabs(-3.5) kept sign bit");
    DIFF1("fabs", cplus_fabs, fabs, -3.5);
}
static void c03(void)  /* floor positive half */
{
    volatile double x = 2.5;
    if (!same_d(cplus_floor(x), 2.0)) FIND("floor(2.5) not 2");
    DIFF1("floor", cplus_floor, floor, 2.5);
}
static void c04(void)  /* ceil negative half */
{
    volatile double x = -2.5;
    if (!same_d(cplus_ceil(x), -2.0)) FIND("ceil(-2.5) not -2");
    DIFF1("ceil", cplus_ceil, ceil, -2.5);
}
static void c05(void)  /* round halfway ties away */
{
    if (!same_d(cplus_round(2.5), 3.0) || !same_d(cplus_round(-2.5), -3.0))
        FIND("round halfway not away from zero");
    DIFF1("round", cplus_round, round, 2.5);
    DIFF1("round", cplus_round, round, -2.5);
}
static void c06(void)  /* trunc toward zero */
{
    if (!same_d(cplus_trunc(-2.7), -2.0)) FIND("trunc(-2.7) not -2");
    DIFF1("trunc", cplus_trunc, trunc, -2.7);
}
static void c07(void)  /* fmod positive */
{
    DIFF2("fmod", cplus_fmod, fmod, 5.5, 2.0);
}
static void c08(void)  /* pow positive base/exponent */
{
    volatile double b = 2.0, e = 10.0;
    if (!same_d(cplus_pow(b, e), 1024.0)) FIND("pow(2,10) not 1024");
    DIFF2("pow", cplus_pow, pow, 2.0, 10.0);
}
static void c09(void)  /* exp positive */
{
    DIFF1("exp", cplus_exp, exp, 1.0);
}
static void c10(void)  /* log one */
{
    volatile double x = 1.0;
    if (!same_d(cplus_log(x), 0.0)) FIND("log(1) not 0");
    DIFF1("log", cplus_log, log, 1.0);
}
static void c11(void)  /* log10 positive */
{
    volatile double x = 100.0;
    if (!same_d(cplus_log10(x), 2.0)) FIND("log10(100) not 2");
    DIFF1("log10", cplus_log10, log10, 100.0);
}
static void c12(void)  /* log2 power of eight */
{
    volatile double x = 8.0;
    if (!same_d(cplus_log2(x), 3.0)) FIND("log2(8) not 3");
    DIFF1("log2", cplus_log2, log2, 8.0);
}
static void c13(void)  /* sin positive quadrant */
{
    DIFF1("sin", cplus_sin, sin, 1.0);
}
static void c14(void)  /* cos zero */
{
    volatile double x = 0.0;
    if (!same_d(cplus_cos(x), 1.0)) FIND("cos(0) not 1");
    DIFF1("cos", cplus_cos, cos, 0.0);
}
static void c15(void)  /* tan positive */
{
    DIFF1("tan", cplus_tan, tan, 0.5);
}
static void c16(void)  /* asin in range */
{
    DIFF1("asin", cplus_asin, asin, 0.5);
}
static void c17(void)  /* acos in range */
{
    DIFF1("acos", cplus_acos, acos, 0.5);
}
static void c18(void)  /* atan positive */
{
    DIFF1("atan", cplus_atan, atan, 1.0);
}
static void c19(void)  /* atan2 quadrants */
{
    DIFF2("atan2", cplus_atan2, atan2, 1.0, 1.0);
    DIFF2("atan2", cplus_atan2, atan2, -1.0, 1.0);
}
static void c20(void)  /* sinh positive */
{
    DIFF1("sinh", cplus_sinh, sinh, 1.0);
}
static void c21(void)  /* cosh positive */
{
    DIFF1("cosh", cplus_cosh, cosh, 1.0);
}
static void c22(void)  /* tanh positive */
{
    DIFF1("tanh", cplus_tanh, tanh, 1.0);
}
static void c23(void)  /* fmin NaN operand */
{
    DIFF2("fmin", cplus_fmin, fmin, NAN, 2.0);
    DIFF2("fmin", cplus_fmin, fmin, 2.0, NAN);
}
static void c24(void)  /* fmax signed zero */
{
    volatile double zp = 0.0, zn = -0.0;
    double r = cplus_fmax(zp, zn);
    if (isnan(r)) FIND("fmax(+0,-0) NaN");
    DIFF2("fmax", cplus_fmax, fmax, 0.0, -0.0);
}
static void c25(void)  /* fdim positive difference */
{
    DIFF2("fdim", cplus_fdim, fdim, 3.0, 2.0);
}
static void c26(void)  /* fma fused cancellation */
{
    volatile double x = 1.0 + 0x1.0p-52, y = 1.0 + 0x1.0p-52;
    volatile double z = -(1.0 + 0x1.0p-51), want = 0x1.0p-104;
    double r = cplus_fma(x, y, z);
    if (r != want) FIND("fma cancellation got %.17g want %.17g", r, (double)want);
    DIFF3("fma", cplus_fma, fma, x, y, z);
}
static void c27(void)  /* copysign sign from second */
{
    DIFF2("copysign", cplus_copysign, copysign, -1.0, 1.0);
    DIFF2("copysign", cplus_copysign, copysign, 1.0, -0.0);
}
static void c28(void)  /* frexp normal decomposition */
{
    diff_frexp("normal", 8.0);
}
static void c29(void)  /* modf normal split */
{
    diff_modf("normal", 8.25);
}
static void c30(void)  /* ldexp scale round-trip */
{
    int e = 0;
    double m = cplus_frexp(8.0, &e);
    if (!same_d(cplus_ldexp(m, e), 8.0)) FIND("ldexp/frexp round-trip failed");
    {
        volatile double vx = 3.0; volatile int ve = 4;
        double rw = cplus_ldexp(vx, ve), rh = ldexp(vx, ve);
        if (!same_d(rw, rh)) FIND("ldexp w=%.17g h=%.17g", rw, rh);
    }
}
static void c31(void)  /* remquo quotient out-param */
{
    diff_remquo("normal", 5.0, 3.0);
}
static void c32(void)  /* remainder normal */
{
    DIFF2("remainder", cplus_remainder, remainder, 5.0, 3.0);
}
static void c33(void)  /* nextafter positive adjacency */
{
    volatile double one = 1.0, two = 2.0;
    double r = cplus_nextafter(one, two), h = nextafter(one, two);
    if (r != h || !(r > one)) FIND("nextafter(1,2) w=%.17g h=%.17g", r, h);
    DIFF2("nextafter", cplus_nextafter, nextafter, 1.0, 2.0);
}
static void c34(void)  /* logb normal exponent */
{
    DIFF1("logb", cplus_logb, logb, 8.0);
}
static void c35(void)  /* ilogb normal exponent */
{
    diff_ilogb("normal", 8.0);
}
static void c36(void)  /* erf symmetry identity */
{
    volatile double one = 1.0;
    double e = cplus_erf(one), c = cplus_erfc(one);
    if (!same_d(e + c, 1.0)) FIND("erf(1)+erfc(1)=%.17g not 1", e + c);
    DIFF1("erf", cplus_erf, erf, 1.0);
    DIFF1("erf", cplus_erf, erf, -1.0);
}
static void c37(void)  /* erfc value and Inf */
{
    DIFF1("erfc", cplus_erfc, erfc, 1.0);
    DIFF1("erfc", cplus_erfc, erfc, INFINITY);
}
static void c38(void)  /* tgamma factorial */
{
    volatile double five = 5.0;
    if (!same_d(cplus_tgamma(five), 24.0)) FIND("tgamma(5) not 24");
    DIFF1("tgamma", cplus_tgamma, tgamma, 5.0);
}
static void c39(void)  /* lgamma log-factorial */
{
    volatile double five = 5.0;
    if (!same_d(cplus_lgamma(five), log(24.0))) FIND("lgamma(5) not log24");
    DIFF1("lgamma", cplus_lgamma, lgamma, 5.0);
}

/* --- float variants (40-47) --------------------------------------------- */
static void c40(void)  /* sqrtf positive and negative */
{
    DIFFF1("sqrtf", cplus_sqrtf, sqrtf, 2.0f);
    DIFFF1("sqrtf", cplus_sqrtf, sqrtf, -1.0f);
    DIFFF1("sqrtf", cplus_sqrtf, sqrtf, FLT_MAX);
}
static void c41(void)  /* fabsf negative zero */
{
    volatile float z = -0.0f;
    if (signbit(cplus_fabsf(z))) FIND("fabsf(-0) kept sign");
    DIFFF1("fabsf", cplus_fabsf, fabsf, -0.0f);
}
static void c42(void)  /* powf negative base domain */
{
    DIFFF2("powf", cplus_powf, powf, -2.0f, 0.5f);
}
static void c43(void)  /* expf overflow FLT_MAX */
{
    volatile float x = FLT_MAX;
    if (!isinf(cplus_expf(x))) FIND("expf(FLT_MAX) not +Inf");
    DIFFF1("expf", cplus_expf, expf, FLT_MAX);
}
static void c44(void)  /* logf zero domain */
{
    DIFFF1("logf", cplus_logf, logf, 0.0f);
}
static void c45(void)  /* fmaf accuracy */
{
    volatile float x = 1.0f + 0x1.0p-23f, y = 1.0f + 0x1.0p-23f;
    volatile float z = -(1.0f + 0x1.0p-22f);
    DIFFF3("fmaf", cplus_fmaf, fmaf, x, y, z);
}
static void c46(void)  /* nextafterf signed zero */
{
    volatile float zp = 0.0f, zn = -0.0f, one = 1.0f;
    float rw = cplus_nextafterf(zp, one), rh = nextafterf(zp, one);
    if (rw != rh || !(rw > 0.0f)) FIND("nextafterf(+0,1) w=%.9g h=%.9g", rw, rh);
    rw = cplus_nextafterf(zn, -one); rh = nextafterf(zn, -one);
    if (rw != rh || !signbit(rw)) FIND("nextafterf(-0,-1) sign lost");
}
static void c47(void)  /* erff value */
{
    DIFFF1("erff", cplus_erff, erff, 1.0f);
}

/* --- long double variants (48-55) --------------------------------------- */
static void c48(void)  /* sqrtl positive and negative */
{
    DIFFL1("sqrtl", cplus_sqrtl, sqrtl, 2.0L);
    DIFFL1("sqrtl", cplus_sqrtl, sqrtl, -1.0L);
}
static void c49(void)  /* fabsl negative */
{
    DIFFL1("fabsl", cplus_fabsl, fabsl, -2.5L);
}
static void c50(void)  /* powl positive base */
{
    DIFFL2("powl", cplus_powl, powl, 2.0L, 10.0L);
}
static void c51(void)  /* expl overflow */
{
    DIFFL1("expl", cplus_expl, expl, 1000.0L);
}
static void c52(void)  /* logl zero domain */
{
    DIFFL1("logl", cplus_logl, logl, 0.0L);
}
static void c53(void)  /* fmal accuracy */
{
    DIFFL3("fmal", cplus_fmal, fmal, 2.0L, 3.0L, 4.0L);
}
static void c54(void)  /* tgammal factorial */
{
    volatile long double five = 5.0L;
    if (!same_ld(cplus_tgammal(five), 24.0L)) FIND("tgammal(5) not 24");
    DIFFL1("tgammal", cplus_tgammal, tgammal, 5.0L);
}
static void c55(void)  /* nextafterl signed zero */
{
    volatile long double zp = 0.0L, one = 1.0L;
    long double rw = cplus_nextafterl(zp, one), rh = nextafterl(zp, one);
    if (rw != rh || !(rw > 0.0L)) FIND("nextafterl(+0,1) mismatch");
}

/* --- complex methods (56-67) -------------------------------------------- */
static void c56(void)  /* cabs finite magnitude */
{
    volatile double r = 3.0, i = 4.0;
    DIFFC1("cabs", cplus_cabs, cabs, r, i);
    {
        double complex z = 3.0 + 4.0 * I;
        if (!same_d(cplus_cabs(z), 5.0)) FIND("cabs(3+4i) not 5");
    }
}
static void c57(void)  /* carg phase angle */
{
    DIFFC1("carg", cplus_carg, carg, 1.0, 1.0);
}
static void c58(void)  /* creal extraction */
{
    double complex z = 3.0 - 4.0 * I;
    if (!same_d(cplus_creal(z), 3.0)) FIND("creal(3-4i) not 3");
    DIFFC1("creal", cplus_creal, creal, 3.0, -4.0);
}
static void c59(void)  /* cimag extraction */
{
    double complex z = 3.0 - 4.0 * I;
    if (!same_d(cplus_cimag(z), -4.0)) FIND("cimag(3-4i) not -4");
    DIFFC1("cimag", cplus_cimag, cimag, 3.0, -4.0);
}
static void c60(void)  /* conj conjugate */
{
    double complex z = 3.0 - 4.0 * I, r = cplus_conj(z);
    if (!same_d(creal(r), 3.0) || !same_d(cimag(r), 4.0))
        FIND("conj(3-4i) wrong");
    DIFFC1("conj", cplus_conj, conj, 3.0, -4.0);
}
static void c61(void)  /* cexp finite */
{
    DIFFC1("cexp", cplus_cexp, cexp, 0.5, 0.25);
}
static void c62(void)  /* csqrt finite */
{
    DIFFC1("csqrt", cplus_csqrt, csqrt, 3.0, 4.0);
}
static void c63(void)  /* csin finite */
{
    DIFFC1("csin", cplus_csin, csin, 0.5, 0.25);
}
static void c64(void)  /* ccos finite */
{
    DIFFC1("ccos", cplus_ccos, ccos, 0.5, 0.25);
}
static void c65(void)  /* clog finite */
{
    DIFFC1("clog", cplus_clog, clog, 3.0, 4.0);
}
static void c66(void)  /* cpow finite */
{
    DIFFC2("cpow", cplus_cpow, cpow, 2.0, 0.0, 3.0, 0.0);
}
static void c67(void)  /* cproj Inf projection */
{
    DIFFC1("cproj", cplus_cproj, cproj, INFINITY, 1.0);
}

/* --- special numeric classes (68-80) ------------------------------------ */
static void c68(void)  /* sqrt negative signed zero */
{
    volatile double z = -0.0;
    double r = cplus_sqrt(z);
    if (isnan(r)) FIND("sqrt(-0) NaN");
    DIFF1("sqrt", cplus_sqrt, sqrt, -0.0);
}
static void c69(void)  /* sqrt negative domain NaN */
{
    volatile double x = -1.0;
    if (!isnan(cplus_sqrt(x))) FIND("sqrt(-1) not NaN");
    DIFF1("sqrt", cplus_sqrt, sqrt, -1.0);
}
static void c70(void)  /* sqrt signaling NaN payload */
{
    double snan = bits_to_double(UINT64_C(0x7ff0000000000001));
    volatile double x = snan;
    double r = cplus_sqrt(x);
    if (!isnan(r)) FIND("sqrt(sNaN) not NaN");
    DIFF1("sqrt", cplus_sqrt, sqrt, snan);
}
static void c71(void)  /* sqrt subnormal input */
{
    DIFF1("sqrt", cplus_sqrt, sqrt, DBL_TRUE_MIN);
}
static void c72(void)  /* sqrt DBL_MAX */
{
    DIFF1("sqrt", cplus_sqrt, sqrt, DBL_MAX);
}
static void c73(void)  /* fmod divide by zero domain */
{
    DIFF2("fmod", cplus_fmod, fmod, 1.0, 0.0);
}
static void c74(void)  /* pow zero to zero */
{
    DIFF2("pow", cplus_pow, pow, 0.0, 0.0);
}
static void c75(void)  /* pow negative base fractional domain */
{
    DIFF2("pow", cplus_pow, pow, -2.0, 0.5);
}
static void c76(void)  /* exp overflow range */
{
    volatile double x = 1000.0;
    if (!isinf(cplus_exp(x))) FIND("exp(1000) not +Inf");
    DIFF1("exp", cplus_exp, exp, 1000.0);
}
static void c77(void)  /* exp underflow range */
{
    volatile double x = -1000.0;
    if (cplus_exp(x) != 0.0) FIND("exp(-1000) not +0");
    DIFF1("exp", cplus_exp, exp, -1000.0);
}
static void c78(void)  /* log negative domain */
{
    volatile double x = -1.0;
    if (!isnan(cplus_log(x))) FIND("log(-1) not NaN");
    DIFF1("log", cplus_log, log, -1.0);
}
static void c79(void)  /* acos out of domain */
{
    volatile double x = 2.0;
    if (!isnan(cplus_acos(x))) FIND("acos(2) not NaN");
    DIFF1("acos", cplus_acos, acos, 2.0);
}
static void c80(void)  /* tgamma pole at zero */
{
    DIFF1("tgamma", cplus_tgamma, tgamma, 0.0);
}

/* --- out-parameter guards and aliasing (81-84) --------------------------- */
static void c81(void)  /* frexp NULL out-param */
{
    errno = 0;
    if (cplus_frexp(8.0, NULL) != 0.0 || errno != EINVAL)
        FIND("frexp(x,NULL) guard errno=%d", errno);
}
static void c82(void)  /* modf NULL out-param */
{
    errno = 0;
    if (cplus_modf(8.0, NULL) != 0.0 || errno != EINVAL)
        FIND("modf(x,NULL) guard errno=%d", errno);
}
static void c83(void)  /* remquo NULL out-param */
{
    errno = 0;
    if (cplus_remquo(8.0, 3.0, NULL) != 0.0 || errno != EINVAL)
        FIND("remquo(x,y,NULL) guard errno=%d", errno);
}
static void c84(void)  /* modf aliased out-param */
{
    double aw = 8.25, ah = 8.25;
    double rw = cplus_modf(aw, &aw), rh = modf(ah, &ah);
    if (!same_d(rw, rh) || !same_d(aw, ah))
        FIND("aliased modf w=(%.17g,%.17g) h=(%.17g,%.17g)", rw, aw, rh, ah);
}

/* --- errno preservation (85-86) ----------------------------------------- */
static void c85(void)  /* errno preserved on success */
{
    volatile double x = 4.0, y = 2.0;
    int ew, eh;
    errno = SENT; (void)cplus_sqrt(x); ew = errno;
    errno = SENT; (void)sqrt(x);       eh = errno;
    if (ew != eh) FIND("sqrt success errno w=%d h=%d", ew, eh);
    errno = SENT; (void)cplus_pow(x, y); ew = errno;
    errno = SENT; (void)pow(x, y);       eh = errno;
    if (ew != eh) FIND("pow success errno w=%d h=%d", ew, eh);
    errno = SENT; (void)cplus_floor(x); ew = errno;
    errno = SENT; (void)floor(x);       eh = errno;
    if (ew != eh) FIND("floor success errno w=%d h=%d", ew, eh);
}
static void c86(void)  /* errno set on domain error */
{
    volatile double x = -1.0;
    int ew, eh;
    feclearexcept(FE_ALL_EXCEPT); errno = SENT; (void)cplus_sqrt(x); ew = errno;
    feclearexcept(FE_ALL_EXCEPT); errno = SENT; (void)sqrt(x);       eh = errno;
    if (ew != eh) FIND("sqrt(-1) errno w=%d h=%d", ew, eh);
}

/* --- FP environment preservation (87-89) -------------------------------- */
static void c87(void)  /* rounding mode not disturbed (FE_UPWARD) */
{
    fenv_t saved;
    volatile double x = 1.234567;
    int rw, rh, fw, fh;
    if (fegetenv(&saved) != 0) { FIND("fegetenv failed"); return; }
    if (fesetround(FE_UPWARD) != 0) { (void)fesetenv(&saved); return; }
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;
    { volatile double v = cplus_sqrt(x); (void)v; }
    rw = fegetround(); fw = fetestexcept(FE_ALL_EXCEPT);
    feclearexcept(FE_ALL_EXCEPT); (void)fesetround(FE_UPWARD); errno = SENT;
    { volatile double v = sqrt(x); (void)v; }
    rh = fegetround(); fh = fetestexcept(FE_ALL_EXCEPT);
    if (rw != rh) FIND("FE_UPWARD rounding w=%d h=%d", rw, rh);
    if (fw != fh) FIND("FE_UPWARD flags w=0x%x h=0x%x", fw, fh);
    (void)fesetenv(&saved);
}
static void c88(void)  /* rounding mode not disturbed (FE_DOWNWARD) */
{
    fenv_t saved;
    volatile double x = 2.5;
    int rw, rh, fw, fh;
    if (fegetenv(&saved) != 0) { FIND("fegetenv failed"); return; }
    if (fesetround(FE_DOWNWARD) != 0) { (void)fesetenv(&saved); return; }
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;
    { volatile double v = cplus_exp(x); (void)v; }
    rw = fegetround(); fw = fetestexcept(FE_ALL_EXCEPT);
    feclearexcept(FE_ALL_EXCEPT); (void)fesetround(FE_DOWNWARD); errno = SENT;
    { volatile double v = exp(x); (void)v; }
    rh = fegetround(); fh = fetestexcept(FE_ALL_EXCEPT);
    if (rw != rh) FIND("FE_DOWNWARD rounding w=%d h=%d", rw, rh);
    if (fw != fh) FIND("FE_DOWNWARD flags w=0x%x h=0x%x", fw, fh);
    (void)fesetenv(&saved);
}
static void c89(void)  /* pre-existing exception flags preserved */
{
    fenv_t saved;
    volatile double x = 4.0;
    int fw, fh;
    if (fegetenv(&saved) != 0) { FIND("fegetenv failed"); return; }
    feclearexcept(FE_ALL_EXCEPT); (void)feraiseexcept(FE_INVALID); errno = SENT;
    { volatile double v = cplus_sqrt(x); (void)v; }
    fw = fetestexcept(FE_ALL_EXCEPT);
    feclearexcept(FE_ALL_EXCEPT); (void)feraiseexcept(FE_INVALID); errno = SENT;
    { volatile double v = sqrt(x); (void)v; }
    fh = fetestexcept(FE_ALL_EXCEPT);
    if (fw != fh) FIND("invalid flag w=0x%x h=0x%x", fw, fh);
    (void)fesetenv(&saved);
}

/* --- logging OFF/ON parity (90-91) -------------------------------------- */
static void c90(void)  /* results identical with logging on/off */
{
    volatile double x = 2.5;
    double off[4], on[4];
    int e, eo[4], en[4], i;
    cplus_libc_log_set_enabled(0);
    off[0] = cplus_sqrt(x);        eo[0] = errno;
    off[1] = cplus_log(x);         eo[1] = errno;
    off[2] = cplus_pow(x, x);      eo[2] = errno;
    off[3] = cplus_frexp(x, &e);   eo[3] = errno;
    cplus_libc_log_set_enabled(1);
    on[0] = cplus_sqrt(x);         en[0] = errno;
    on[1] = cplus_log(x);          en[1] = errno;
    on[2] = cplus_pow(x, x);       en[2] = errno;
    on[3] = cplus_frexp(x, &e);    en[3] = errno;
    cplus_libc_log_set_enabled(0);
    for (i = 0; i < 4; i++) {
        if (!same_d(off[i], on[i]))
            FIND("logging result[%d] off=%.17g on=%.17g", i, off[i], on[i]);
        if (eo[i] != en[i]) FIND("logging errno[%d] off=%d on=%d", i, eo[i], en[i]);
    }
}
static void c91(void)  /* out-params identical with logging on/off */
{
    volatile double x = 8.25;
    double iw = 0.0, ih = 0.0, rw, rh;
    int qw = 0, qh = 0, eo, en;
    cplus_libc_log_set_enabled(0);
    rw = cplus_modf(x, &iw); eo = errno;
    (void)cplus_remquo(8.0, 3.0, &qw);
    cplus_libc_log_set_enabled(1);
    rh = cplus_modf(x, &ih); en = errno;
    (void)cplus_remquo(8.0, 3.0, &qh);
    cplus_libc_log_set_enabled(0);
    if (!same_d(rw, rh) || !same_d(iw, ih))
        FIND("logging modf off=(%.17g,%.17g) on=(%.17g,%.17g)", rw, iw, rh, ih);
    if (eo != en) FIND("logging modf errno off=%d on=%d", eo, en);
    if (qw != qh) FIND("logging remquo quotient off=%d on=%d", qw, qh);
}

/* --- additional distinct class scenarios (92-100) ----------------------- */
static void c92(void)  /* fmin NaN both operands */
{
    DIFF2("fmin", cplus_fmin, fmin, NAN, NAN);
}
static void c93(void)  /* fmax Inf and NaN */
{
    DIFF2("fmax", cplus_fmax, fmax, INFINITY, NAN);
}
static void c94(void)  /* copysign NaN sign from second */
{
    DIFF2("copysign", cplus_copysign, copysign, NAN, -1.0);
}
static void c95(void)  /* nextafter subnormal step */
{
    DIFF2("nextafter", cplus_nextafter, nextafter, DBL_TRUE_MIN, 0.0);
}
static void c96(void)  /* logb zero and Inf specials */
{
    DIFF1("logb", cplus_logb, logb, 0.0);
    DIFF1("logb", cplus_logb, logb, INFINITY);
}
static void c97(void)  /* ilogb zero and NaN specials */
{
    diff_ilogb("zero", 0.0);
    diff_ilogb("nan", NAN);
}
static void c98(void)  /* erf Inf and NaN */
{
    DIFF1("erf", cplus_erf, erf, INFINITY);
    DIFF1("erf", cplus_erf, erf, NAN);
}
static void c99(void)  /* lgamma negative integer pole */
{
    DIFF1("lgamma", cplus_lgamma, lgamma, -1.0);
    DIFF1("lgamma", cplus_lgamma, lgamma, -2.0);
}
static void c100(void) /* remquo divide by zero domain */
{
    diff_remquo("div0", 1.0, 0.0);
}

/* ===================== dispatcher ======================================== */
typedef void (*case_fn)(void);
typedef struct { const char *method; const char *scenario; case_fn fn; } Case;

static const Case g_cases[] = {
    { "sqrt",      "positive",                 c01 },
    { "fabs",      "negative-value",           c02 },
    { "floor",     "positive-half",            c03 },
    { "ceil",      "negative-half",            c04 },
    { "round",     "halfway-ties-away",        c05 },
    { "trunc",     "negative-toward-zero",     c06 },
    { "fmod",      "positive-division",        c07 },
    { "pow",       "positive-base-exponent",   c08 },
    { "exp",       "positive-one",             c09 },
    { "log",       "one-exact-zero",           c10 },
    { "log10",     "positive-value",           c11 },
    { "log2",      "power-of-eight",           c12 },
    { "sin",       "positive-quadrant",        c13 },
    { "cos",       "zero-exact-one",           c14 },
    { "tan",       "positive-value",           c15 },
    { "asin",      "in-range",                 c16 },
    { "acos",      "in-range",                 c17 },
    { "atan",      "positive-value",           c18 },
    { "atan2",     "quadrant-signed",          c19 },
    { "sinh",      "positive-value",           c20 },
    { "cosh",      "positive-value",           c21 },
    { "tanh",      "positive-value",           c22 },
    { "fmin",      "nan-operand",              c23 },
    { "fmax",      "signed-zero",              c24 },
    { "fdim",      "positive-difference",      c25 },
    { "fma",       "fused-cancellation",       c26 },
    { "copysign",  "sign-from-second",         c27 },
    { "frexp",     "normal-decomposition",     c28 },
    { "modf",      "normal-split",             c29 },
    { "ldexp",     "scale-roundtrip",          c30 },
    { "remquo",    "quotient-outparam",        c31 },
    { "remainder", "normal-reduction",         c32 },
    { "nextafter", "positive-adjacency",       c33 },
    { "logb",      "normal-exponent",          c34 },
    { "ilogb",     "normal-exponent",          c35 },
    { "erf",       "symmetry-identity",        c36 },
    { "erfc",      "value-and-inf",            c37 },
    { "tgamma",    "factorial",                c38 },
    { "lgamma",    "log-factorial",            c39 },
    { "sqrtf",     "positive-and-negative",    c40 },
    { "fabsf",     "negative-zero",            c41 },
    { "powf",      "negative-base-domain",     c42 },
    { "expf",      "overflow-FLT_MAX",         c43 },
    { "logf",      "zero-domain",              c44 },
    { "fmaf",      "accuracy",                 c45 },
    { "nextafterf","signed-zero",              c46 },
    { "erff",      "value",                    c47 },
    { "sqrtl",     "positive-and-negative",    c48 },
    { "fabsl",     "negative-value",           c49 },
    { "powl",      "positive-base",            c50 },
    { "expl",      "overflow",                 c51 },
    { "logl",      "zero-domain",              c52 },
    { "fmal",      "accuracy",                 c53 },
    { "tgammal",   "factorial",                c54 },
    { "nextafterl","signed-zero",              c55 },
    { "cabs",      "finite-magnitude",         c56 },
    { "carg",      "phase-angle",              c57 },
    { "creal",     "real-part",                c58 },
    { "cimag",     "imag-part",                c59 },
    { "conj",      "conjugate",                c60 },
    { "cexp",      "finite-exponential",       c61 },
    { "csqrt",     "finite-sqrt",              c62 },
    { "csin",      "finite-sine",              c63 },
    { "ccos",      "finite-cosine",            c64 },
    { "clog",      "finite-log",               c65 },
    { "cpow",      "finite-power",             c66 },
    { "cproj",     "infinity-projection",      c67 },
    { "sqrt",      "negative-signed-zero",     c68 },
    { "sqrt",      "negative-domain-nan",      c69 },
    { "sqrt",      "signaling-nan-payload",    c70 },
    { "sqrt",      "subnormal-input",          c71 },
    { "sqrt",      "DBL_MAX-input",            c72 },
    { "fmod",      "divide-by-zero-domain",    c73 },
    { "pow",       "zero-to-zero",             c74 },
    { "pow",       "negative-fractional",      c75 },
    { "exp",       "overflow-range",           c76 },
    { "exp",       "underflow-range",          c77 },
    { "log",       "negative-domain",          c78 },
    { "acos",      "out-of-domain",            c79 },
    { "tgamma",    "pole-at-zero",             c80 },
    { "frexp",     "NULL-out-param",           c81 },
    { "modf",      "NULL-out-param",           c82 },
    { "remquo",    "NULL-out-param",           c83 },
    { "modf",      "aliased-out-param",        c84 },
    { "errno",     "success-preserved",        c85 },
    { "errno",     "domain-error-set",         c86 },
    { "fenv",      "FE_UPWARD-rounding",       c87 },
    { "fenv",      "FE_DOWNWARD-rounding",     c88 },
    { "fenv",      "invalid-flag-preserved",   c89 },
    { "logging",   "off-on-result-parity",     c90 },
    { "logging",   "off-on-outparam-parity",   c91 },
    { "fmin",      "nan-both-operands",        c92 },
    { "fmax",      "inf-and-nan",              c93 },
    { "copysign",  "nan-sign",                 c94 },
    { "nextafter", "subnormal-step",           c95 },
    { "logb",      "zero-and-inf-specials",    c96 },
    { "ilogb",     "zero-and-nan-specials",    c97 },
    { "erf",       "inf-and-nan",              c98 },
    { "lgamma",    "negative-integer-pole",    c99 },
    { "remquo",    "divide-by-zero-domain",    c100 }
};

int main(void)
{
    const size_t n = sizeof g_cases / sizeof g_cases[0];
    fenv_t saved_env;
    int saved_errno = errno;
    size_t i;

    if (fegetenv(&saved_env) != 0) { FIND("fegetenv failed in main"); return 1; }
    cplus_libc_log_set_enabled(0);

    for (i = 0; i < n; i++) {
        fenv_t per;
        g_case = (int)i + 1;
        g_method = g_cases[i].method;
        g_scenario = g_cases[i].scenario;
        g_current_fail = 0;
        if (fegetenv(&per) != 0) { FIND("fegetenv failed before case"); continue; }
        g_cases[i].fn();
        (void)fesetenv(&per);          /* keep scenarios independent */
        errno = saved_errno;
        if (!g_current_fail) g_pass++;
        printf("hundred_math: case %d %s %s %s\n", g_case, g_method, g_scenario,
               g_current_fail ? "FINDING" : "ok");
    }

    (void)fesetenv(&saved_env);        /* restore the caller's FP environment */
    errno = saved_errno;
    cplus_libc_log_set_enabled(0);

    if (g_findings)
        fprintf(stderr, "hundred_math: %d finding(s) reported\n", g_findings);
    printf("hundred_math: %d/100 scenarios ok\n", g_pass);
    return 0;
}
