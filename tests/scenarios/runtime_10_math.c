/* Runtime math differential scenarios for the C+ hardened libc wrappers.
 *
 * Standalone C99.  Every cplus_* math wrapper is compared against the same
 * HOST libm function called from this translation unit.  We compare the
 * NaN/Inf/signed-zero class, the finite value (with tolerance), errno and the
 * FP exception/rounding state.  Findings are printed with a "FINDING:" prefix;
 * the suite still exits 0 so the owning harness can collect findings.
 *
 * FP environment: each scenario's incoming fenv_t and errno are saved and
 * restored by the dispatcher, and every state-mutating probe restores the
 * environment it installed.  No genuine UB (no integer casts of NaN/Inf, no
 * integer division by zero, and no raw call that would crash).
 */
#include "libc/install-include/cplus_libc.h"

#include <errno.h>
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <complex.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define SENT 424242

static int g_findings;

#define FIND(...) do {                                                        \
    fprintf(stderr, "FINDING: ");                                             \
    fprintf(stderr, __VA_ARGS__);                                             \
    fputc('\n', stderr);                                                      \
    g_findings++;                                                             \
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

/* ---- differential macros: wrapper vs host ------------------------------- */
#define DIFF1(label, w, h, x) do {                                            \
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
        FIND("%s(%s,%s,%s): class/value w=%.17g h=%.17g", label, #x, #y, #z,  \
             (double)rw_, (double)rh_);                                       \
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
    /* C99 complex layout == two adjacent real components; no FP ops here so  \
     * operand construction cannot leak flags into the differential. */      \
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
        FIND("%s(%s,%s): complex class/value w=(%.17g,%.17g) h=(%.17g,%.17g)", \
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
        FIND("%s: complex class/value w=(%.17g,%.17g) h=(%.17g,%.17g)",       \
             label, creal(rw_), cimag(rw_), creal(rh_), cimag(rh_));          \
    if (ew_ != eh_) FIND("%s: errno w=%d h=%d", label, ew_, eh_);             \
    if (fw_ != fh_) FIND("%s: FP flags w=0x%x h=0x%x", label, fw_, fh_);      \
} while (0)

/* ===================== individual scenarios ============================= */

/* sqrt of positive finite values: exact spot checks + differential. */
static void sc_10_sqrt_positive(void)
{
    volatile double x = 9.0;
    double r = cplus_sqrt(x);
    if (!same_d(r, 3.0)) FIND("sqrt(9)=%.17g expected 3", r);
    DIFF1("sqrt", cplus_sqrt, sqrt, 4.0);
    DIFF1("sqrt", cplus_sqrt, sqrt, 2.0);
    DIFF1("sqrt", cplus_sqrt, sqrt, 0.25);
    DIFF1("sqrt", cplus_sqrt, sqrt, DBL_MAX);
}

/* sqrt of negative values: NaN class, errno and FP flags must track host. */
static void sc_10_sqrt_negative(void)
{
    volatile double x = -1.0;
    double r = cplus_sqrt(x);
    if (!isnan(r)) FIND("sqrt(-1)=%.17g expected NaN", r);
    DIFF1("sqrt", cplus_sqrt, sqrt, -1.0);
    DIFF1("sqrt", cplus_sqrt, sqrt, -0.25);
    DIFF1("sqrt", cplus_sqrt, sqrt, -DBL_MAX);
}

/* sqrt of signed zero must preserve the sign of zero. */
static void sc_10_sqrt_signed_zero(void)
{
    volatile double zp = 0.0, zn = -0.0;
    volatile double r = cplus_sqrt(zn);
    if (!isinf(r) && !signbit(r)) FIND("sqrt(-0) lost negative-zero sign");
    DIFF1("sqrt(+0)", cplus_sqrt, sqrt, zp);
    DIFF1("sqrt(-0)", cplus_sqrt, sqrt, zn);
}

/* log family domains: log(1)=0, log(0)=-Inf, log(-1)=NaN, log2/log10/log1p. */
static void sc_10_log_domains(void)
{
    DIFF1("log(1)", cplus_log, log, 1.0);
    DIFF1("log(0)", cplus_log, log, 0.0);
    DIFF1("log(-1)", cplus_log, log, -1.0);
    DIFF1("log10(1)", cplus_log10, log10, 1.0);
    DIFF1("log10(-1)", cplus_log10, log10, -1.0);
    DIFF1("log2(8)", cplus_log2, log2, 8.0);
    DIFF1("log1p(0)", cplus_log1p, log1p, 0.0);
    DIFF1("log1p(-1)", cplus_log1p, log1p, -1.0);
}

