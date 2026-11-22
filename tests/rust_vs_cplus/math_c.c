/* tests/rust_vs_cplus/math_c.c
 *
 * C+ half of the paired 100-scenario math comparison.  The Rust half is
 * tests/rust_vs_cplus/math_rs.rs; case <n> denotes the SAME scenario in both
 * files.  tests/rust_vs_cplus/run_compare.sh runs both programs with an
 * integer argument 1..100 and compares the single line:
 *
 *     RESULT <n> <OUTCOME>
 *
 * where OUTCOME is one of ACCEPTED, REFUSED, SKIP.  Both programs always
 * return 0 and never raise a signal or (in Rust) panic.
 *
 * OUTCOME DEFINITIONS (shared with math_rs.rs)
 * --------------------------------------------
 *   ACCEPTED  A defined numeric result was produced.  This deliberately
 *             INCLUDES IEEE-754 NaN/Inf results from domain errors, poles,
 *             overflow and underflow: those are defined results, not UB.
 *             On this macOS/arm64 host the platform libm leaves errno at 0
 *             for math domain/range errors (C99 allows this), so C and Rust
 *             both report ACCEPTED for e.g. sqrt(-1) and log(0).  The only
 *             observed difference is that Rust has no errno at all; it is
 *             documented per case, never presented as a fake outcome match.
 *   REFUSED   The call itself was actively refused, rather than producing a
 *             numeric result.  Two sources: (a) the hardened cplus_* wrapper
 *             rejects a NULL out-parameter with 0 + errno=EINVAL (verified
 *             empirically; cases 93-96); (b) a checked narrowing guard on a
 *             float bit pattern declines an out-of-range conversion
 *             (cases 97-100, mirrored with try_from in Rust).
 *   SKIP      The scenario is not expressible in a language.  This program
 *             never skips: it has f64 (double), f32 (float) and long double
 *             plus frexp/modf/ldexp/ilogb/erf/tgamma wrappers.  Rust skips
 *             every long-double case (83-92), every function with no std
 *             method such as frexp/modf/ldexp/ilogb/erf/gamma (43-52,
 *             76-81), and the C-only NULL out-parameter probes (93-96).
 *
 * No case invokes genuine UB: no integer casts of NaN/Inf, no integer / 0,
 * no raw float pointer punning (bit patterns are copied with memcpy), and
 * NUL out-pointers are only given to the guarded wrappers, never to libm.
 * Inputs pass through vd()/vf()/vl() volatile sinks so the calls happen at
 * runtime instead of being constant folded.
 *
 * Build (matches run_compare.sh):
 *   /usr/bin/clang -O1 -g -std=c99 -Wall -Wextra -pedantic \
 *     -I. -Ilibc -Ilibc/install-include \
 *     tests/rust_vs_cplus/math_c.c \
 *     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
 *     -lm -pthread -o /tmp/math_c
 */
#include "cplus_libc.h"

#include <assert.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CASE_COUNT 100
/* Compile-time assertion that the suite has exactly 100 cases. */
typedef char assert_case_count[(CASE_COUNT == 100) ? 1 : -1];

#ifndef DBL_TRUE_MIN
#define DBL_TRUE_MIN 0x1.0p-1074
#endif
#ifndef FLT_TRUE_MIN
#define FLT_TRUE_MIN 0x1.0p-149f
#endif

/* Runtime sinks keep every wrapper call observable. */
static volatile double  g_d;
static volatile float   g_f;
static volatile long double g_l;
static volatile int     g_i;
static volatile int     g_e;
static volatile double  g_frac;
static volatile float   g_ffrac;

static void report(int n, const char *outcome)
{
    printf("RESULT %d %s\n", n, outcome);
}
static void accepted(int n) { report(n, "ACCEPTED"); }
static void refused(int n)  { report(n, "REFUSED");  }
static void skip(int n)     { report(n, "SKIP");     }