/* exp overflow / range errors: must raise the same flags and errno as host. */
static void sc_10_exp_overflow(void)
{
    volatile double x = 1000.0;
    double r = cplus_exp(x);
    if (!isinf(r)) FIND("exp(1000)=%.17g expected +Inf", r);
    DIFF1("exp(1000)", cplus_exp, exp, 1000.0);
    DIFF1("exp(1)", cplus_exp, exp, 1.0);
    DIFF1("exp2(2000)", cplus_exp2, exp2, 2000.0);
    DIFF1("expm1(1000)", cplus_expm1, expm1, 1000.0);
}

/* pow with a negative base and fractional exponent (domain error). */
static void sc_10_pow_negative_base(void)
{
    DIFF2("pow", cplus_pow, pow, -2.0, 0.5);
    DIFF2("pow", cplus_pow, pow, -2.0, 3.0);
    DIFF2("pow", cplus_pow, pow, 2.0, -3.0);
    DIFF2("pow", cplus_pow, pow, 0.0, 0.0);
}

/* fmod by zero must produce NaN (host-defined domain error), not crash. */
static void sc_10_fmod_by_zero(void)
{
    DIFF2("fmod", cplus_fmod, fmod, 1.0, 0.0);
    DIFF2("fmod", cplus_fmod, fmod, 5.5, 2.0);
    DIFF2("fmod", cplus_fmod, fmod, -5.5, 2.0);
}

/* floor/ceil/round/trunc on positive, negative and half values. */
static void sc_10_rounding_modes(void)
{
    static const double vals[] = { 2.3, -2.3, 2.5, -2.5, 0.5, -0.5, 3.0, -3.0 };
    size_t i;
    for (i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        volatile double v = vals[i];
        DIFF1("floor", cplus_floor, floor, v);
        DIFF1("ceil", cplus_ceil, ceil, v);
        DIFF1("round", cplus_round, round, v);
        DIFF1("trunc", cplus_trunc, trunc, v);
    }
}

/* fabs of signed zero and negative values: sign must be cleared. */
static void sc_10_fabs_signed_zero(void)
{
    volatile double zn = -0.0, neg = -3.5;
    volatile double r = cplus_fabs(zn);
    if (signbit(r)) FIND("fabs(-0) kept negative sign");
    DIFF1("fabs(-0)", cplus_fabs, fabs, zn);
    DIFF1("fabs(-3.5)", cplus_fabs, fabs, neg);
}

/* fmin/fmax with NaN and with signed zero (IEEE select rules). */
static void sc_10_fmin_fmax_nan(void)
{
    volatile double nan = NAN, zp = 0.0, zn = -0.0;
    DIFF2("fmin", cplus_fmin, fmin, nan, 2.0);
    DIFF2("fmin", cplus_fmin, fmin, 2.0, nan);
    DIFF2("fmax", cplus_fmax, fmax, nan, 2.0);
    DIFF2("fmax", cplus_fmax, fmax, 2.0, nan);
    DIFF2("fmin(+0,-0)", cplus_fmin, fmin, zp, zn);
    DIFF2("fmax(+0,-0)", cplus_fmax, fmax, zp, zn);
}

/* fma accuracy: naive x*y+z cancels to zero, fused result must be 2^-104. */
static void sc_10_fma_accuracy(void)
{
    volatile double x = 1.0 + 0x1.0p-52;
    volatile double y = 1.0 + 0x1.0p-52;
    volatile double z = -(1.0 + 0x1.0p-51);
    volatile double expected = 0x1.0p-104;
    volatile double r = cplus_fma(x, y, z);
    if (r != expected)
        FIND("fma accuracy: got %.17g expected %.17g (naive x*y+z=%g)",
             r, (double)expected, (double)(x * y + z));
    DIFF3("fma", cplus_fma, fma, x, y, z);
    DIFF3("fma", cplus_fma, fma, 3.0, 4.0, 5.0);
}