static double vd(double x)             { volatile double v = x; return v; }
static float vf(float x)               { volatile float v = x; return v; }
static long double vl(long double x)   { volatile long double v = x; return v; }

/* Checked narrowing of an IEEE-754 bit pattern to a small integer type. */
static int narrow_u64_to_i32(uint64_t bits, int *out)
{
    if (bits > (uint64_t)INT32_MAX) return 0;
    *out = (int32_t)bits;
    return 1;
}
static int narrow_u32_to_i16(uint32_t bits, int *out)
{
    if (bits > (uint32_t)INT16_MAX) return 0;
    *out = (int16_t)bits;
    return 1;
}
static uint64_t d_bits(double x)
{
    uint64_t b;
    volatile double v = x;
    memcpy(&b, (const void *)&v, sizeof b);
    return b;
}
static uint32_t f_bits(float x)
{
    uint32_t b;
    volatile float v = x;
    memcpy(&b, (const void *)&v, sizeof b);
    return b;
}

int main(int argc, char **argv)
{
    int n;
    int ei = 0;
    int narrow = 0;
    double fd = 0.0;
    float ff = 0.0f;

    assert(CASE_COUNT == 100);
    if (argc < 2) {
        report(0, "SKIP");
        return 0;
    }
    n = atoi(argv[1]);
    if (n < 1 || n > CASE_COUNT) {
        report(n, "SKIP");
        return 0;
    }

    switch (n) {
    /* ---- double / f64, direct method counterparts (ACCEPTED both) ------- */
    case 1:  /* sqrt(4.0) = 2.0: ordinary finite square root. */
        g_d = cplus_sqrt(vd(4.0)); accepted(n); break;
    case 2:  /* sqrt(-1.0) = NaN: domain error. C errno stays 0 here; Rust NaN. */
        errno = 0; g_d = cplus_sqrt(vd(-1.0)); accepted(n); break;
    case 3:  /* sqrt(-0.0) = -0.0: signed zero preserved. */
        g_d = cplus_sqrt(vd(-0.0)); accepted(n); break;
    case 4:  /* sqrt(+Inf) = +Inf: infinity is a fixed point. */
        g_d = cplus_sqrt(vd(INFINITY)); accepted(n); break;
    case 5:  /* sqrt(DBL_TRUE_MIN): smallest subnormal input, finite output. */
        g_d = cplus_sqrt(vd(DBL_TRUE_MIN)); accepted(n); break;
    case 6:  /* sqrt(DBL_MAX): largest finite double stays finite. */
        g_d = cplus_sqrt(vd(DBL_MAX)); accepted(n); break;
    case 7:  /* fabs(-3.5) = 3.5: magnitude. */
        g_d = cplus_fabs(vd(-3.5)); accepted(n); break;
    case 8:  /* fabs(-0.0) = +0.0: sign bit cleared. */
        g_d = cplus_fabs(vd(-0.0)); accepted(n); break;
    case 9:  /* floor(2.9) = 2.0: toward negative infinity. */
        g_d = cplus_floor(vd(2.9)); accepted(n); break;
    case 10: /* floor(-2.1) = -3.0: negative rounding direction. */
        g_d = cplus_floor(vd(-2.1)); accepted(n); break;
    case 11: /* ceil(2.1) = 3.0: toward positive infinity. */
        g_d = cplus_ceil(vd(2.1)); accepted(n); break;
    case 12: /* ceil(-2.9) = -2.0: negative rounding direction. */
        g_d = cplus_ceil(vd(-2.9)); accepted(n); break;
    case 13: /* round(2.5) = 3.0: halfway away from zero. */
        g_d = cplus_round(vd(2.5)); accepted(n); break;
    case 14: /* round(-2.5) = -3.0: symmetric halfway rule. */
        g_d = cplus_round(vd(-2.5)); accepted(n); break;
    case 15: /* trunc(2.9) = 2.0: toward zero. */
        g_d = cplus_trunc(vd(2.9)); accepted(n); break;
    case 16: /* trunc(-2.9) = -2.0: toward zero from negative side. */
        g_d = cplus_trunc(vd(-2.9)); accepted(n); break;
    case 17: /* fmod(5,3) = 2: remainder, corresponds to Rust `%`. */
        g_d = cplus_fmod(vd(5.0), vd(3.0)); accepted(n); break;
    case 18: /* fmod(-5,3) = -2: remainder keeps dividend sign. */
        g_d = cplus_fmod(vd(-5.0), vd(3.0)); accepted(n); break;
    case 19: /* fmod(5,0) = NaN: division-by-zero, defined NaN both sides. */
        errno = 0; g_d = cplus_fmod(vd(5.0), vd(0.0)); accepted(n); break;
    case 20: /* pow(2,10) = 1024: exact integer exponent. */
        g_d = cplus_pow(vd(2.0), vd(10.0)); accepted(n); break;
    case 21: /* pow(-1,0.5) = NaN: domain error, no real result. */
        errno = 0; g_d = cplus_pow(vd(-1.0), vd(0.5)); accepted(n); break;
    case 22: /* pow(0,-1) = +Inf: pole, defined infinity both sides. */
        g_d = cplus_pow(vd(0.0), vd(-1.0)); accepted(n); break;
    case 23: /* pow(2,-1074) = DBL_TRUE_MIN: underflow to subnormal. */
        g_d = cplus_pow(vd(2.0), vd(-1074.0)); accepted(n); break;
    case 24: /* exp(1) = e: ordinary transcendental. */
        g_d = cplus_exp(vd(1.0)); accepted(n); break;
    case 25: /* exp(1000) = +Inf: overflow to infinity. */
        g_d = cplus_exp(vd(1000.0)); accepted(n); break;
    case 26: /* exp(-1000) = 0.0: underflow to zero. */
        g_d = cplus_exp(vd(-1000.0)); accepted(n); break;
    case 27: /* log(1) = 0.0: neutral element. */
        g_d = cplus_log(vd(1.0)); accepted(n); break;
    case 28: /* log(0) = -Inf: pole. C errno 0 here; Rust ln(0) = -inf. */
        errno = 0; g_d = cplus_log(vd(0.0)); accepted(n); break;
    case 29: /* log(-1) = NaN: domain error both sides. */
        errno = 0; g_d = cplus_log(vd(-1.0)); accepted(n); break;
    case 30: /* log(+Inf) = +Inf: logarithmic growth unbounded. */
        g_d = cplus_log(vd(INFINITY)); accepted(n); break;
    case 31: /* sin(0) = 0.0: zero sine. */
        g_d = cplus_sin(vd(0.0)); accepted(n); break;
    case 32: /* cos(0) = 1.0: zero cosine. */
        g_d = cplus_cos(vd(0.0)); accepted(n); break;
    case 33: /* tan(0) = 0.0: zero tangent. */
        g_d = cplus_tan(vd(0.0)); accepted(n); break;
    case 34: /* sin(NaN) = NaN: NaN propagation, not a domain error. */
        g_d = cplus_sin(vd(NAN)); accepted(n); break;
    case 35: /* fmin(1,2) = 1: finite minimum. */
        g_d = cplus_fmin(vd(1.0), vd(2.0)); accepted(n); break;
    case 36: /* fmin(NaN,2) = 2: fmin ignores NaN operands. */
        g_d = cplus_fmin(vd(NAN), vd(2.0)); accepted(n); break;
    case 37: /* fmax(NaN,2) = 2: fmax ignores NaN operands. */
        g_d = cplus_fmax(vd(NAN), vd(2.0)); accepted(n); break;
    case 38: /* fmax(-0,+0) = +0: both non-NaN, sign of zero per IEEE max. */
        g_d = cplus_fmax(vd(-0.0), vd(0.0)); accepted(n); break;
    case 39: /* fma(2,3,4) = 10: fused multiply-add, Rust mul_add. */
        g_d = cplus_fma(vd(2.0), vd(3.0), vd(4.0)); accepted(n); break;
    case 40: /* hypot(3,4) = 5: Euclidean length. */
        g_d = cplus_hypot(vd(3.0), vd(4.0)); accepted(n); break;
    case 41: /* hypot(DBL_MAX,DBL_MAX) = +Inf: overflow avoided in intermediate. */
        g_d = cplus_hypot(vd(DBL_MAX), vd(DBL_MAX)); accepted(n); break;
    case 42: /* copysign(1,-0) = -1: sign manipulation, Rust copysign. */
        g_d = cplus_copysign(vd(1.0), vd(-0.0)); accepted(n); break;

    /* ---- double functions with no std Rust method: C ACCEPTED, Rust SKIP - */
    case 43: /* frexp(8,&e) = 0.5, e=4: no Rust frexp method -> SKIP there. */
        g_d = cplus_frexp(vd(8.0), &ei); g_e = ei; accepted(n); break;
    case 44: /* frexp(0,&e) = 0.0, e=0: zero decomposition -> SKIP in Rust. */
        g_d = cplus_frexp(vd(0.0), &ei); g_e = ei; accepted(n); break;
    case 45: /* modf(3.25,&i) = 0.25, i=3: Rust has fract() but not modf API. */
        g_d = cplus_modf(vd(3.25), &fd); g_frac = fd; accepted(n); break;
    case 46: /* modf(-3.25,&i) = -0.25, i=-3: signed splitting -> SKIP Rust. */
        g_d = cplus_modf(vd(-3.25), &fd); g_frac = fd; accepted(n); break;
    case 47: /* ldexp(0.5,4) = 8: exponent scaling, no Rust ldexp method. */
        g_d = cplus_ldexp(vd(0.5), 4); accepted(n); break;
    case 48: /* ldexp(DBL_MAX,1) = +Inf: scaling overflow -> SKIP in Rust. */
        g_d = cplus_ldexp(vd(DBL_MAX), 1); accepted(n); break;
    case 49: /* ilogb(8) = 3: integer exponent, no Rust ilogb method. */
        g_i = cplus_ilogb(vd(8.0)); accepted(n); break;
    case 50: /* ilogb(0) = FP_ILOGB0: sentinel exponent -> SKIP in Rust. */
        g_i = cplus_ilogb(vd(0.0)); accepted(n); break;
    case 51: /* erf(0) = 0: error function, no Rust std erf method. */
        g_d = cplus_erf(vd(0.0)); accepted(n); break;
    case 52: /* tgamma(5) = 24: Gamma(5)=4!, no Rust std gamma method. */
        g_d = cplus_tgamma(vd(5.0)); accepted(n); break;

    /* ---- float / f32 with direct Rust method counterparts --------------- */
    case 53: /* sqrtf(4f) = 2f. */
        g_f = cplus_sqrtf(vf(4.0f)); accepted(n); break;
    case 54: /* sqrtf(-1f) = NaN: float domain error, Rust f32::sqrt NaN. */
        errno = 0; g_f = cplus_sqrtf(vf(-1.0f)); accepted(n); break;
    case 55: /* fabsf(-3.5f) = 3.5f. */
        g_f = cplus_fabsf(vf(-3.5f)); accepted(n); break;
    case 56: /* floorf(2.9f) = 2f. */
        g_f = cplus_floorf(vf(2.9f)); accepted(n); break;
    case 57: /* ceilf(2.1f) = 3f. */
        g_f = cplus_ceilf(vf(2.1f)); accepted(n); break;
    case 58: /* roundf(2.5f) = 3f: halfway away from zero. */
        g_f = cplus_roundf(vf(2.5f)); accepted(n); break;
    case 59: /* truncf(2.9f) = 2f. */
        g_f = cplus_truncf(vf(2.9f)); accepted(n); break;
    case 60: /* fmodf(5f,3f) = 2f: Rust f32 `%`. */
        g_f = cplus_fmodf(vf(5.0f), vf(3.0f)); accepted(n); break;
    case 61: /* powf(2f,10f) = 1024f. */
        g_f = cplus_powf(vf(2.0f), vf(10.0f)); accepted(n); break;
    case 62: /* expf(1f) = e as f32. */
        g_f = cplus_expf(vf(1.0f)); accepted(n); break;
    case 63: /* expf(100f) = +Inf: FLT_MAX overflow. */
        g_f = cplus_expf(vf(100.0f)); accepted(n); break;
    case 64: /* logf(1f) = 0f. */
        g_f = cplus_logf(vf(1.0f)); accepted(n); break;
    case 65: /* logf(0f) = -Inf: pole, Rust f32::ln -> -inf. */
        errno = 0; g_f = cplus_logf(vf(0.0f)); accepted(n); break;
    case 66: /* sinf(0f) = 0f. */
        g_f = cplus_sinf(vf(0.0f)); accepted(n); break;
    case 67: /* cosf(0f) = 1f. */
        g_f = cplus_cosf(vf(0.0f)); accepted(n); break;
    case 68: /* tanf(0f) = 0f. */
        g_f = cplus_tanf(vf(0.0f)); accepted(n); break;
    case 69: /* fminf(NaN,2f) = 2f: NaN ignored. */
        g_f = cplus_fminf(vf(NAN), vf(2.0f)); accepted(n); break;
    case 70: /* fmaxf(NaN,2f) = 2f: NaN ignored. */
        g_f = cplus_fmaxf(vf(NAN), vf(2.0f)); accepted(n); break;
    case 71: /* fmaf(2f,3f,4f) = 10f: Rust f32::mul_add. */
        g_f = cplus_fmaf(vf(2.0f), vf(3.0f), vf(4.0f)); accepted(n); break;
    case 72: /* hypotf(3f,4f) = 5f. */
        g_f = cplus_hypotf(vf(3.0f), vf(4.0f)); accepted(n); break;
    case 73: /* sqrtf(FLT_TRUE_MIN): smallest subnormal float input. */
        g_f = cplus_sqrtf(vf(FLT_TRUE_MIN)); accepted(n); break;
    case 74: /* fmaxf(FLT_MAX,1f) = FLT_MAX: largest finite float. */
        g_f = cplus_fmaxf(vf(FLT_MAX), vf(1.0f)); accepted(n); break;
    case 75: /* fminf(-FLT_MAX,1f) = -FLT_MAX: negative largest finite. */
        g_f = cplus_fminf(vf(-FLT_MAX), vf(1.0f)); accepted(n); break;
    case 82: /* expf(-100f) = 0f: float underflow. */
        g_f = cplus_expf(vf(-100.0f)); accepted(n); break;

    /* ---- float functions with no std Rust method: C ACCEPTED, Rust SKIP -- */
    case 76: /* frexpf(8f,&e) = 0.5f, e=4 -> SKIP in Rust. */
        g_f = cplus_frexpf(vf(8.0f), &ei); g_e = ei; accepted(n); break;
    case 77: /* modff(3.25f,&i) = 0.25f, i=3f -> SKIP in Rust. */
        g_f = cplus_modff(vf(3.25f), &ff); g_ffrac = ff; accepted(n); break;
    case 78: /* ldexpf(0.5f,4) = 8f -> SKIP in Rust. */
        g_f = cplus_ldexpf(vf(0.5f), 4); accepted(n); break;
    case 79: /* ilogbf(8f) = 3 -> SKIP in Rust. */
        g_i = cplus_ilogbf(vf(8.0f)); accepted(n); break;
    case 80: /* erff(0f) = 0f -> SKIP in Rust. */
        g_f = cplus_erff(vf(0.0f)); accepted(n); break;
    case 81: /* tgammaf(5f) = 24f -> SKIP in Rust. */
        g_f = cplus_tgammaf(vf(5.0f)); accepted(n); break;

    /* ---- long double: Rust has no f80/f128, so SKIP there --------------- */
    case 83: /* sqrtl(4L) = 2L: extended-precision square root. */
        g_l = cplus_sqrtl(vl(4.0L)); accepted(n); break;
    case 84: /* fabsl(-3.5L) = 3.5L. */
        g_l = cplus_fabsl(vl(-3.5L)); accepted(n); break;
    case 85: /* floorl(2.9L) = 2L. */
        g_l = cplus_floorl(vl(2.9L)); accepted(n); break;
    case 86: /* ceill(2.1L) = 3L. */
        g_l = cplus_ceill(vl(2.1L)); accepted(n); break;
    case 87: /* sinl(0L) = 0L. */
        g_l = cplus_sinl(vl(0.0L)); accepted(n); break;
    case 88: /* cosl(0L) = 1L. */
        g_l = cplus_cosl(vl(0.0L)); accepted(n); break;
    case 89: /* tanl(0L) = 0L. */
        g_l = cplus_tanl(vl(0.0L)); accepted(n); break;
    case 90: /* expl(1L) = e in long double. */
        g_l = cplus_expl(vl(1.0L)); accepted(n); break;
    case 91: /* logl(1L) = 0L. */
        g_l = cplus_logl(vl(1.0L)); accepted(n); break;
    case 92: /* powl(2L,10L) = 1024L. */
        g_l = cplus_powl(vl(2.0L), vl(10.0L)); accepted(n); break;

    /* ---- genuine REFUSED: guarded wrappers reject NULL out-parameters ---- */
    case 93: /* frexp(8,NULL): wrapper returns 0 and sets errno=EINVAL. */
        errno = 0; g_d = cplus_frexp(vd(8.0), NULL);
        if (errno == EINVAL) refused(n); else accepted(n);
        break;
    case 94: /* modf(3.25,NULL): wrapper returns 0 and sets errno=EINVAL. */
        errno = 0; g_d = cplus_modf(vd(3.25), NULL);
        if (errno == EINVAL) refused(n); else accepted(n);
        break;
    case 95: /* frexpf(8f,NULL): float variant refuses with EINVAL. */
        errno = 0; g_f = cplus_frexpf(vf(8.0f), NULL);
        if (errno == EINVAL) refused(n); else accepted(n);
        break;
    case 96: /* modff(3.25f,NULL): float variant refuses with EINVAL. */
        errno = 0; g_f = cplus_modff(vf(3.25f), NULL);
        if (errno == EINVAL) refused(n); else accepted(n);
        break;

    /* ---- genuine REFUSED: checked narrowing of math bit patterns -------- */
    case 97: /* fabs(-DBL_MAX) bits -> int32: exceeds INT32_MAX, refuse. */
        if (narrow_u64_to_i32(d_bits(cplus_fabs(vd(-DBL_MAX))), &narrow))
            accepted(n); else refused(n);
        g_i = narrow; break;
    case 98: /* fabsf(-FLT_MAX) bits -> int16: exceeds INT16_MAX, refuse. */
        if (narrow_u32_to_i16(f_bits(cplus_fabsf(vf(-FLT_MAX))), &narrow))
            accepted(n); else refused(n);
        g_i = narrow; break;
    case 99: /* exp(1000)=Inf bits -> int32: out of range, refuse. */
        if (narrow_u64_to_i32(d_bits(cplus_exp(vd(1000.0))), &narrow))
            accepted(n); else refused(n);
        g_i = narrow; break;
    case 100: /* expf(100f)=Inf bits -> int16: out of range, refuse. */
        if (narrow_u32_to_i16(f_bits(cplus_expf(vf(100.0f))), &narrow))
            accepted(n); else refused(n);
        g_i = narrow; break;

    default:
        skip(n);
        break;
    }
    return 0;
}