/* remainder and remquo, including the integer quotient out-parameter. */
static void sc_10_remainder_remquo(void)
{
    int qw = -99, qh = -99;
    volatile double x = 5.0, y = 3.0;
    double rw, rh;
    int ew, eh, fw, fh;
    DIFF2("remainder", cplus_remainder, remainder, 5.0, 3.0);
    DIFF2("remainder", cplus_remainder, remainder, 5.0, 0.0);

    feclearexcept(FE_ALL_EXCEPT); errno = SENT;
    rw = cplus_remquo(x, y, &qw);
    ew = errno; fw = fetestexcept(FE_ALL_EXCEPT);
    feclearexcept(FE_ALL_EXCEPT); errno = SENT;
    rh = remquo(x, y, &qh);
    eh = errno; fh = fetestexcept(FE_ALL_EXCEPT);
    if (!same_d(rw, rh)) FIND("remquo value w=%.17g h=%.17g", rw, rh);
    if (qw != qh) FIND("remquo quotient w=%d h=%d", qw, qh);
    if (ew != eh) FIND("remquo errno w=%d h=%d", ew, eh);
    if (fw != fh) FIND("remquo FP flags w=0x%x h=0x%x", fw, fh);
}

/* ldexp/frexp/modf round-trips and out-parameters. */
static void sc_10_ldexp_frexp_modf(void)
{
    int ew = -99, eh = -99;
    double mw, mh, iw, ih;
    volatile double v = 8.0;

    feclearexcept(FE_ALL_EXCEPT); errno = SENT;
    mw = cplus_frexp(v, &ew);
    { int e1 = errno, f1 = fetestexcept(FE_ALL_EXCEPT);
      feclearexcept(FE_ALL_EXCEPT); errno = SENT;
      mh = frexp(v, &eh);
      { int e2 = errno, f2 = fetestexcept(FE_ALL_EXCEPT);
        if (!same_d(mw, mh) || ew != eh)
            FIND("frexp w=(%.17g,%d) h=(%.17g,%d)", mw, ew, mh, eh);
        if (e1 != e2 || f1 != f2)
            FIND("frexp errno/flags w=%d/0x%x h=%d/0x%x", e1, f1, e2, f2);
      }
    }
    if (!same_d(cplus_ldexp(mw, ew), v)) FIND("ldexp/frexp round-trip failed");

    mw = cplus_modf(8.25, &iw);
    mh = modf(8.25, &ih);
    if (!same_d(mw, mh) || !same_d(iw, ih))
        FIND("modf w=(%.17g,%.17g) h=(%.17g,%.17g)", mw, iw, mh, ih);
}

/* NULL out-params are guarded: return 0 and set EINVAL instead of crashing. */
static void sc_10_null_outparam_guard(void)
{
    errno = 0;
    if (cplus_frexp(8.0, NULL) != 0.0 || errno != EINVAL)
        FIND("frexp(x,NULL): retval/errno mismatch errno=%d", errno);
    errno = 0;
    if (cplus_modf(8.0, NULL) != 0.0 || errno != EINVAL)
        FIND("modf(x,NULL): retval/errno mismatch errno=%d", errno);
    errno = 0;
    if (cplus_remquo(8.0, 3.0, NULL) != 0.0 || errno != EINVAL)
        FIND("remquo(x,y,NULL): retval/errno mismatch errno=%d", errno);

    errno = 0;
    if (cplus_frexpf(8.0f, NULL) != 0.0f || errno != EINVAL)
        FIND("frexpf(x,NULL): retval/errno mismatch errno=%d", errno);
    errno = 0;
    if (cplus_modff(8.0f, NULL) != 0.0f || errno != EINVAL)
        FIND("modff(x,NULL): retval/errno mismatch errno=%d", errno);
    errno = 0;
    if (cplus_remquof(8.0f, 3.0f, NULL) != 0.0f || errno != EINVAL)
        FIND("remquof(x,y,NULL): retval/errno mismatch errno=%d", errno);

    errno = 0;
    if (cplus_frexpl(8.0L, NULL) != 0.0L || errno != EINVAL)
        FIND("frexpl(x,NULL): retval/errno mismatch errno=%d", errno);
    errno = 0;
    if (cplus_modfl(8.0L, NULL) != 0.0L || errno != EINVAL)
        FIND("modfl(x,NULL): retval/errno mismatch errno=%d", errno);
    errno = 0;
    if (cplus_remquol(8.0L, 3.0L, NULL) != 0.0L || errno != EINVAL)
        FIND("remquol(x,y,NULL): retval/errno mismatch errno=%d", errno);
}

/* nan/nanf/nanl with tags: NaN class plus errno/flags parity with host. */
static void sc_10_nan_tags(void)
{
    static const char *const tags[] = { "", "0x1234", "1234", "0x7fc00001" };
    size_t i;
    for (i = 0; i < sizeof tags / sizeof tags[0]; i++) {
        double w, h; float wf, hf; long double wl, hl;
        int ew, eh, ewf, ehf, ewl, ehl, fw, fh, fwf, fhf, fwl, fhl;
        feclearexcept(FE_ALL_EXCEPT); errno = SENT; w = cplus_nan(tags[i]);
        ew = errno; fw = fetestexcept(FE_ALL_EXCEPT);
        feclearexcept(FE_ALL_EXCEPT); errno = SENT; h = nan(tags[i]);
        eh = errno; fh = fetestexcept(FE_ALL_EXCEPT);
        if (!isnan(w) || !isnan(h)) FIND("nan(%s): not NaN w/h=%d/%d", tags[i], isnan(w), isnan(h));
        if (ew != eh || fw != fh) FIND("nan(%s): errno/flags w=%d/0x%x h=%d/0x%x", tags[i], ew, fw, eh, fh);

        feclearexcept(FE_ALL_EXCEPT); errno = SENT; wf = cplus_nanf(tags[i]);
        ewf = errno; fwf = fetestexcept(FE_ALL_EXCEPT);
        feclearexcept(FE_ALL_EXCEPT); errno = SENT; hf = nanf(tags[i]);
        ehf = errno; fhf = fetestexcept(FE_ALL_EXCEPT);
        if (!isnan(wf) || !isnan(hf)) FIND("nanf(%s): not NaN", tags[i]);
        if (ewf != ehf || fwf != fhf)
            FIND("nanf(%s): errno/flags w=%d/0x%x h=%d/0x%x", tags[i], ewf, fwf, ehf, fhf);

        feclearexcept(FE_ALL_EXCEPT); errno = SENT; wl = cplus_nanl(tags[i]);
        ewl = errno; fwl = fetestexcept(FE_ALL_EXCEPT);
        feclearexcept(FE_ALL_EXCEPT); errno = SENT; hl = nanl(tags[i]);
        ehl = errno; fhl = fetestexcept(FE_ALL_EXCEPT);
        if (!isnan(wl) || !isnan(hl)) FIND("nanl(%s): not NaN", tags[i]);
        if (ewl != ehl || fwl != fhl)
            FIND("nanl(%s): errno/flags w=%d/0x%x h=%d/0x%x", tags[i], ewl, fwl, ehl, fhl);
    }
}

/* nextafter adjacency: one ULP away, including signed zero and subnormals. */
static void sc_10_nextafter_adjacency(void)
{
    volatile double zp = 0.0, zn = -0.0, one = 1.0;
    volatile float zf = 0.0f, of = 1.0f;
    volatile long double zl = 0.0L, ol = 1.0L;
    double w, h; float wf, hf; long double wl, hl;

    w = cplus_nextafter(zp, one); h = nextafter(zp, one);
    if (w != h || !(w > 0.0) || w >= DBL_MIN) FIND("nextafter(+0,1)=%.17g host=%.17g", w, h);
    w = cplus_nextafter(one, zp); h = nextafter(one, zp);
    if (w != h || !(w < 1.0)) FIND("nextafter(1,0)=%.17g host=%.17g", w, h);
    w = cplus_nextafter(one, 2.0); h = nextafter(one, 2.0);
    if (w != h || !(w > 1.0)) FIND("nextafter(1,2)=%.17g host=%.17g", w, h);
    w = cplus_nextafter(zn, -1.0); h = nextafter(zn, -1.0);
    if (w != h || !signbit(w)) FIND("nextafter(-0,-1) sign lost");

    wf = cplus_nextafterf(zf, of); hf = nextafterf(zf, of);
    if (wf != hf || !(wf > 0.0f)) FIND("nextafterf(+0,1) mismatch");
    wl = cplus_nextafterl(zl, ol); hl = nextafterl(zl, ol);
    if (wl != hl || !(wl > 0.0L)) FIND("nextafterl(+0,1) mismatch");

    DIFF2("nextafter", cplus_nextafter, nextafter, 1.0, 2.0);
}

/* ilogb/logb including FP_ILOGB0/INT_MAX/FP_ILOGBNAN special values. */
static void sc_10_ilogb_logb(void)
{
    volatile double vals[] = { 8.0, 0.0, INFINITY, NAN, -4.0 };
    size_t i;
    for (i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        volatile double v = vals[i];
        int iw, ih, ew, eh, fw, fh;
        DIFF1("logb", cplus_logb, logb, v);
        feclearexcept(FE_ALL_EXCEPT); errno = SENT; iw = cplus_ilogb(v);
        ew = errno; fw = fetestexcept(FE_ALL_EXCEPT);
        feclearexcept(FE_ALL_EXCEPT); errno = SENT; ih = ilogb(v);
        eh = errno; fh = fetestexcept(FE_ALL_EXCEPT);
        if (iw != ih) FIND("ilogb input %d: w=%d h=%d", (int)i, iw, ih);
        if (ew != eh || fw != fh) FIND("ilogb input %d: errno/flags w=%d/0x%x h=%d/0x%x",
                                      (int)i, ew, fw, eh, fh);
    }
    DIFFF1("logbf", cplus_logbf, logbf, 8.0f);
    DIFFL1("logbl", cplus_logbl, logbl, 8.0L);
}

/* erf/erfc values, symmetry and the erfc+erf == 1 identity. */
static void sc_10_erf_erfc(void)
{
    volatile double one = 1.0;
    double e = cplus_erf(one), c = cplus_erfc(one);
    if (!same_d(e + c, 1.0)) FIND("erf(1)+erfc(1)=%.17g expected 1", e + c);
    DIFF1("erf(0)", cplus_erf, erf, 0.0);
    DIFF1("erf(1)", cplus_erf, erf, 1.0);
    DIFF1("erf(-1)", cplus_erf, erf, -1.0);
    DIFF1("erfc(1)", cplus_erfc, erfc, 1.0);
    DIFF1("erfc(Inf)", cplus_erfc, erfc, INFINITY);
}

/* tgamma/lgamma on positive integers, poles and negative integers. */
static void sc_10_tgamma_lgamma(void)
{
    volatile double five = 5.0;
    double g = cplus_tgamma(five), l = cplus_lgamma(five);
    if (!same_d(g, 24.0)) FIND("tgamma(5)=%.17g expected 24", g);
    if (!same_d(l, log(24.0))) FIND("lgamma(5)=%.17g expected log(24)", l);
    DIFF1("tgamma", cplus_tgamma, tgamma, five);
    DIFF1("lgamma", cplus_lgamma, lgamma, five);
    DIFF1("tgamma(0)", cplus_tgamma, tgamma, 0.0);
    DIFF1("lgamma(0)", cplus_lgamma, lgamma, 0.0);
    DIFF1("tgamma(-1)", cplus_tgamma, tgamma, -1.0);
    DIFF1("lgamma(-1)", cplus_lgamma, lgamma, -1.0);
}

/* Float variants of the key functions: domains, rounding and special values. */
static void sc_10_float_variants(void)
{
    volatile float x = 2.5f, y = 3.0f, neg = -2.5f;
    DIFFF1("sqrtf", cplus_sqrtf, sqrtf, x);
    DIFFF1("sqrtf(-1)", cplus_sqrtf, sqrtf, neg);
    DIFFF1("logf", cplus_logf, logf, x);
    DIFFF1("logf(0)", cplus_logf, logf, 0.0f);
    DIFFF1("expf", cplus_expf, expf, x);
    DIFFF2("powf", cplus_powf, powf, x, y);
    DIFFF2("powf(-2,.5)", cplus_powf, powf, -2.0f, 0.5f);
    DIFFF1("floorf", cplus_floorf, floorf, neg);
    DIFFF1("ceilf", cplus_ceilf, ceilf, neg);
    DIFFF1("roundf", cplus_roundf, roundf, x);
    DIFFF1("truncf", cplus_truncf, truncf, neg);
    DIFFF1("fabsf", cplus_fabsf, fabsf, neg);
    DIFFF2("fminf", cplus_fminf, fminf, x, y);
    DIFFF2("fmaxf", cplus_fmaxf, fmaxf, x, y);
    DIFFF2("fmodf", cplus_fmodf, fmodf, x, y);
    DIFFF3("fmaf", cplus_fmaf, fmaf, x, y, 1.0f);
    DIFFF2("nextafterf", cplus_nextafterf, nextafterf, x, y);
    DIFFF1("erff", cplus_erff, erff, x);
    DIFFF1("erfcf", cplus_erfcf, erfcf, x);
    DIFFF1("tgammaf", cplus_tgammaf, tgammaf, 5.0f);
    DIFFF1("lgammaf", cplus_lgammaf, lgammaf, 5.0f);
}

/* Long double variants of the key functions. */
static void sc_10_long_double_variants(void)
{
    volatile long double x = 2.5L, y = 3.0L, neg = -2.5L;
    DIFFL1("sqrtl", cplus_sqrtl, sqrtl, x);
    DIFFL1("sqrtl(-1)", cplus_sqrtl, sqrtl, neg);
    DIFFL1("logl", cplus_logl, logl, x);
    DIFFL1("logl(0)", cplus_logl, logl, 0.0L);
    DIFFL1("expl", cplus_expl, expl, x);
    DIFFL2("powl", cplus_powl, powl, x, y);
    DIFFL2("powl(-2,.5)", cplus_powl, powl, -2.0L, 0.5L);
    DIFFL1("floorl", cplus_floorl, floorl, neg);
    DIFFL1("ceill", cplus_ceill, ceill, neg);
    DIFFL1("roundl", cplus_roundl, roundl, x);
    DIFFL1("truncl", cplus_truncl, truncl, neg);
    DIFFL1("fabsl", cplus_fabsl, fabsl, neg);
    DIFFL2("fminl", cplus_fminl, fminl, x, y);
    DIFFL2("fmaxl", cplus_fmaxl, fmaxl, x, y);
    DIFFL2("fmodl", cplus_fmodl, fmodl, x, y);
    DIFFL3("fmal", cplus_fmal, fmal, x, y, 1.0L);
    DIFFL2("nextafterl", cplus_nextafterl, nextafterl, x, y);
    DIFFL1("erfl", cplus_erfl, erfl, x);
    DIFFL1("erfcl", cplus_erfcl, erfcl, x);
    DIFFL1("tgammal", cplus_tgammal, tgammal, 5.0L);
    DIFFL1("lgammal", cplus_lgammal, lgammal, 5.0L);
}

/* Complex csin/cos/clog with finite and NaN/Inf operands. */
static void sc_10_complex_trig_log(void)
{
    DIFFC1("csin", cplus_csin, csin, 0.5, 0.25);
    DIFFC1("csin(NaN,Inf)", cplus_csin, csin, NAN, INFINITY);
    DIFFC1("ccos(Inf,NaN)", cplus_ccos, ccos, INFINITY, NAN);
    DIFFC1("ccos", cplus_ccos, ccos, 1.0, -1.0);
    DIFFC1("clog", cplus_clog, clog, 3.0, 4.0);
    DIFFC1("clog(0,0)", cplus_clog, clog, 0.0, 0.0);
    DIFFC1("clog(NaN,1)", cplus_clog, clog, NAN, 1.0);
}

/* Complex hyperbolic functions and cpow with finite and NaN inputs. */
static void sc_10_complex_hyperbolic_pow(void)
{
    DIFFC1("ccosh", cplus_ccosh, ccosh, 0.5, 0.25);
    DIFFC1("ccosh(NaN,0)", cplus_ccosh, ccosh, NAN, 0.0);
    DIFFC1("csinh", cplus_csinh, csinh, 1.0, 1.0);
    DIFFC1("ctanh", cplus_ctanh, ctanh, 0.5, -0.5);
    DIFFC2("cpow", cplus_cpow, cpow, 2.0, 0.0, 3.0, 0.0);
    DIFFC2("cpow(0,0)", cplus_cpow, cpow, 0.0, 0.0, 0.0, 0.0);
    DIFFC2("cpow(NaN+Inf,2)", cplus_cpow, cpow, NAN, INFINITY, 2.0, 0.0);
}

/* errno preservation on successful calls: a sentinel must survive. */
static void sc_10_errno_preserved_success(void)
{
    volatile double x = 4.0, y = 2.0;
    int ew, eh;
    errno = SENT; (void)cplus_sqrt(x); ew = errno;
    errno = SENT; (void)sqrt(x); eh = errno;
    if (ew != eh) FIND("sqrt success errno w=%d h=%d", ew, eh);

    errno = SENT; (void)cplus_pow(x, y); ew = errno;
    errno = SENT; (void)pow(x, y); eh = errno;
    if (ew != eh) FIND("pow success errno w=%d h=%d", ew, eh);

    errno = SENT; (void)cplus_floor(x); ew = errno;
    errno = SENT; (void)floor(x); eh = errno;
    if (ew != eh) FIND("floor success errno w=%d h=%d", ew, eh);

    errno = SENT; (void)cplus_sin(x); ew = errno;
    errno = SENT; (void)sin(x); eh = errno;
    if (ew != eh) FIND("sin success errno w=%d h=%d", ew, eh);

    errno = SENT; (void)cplus_fmin(x, y); ew = errno;
    errno = SENT; (void)fmin(x, y); eh = errno;
    if (ew != eh) FIND("fmin success errno w=%d h=%d", ew, eh);

    errno = SENT; (void)cplus_nextafter(x, y); ew = errno;
    errno = SENT; (void)nextafter(x, y); eh = errno;
    if (ew != eh) FIND("nextafter success errno w=%d h=%d", ew, eh);
}

/* FP flags and rounding mode must not be disturbed differently from host. */
static void sc_10_fp_env_preserved(void)
{
    fenv_t saved;
    volatile double x = 1.234567;
    int rc;
    if (fegetenv(&saved) != 0) { FIND("fegetenv failed"); return; }

#define STATE1(label, w, h) do {                                              \
        int fr_, wr_, hr_, fw_, fh_, ew_, eh_;                                \
        feclearexcept(FE_ALL_EXCEPT);                                         \
        fr_ = fesetround(FE_UPWARD);                                          \
        feclearexcept(FE_ALL_EXCEPT); feraiseexcept(FE_INVALID);              \
        errno = SENT; { volatile double v_ = (w)(x); (void)v_; }              \
        wr_ = fegetround(); fw_ = fetestexcept(FE_ALL_EXCEPT); ew_ = errno;   \
        feclearexcept(FE_ALL_EXCEPT);                                         \
        (void)fesetround(FE_UPWARD);                                          \
        feclearexcept(FE_ALL_EXCEPT); feraiseexcept(FE_INVALID);              \
        errno = SENT; { volatile double v_ = (h)(x); (void)v_; }              \
        hr_ = fegetround(); fh_ = fetestexcept(FE_ALL_EXCEPT); eh_ = errno;   \
        if (fr_ == 0 && wr_ != hr_)                                           \
            FIND("%s state: rounding w=%d h=%d", label, wr_, hr_);            \
        if (fw_ != fh_ || ew_ != eh_)                                         \
            FIND("%s state: flags w=0x%x h=0x%x errno w=%d h=%d",             \
                 label, fw_, fh_, ew_, eh_);                                  \
    } while (0)
    /* FE_UPWARD is installed on both paths, so fegetround() must match too. */
    rc = fesetround(FE_UPWARD);
    if (rc == 0) {
        int rw, rh;
        feclearexcept(FE_ALL_EXCEPT); feraiseexcept(FE_INVALID);
        errno = SENT; { volatile double v = cplus_sqrt(x); (void)v; }
        rw = fegetround();
        feclearexcept(FE_ALL_EXCEPT); (void)fesetround(FE_UPWARD);
        feraiseexcept(FE_INVALID);
        errno = SENT; { volatile double v = sqrt(x); (void)v; }
        rh = fegetround();
        if (rw != rh) FIND("sqrt rounding mode w=%d h=%d", rw, rh);
    }
    STATE1("sin", cplus_sin, sin);
    STATE1("cos", cplus_cos, cos);
    STATE1("sqrt", cplus_sqrt, sqrt);
    STATE1("log", cplus_log, log);
    STATE1("exp", cplus_exp, exp);
    STATE1("floor", cplus_floor, floor);
    STATE1("erf", cplus_erf, erf);
    STATE1("tgamma", cplus_tgamma, tgamma);
#undef STATE1
    (void)fesetenv(&saved);
}

/* Logging on/off parity: numeric results and errno are unchanged. */
static void sc_10_logging_parity(void)
{
    volatile double x = 2.5;
    double off[4], on[4];
    int eo[4], en[4], e;
    size_t i;

    cplus_libc_log_set_enabled(0);
    off[0] = cplus_sqrt(x); eo[0] = errno;
    off[1] = cplus_log(x);  eo[1] = errno;
    off[2] = cplus_pow(x, x); eo[2] = errno;
    off[3] = cplus_frexp(x, &e); eo[3] = errno;

    cplus_libc_log_set_enabled(1);
    on[0] = cplus_sqrt(x); en[0] = errno;
    on[1] = cplus_log(x);  en[1] = errno;
    on[2] = cplus_pow(x, x); en[2] = errno;
    on[3] = cplus_frexp(x, &e); en[3] = errno;
    cplus_libc_log_set_enabled(0);

    for (i = 0; i < 4; i++) {
        if (!same_d(off[i], on[i])) FIND("logging changed result[%d]: off=%.17g on=%.17g",
                                         (int)i, off[i], on[i]);
        if (eo[i] != en[i]) FIND("logging changed errno[%d]: off=%d on=%d", (int)i, eo[i], en[i]);
    }
}

/* Domain/range errors from other math families must track host exactly. */
static void sc_10_domain_range_errno(void)
{
    DIFF1("acos(2)", cplus_acos, acos, 2.0);
    DIFF1("asin(-2)", cplus_asin, asin, -2.0);
    DIFF1("atanh(1)", cplus_atanh, atanh, 1.0);
    DIFF1("acosh(0)", cplus_acosh, acosh, 0.0);
    DIFF1("log10(-1)", cplus_log10, log10, -1.0);
    DIFF1("exp(-1000)", cplus_exp, exp, -1000.0);
    DIFF1("cbrt(-8)", cplus_cbrt, cbrt, -8.0);
    DIFF1("log1p(-2)", cplus_log1p, log1p, -2.0);
}

/* Additional double-family wrappers: hypot, copysign, fdim, nearbyint, scalbn. */
static void sc_10_hypot_copysign_extras(void)
{
    DIFF2("hypot", cplus_hypot, hypot, 3.0, 4.0);
    DIFF2("hypot(Inf,NaN)", cplus_hypot, hypot, INFINITY, NAN);
    DIFF2("copysign", cplus_copysign, copysign, -1.0, 1.0);
    DIFF2("copysign", cplus_copysign, copysign, 1.0, -0.0);
    DIFF2("fdim", cplus_fdim, fdim, 3.0, 2.0);
    DIFF2("fdim", cplus_fdim, fdim, 2.0, 3.0);
    DIFF1("nearbyint", cplus_nearbyint, nearbyint, 2.5);
    DIFF1("nearbyint", cplus_nearbyint, nearbyint, -2.5);
    DIFF1("exp2", cplus_exp2, exp2, 3.0);
    DIFF1("expm1", cplus_expm1, expm1, 0.5);
    DIFF1("log2", cplus_log2, log2, 8.0);
    DIFF1("log1p", cplus_log1p, log1p, 0.5);
}

typedef void (*sc_fn)(void);

static const sc_fn g_scenarios[] = {
    sc_10_sqrt_positive,
    sc_10_sqrt_negative,
    sc_10_sqrt_signed_zero,
    sc_10_log_domains,
    sc_10_exp_overflow,
    sc_10_pow_negative_base,
    sc_10_fmod_by_zero,
    sc_10_rounding_modes,
    sc_10_fabs_signed_zero,
    sc_10_fmin_fmax_nan,
    sc_10_fma_accuracy,
    sc_10_remainder_remquo,
    sc_10_ldexp_frexp_modf,
    sc_10_null_outparam_guard,
    sc_10_nan_tags,
    sc_10_nextafter_adjacency,
    sc_10_ilogb_logb,
    sc_10_erf_erfc,
    sc_10_tgamma_lgamma,
    sc_10_float_variants,
    sc_10_long_double_variants,
    sc_10_complex_trig_log,
    sc_10_complex_hyperbolic_pow,
    sc_10_errno_preserved_success,
    sc_10_fp_env_preserved,
    sc_10_logging_parity,
    sc_10_domain_range_errno,
    sc_10_hypot_copysign_extras
};

int main(void)
{
    const size_t n = sizeof g_scenarios / sizeof g_scenarios[0];
    fenv_t saved_env;
    int saved_errno = errno;
    size_t i;

    if (fegetenv(&saved_env) != 0) { FIND("fegetenv failed in main"); return 1; }
    cplus_libc_log_set_enabled(0);

    for (i = 0; i < n; i++) {
        fenv_t per;
        if (fegetenv(&per) != 0) { FIND("fegetenv failed before scenario %u", (unsigned)i); continue; }
        g_scenarios[i]();
        (void)fesetenv(&per);          /* keep scenarios independent */
    }

    (void)fesetenv(&saved_env);        /* restore the caller's FP environment */
    errno = saved_errno;
    cplus_libc_log_set_enabled(0);

    if (g_findings)
        fprintf(stderr, "runtime_math: %d finding(s) reported\n", g_findings);
    printf("runtime_math: %u scenarios ok\n", (unsigned)n);
    return 0;
}
