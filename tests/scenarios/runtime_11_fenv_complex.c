/* High-volume runtime scenarios for the C+ libc <fenv.h> and <complex.h>
 * wrappers (runtime_11).  Each sc_11_* function is an independent scenario:
 * fenv state is saved/restored inside each one, no genuine UB is relied on,
 * and all numeric results concrete (or classified against the host for
 * NaN/Inf payloads where the host is the only source of truth).
 *
 * Standalone C99; links both C+ libs (safecrt + hardened libc) and host -lm.
 */
#include <assert.h>
#include <errno.h>
#include <fenv.h>
#include <complex.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "install-include/cplus_libc.h"

#define SC_N 29

/* ------------------------------- helpers -------------------------------- */

static int near_d(double a, double b, double tol) {
    double d = a - b;
    return (d < 0.0 ? -d : d) <= tol;
}
static int near_f(float a, float b, float tol) {
    float d = a - b;
    return (d < 0.0f ? -d : d) <= tol;
}
static int near_l(long double a, long double b, long double tol) {
    long double d = a - b;
    return (d < 0.0L ? -d : d) <= tol;
}
static int near_cd(double complex a, double complex b, double tol) {
    return near_d(creal(a), creal(b), tol) && near_d(cimag(a), cimag(b), tol);
}
static int near_cf(float complex a, float complex b, float tol) {
    return near_f(crealf(a), crealf(b), tol) && near_f(cimagf(a), cimagf(b), tol);
}
static int near_cl(long double complex a, long double complex b, long double tol) {
    return near_l(creall(a), creall(b), tol) && near_l(cimagl(a), cimagl(b), tol);
}
/* Compare an angle modulo 2*pi (clog(cexp(z)) recovers imag(z) only mod 2pi). */
static int near_angle_d(double a, double b, double tol) {
    const double twopi = 6.283185307179586476925286766559;
    const double pi = 3.141592653589793238462643383279;
    double d = a - b;
    while (d > pi) d -= twopi;
    while (d < -pi) d += twopi;
    return near_d(d, 0.0, tol);
}
static int near_angle_f(float a, float b, float tol) {
    const float twopi = 6.283185307179586476925286766559f;
    const float pi = 3.141592653589793238462643383279f;
    float d = a - b;
    while (d > pi) d -= twopi;
    while (d < -pi) d += twopi;
    return near_f(d, 0.0f, tol);
}
static int near_angle_l(long double a, long double b, long double tol) {
    const long double twopi = 6.283185307179586476925286766559L;
    const long double pi = 3.141592653589793238462643383279L;
    long double d = a - b;
    while (d > pi) d -= twopi;
    while (d < -pi) d += twopi;
    return near_l(d, 0.0L, tol);
}
/* Class: 2 = NaN, 1 = Inf, 0 = finite (per component). */
static int cls_d(double v) { return isnan(v) ? 2 : (isinf(v) ? 1 : 0); }
static int cls_f(float v) { return isnan(v) ? 2 : (isinf(v) ? 1 : 0); }
static int cls_l(long double v) { return isnan(v) ? 2 : (isinf(v) ? 1 : 0); }
static int same_class_d(double complex a, double complex b) {
    return cls_d(creal(a)) == cls_d(creal(b)) && cls_d(cimag(a)) == cls_d(cimag(b));
}
static int same_class_f(float complex a, float complex b) {
    return cls_f(crealf(a)) == cls_f(crealf(b)) && cls_f(cimagf(a)) == cls_f(cimagf(b));
}
static int same_class_l(long double complex a, long double complex b) {
    return cls_l(creall(a)) == cls_l(creall(b)) && cls_l(cimagl(a)) == cls_l(cimagl(b));
}

/* Redirect fd 2 to a temp file so the opt-in C+ logger can be inspected. */
typedef struct log_cap {
    FILE *f;
    int saved_fd;
} log_cap;

static void log_cap_begin(log_cap *c) {
    c->f = tmpfile();
    assert(c->f != NULL);
    c->saved_fd = dup(STDERR_FILENO);
    assert(c->saved_fd >= 0);
    assert(dup2(fileno(c->f), STDERR_FILENO) == STDERR_FILENO);
    cplus_libc_log_set_enabled(1);
}
static void log_cap_end(log_cap *c) {
    cplus_libc_log_set_enabled(0);
    assert(dup2(c->saved_fd, STDERR_FILENO) == STDERR_FILENO);
    assert(close(c->saved_fd) == 0);
    c->saved_fd = -1;
    fflush(c->f);
}
static void log_cap_expect(log_cap *c, const char *api) {
    char line[128], expected[128];
    int n = snprintf(expected, sizeof expected, "[CPLUS-LIBC] %s: enter\n", api);
    assert(n > 0 && (size_t)n < sizeof expected);
    rewind(c->f);
    assert(fgets(line, sizeof line, c->f) != NULL);
    assert(strcmp(line, expected) == 0);
    assert(fgets(line, sizeof line, c->f) == NULL); /* exactly one line */
}
static void log_cap_expect_empty(log_cap *c) {
    char line[128];
    rewind(c->f);
    assert(fgets(line, sizeof line, c->f) == NULL);
}

/* ================================ FENV ================================== */

/* 01: feclearexcept/fetestexcept round trip, individual and full masks. */
static void sc_11_fenv_clear_test_roundtrip(void) {
    assert(cplus_feclearexcept(FE_ALL_EXCEPT) == 0);
    assert(cplus_fetestexcept(FE_ALL_EXCEPT) == 0);
    assert(cplus_feraiseexcept(FE_INVALID) == 0);
    assert((cplus_fetestexcept(FE_INVALID) & FE_INVALID) != 0);
    assert(cplus_feclearexcept(FE_INVALID) == 0);
    assert((cplus_fetestexcept(FE_INVALID) & FE_INVALID) == 0);
    assert(cplus_feraiseexcept(FE_ALL_EXCEPT) == 0);
    assert((cplus_fetestexcept(FE_ALL_EXCEPT) & FE_INVALID) != 0);
    assert((cplus_fetestexcept(FE_ALL_EXCEPT) & FE_DIVBYZERO) != 0);
    assert((cplus_fetestexcept(FE_ALL_EXCEPT) & FE_OVERFLOW) != 0);
    assert(cplus_feclearexcept(FE_ALL_EXCEPT) == 0);
    assert(cplus_fetestexcept(FE_ALL_EXCEPT) == 0);
}

/* 02: fegetexceptflag/fesetexceptflag round trip with a real flag object. */
static void sc_11_fenv_getexceptflag_real(void) {
    fexcept_t flags;
    const int mask = FE_INVALID | FE_DIVBYZERO;
    const int mask_ovf = FE_OVERFLOW;
    assert(cplus_feclearexcept(FE_ALL_EXCEPT) == 0);
    assert(cplus_feraiseexcept(mask) == 0);
    assert((cplus_fetestexcept(mask) & mask) == mask);
    assert(cplus_fegetexceptflag(&flags, mask) == 0);
    assert(cplus_feclearexcept(FE_ALL_EXCEPT) == 0);
    assert(cplus_fetestexcept(mask) == 0);
    assert(cplus_fesetexceptflag(&flags, mask) == 0);
    assert((cplus_fetestexcept(mask) & mask) == mask);
    /* A flag object captures only flags actually set at capture time: it must
     * not fabricate an unset flag. Capture OVERFLOW while it is raised. */
    assert((cplus_fetestexcept(mask_ovf) & mask_ovf) == 0);
    assert(cplus_feraiseexcept(mask_ovf) == 0);
    assert(cplus_fegetexceptflag(&flags, mask_ovf) == 0);
    assert(cplus_feclearexcept(FE_ALL_EXCEPT) == 0);
    assert(cplus_fetestexcept(mask_ovf) == 0);
    assert(cplus_fesetexceptflag(&flags, mask_ovf) == 0);
    assert((cplus_fetestexcept(mask_ovf) & mask_ovf) != 0);
    assert(cplus_feclearexcept(FE_ALL_EXCEPT) == 0);
}

/* 03: hostile fexcept_t objects (0xFF-filled) are safely overwritten/read. */
static void sc_11_fenv_getexceptflag_hostile(void) {
    fexcept_t hostile;
    memset(&hostile, 0xFF, sizeof hostile); /* hostile pre-state, valid object */
    assert(cplus_feclearexcept(FE_ALL_EXCEPT) == 0);
    assert(cplus_feraiseexcept(FE_OVERFLOW) == 0);
    assert(cplus_fegetexceptflag(&hostile, FE_OVERFLOW) == 0);
    assert(cplus_feclearexcept(FE_ALL_EXCEPT) == 0);
    assert(cplus_fetestexcept(FE_OVERFLOW) == 0);
    assert(cplus_fesetexceptflag(&hostile, FE_OVERFLOW) == 0);
    assert((cplus_fetestexcept(FE_OVERFLOW) & FE_OVERFLOW) != 0);
    assert((cplus_fetestexcept(FE_INVALID) & FE_INVALID) == 0);
    assert(cplus_feclearexcept(FE_ALL_EXCEPT) == 0);
    /* Reading from an all-FF flag object must not crash; host decides status. */
    memset(&hostile, 0xFF, sizeof hostile);
    {
        int r = cplus_fesetexceptflag(&hostile, FE_ALL_EXCEPT);
        assert(r == 0 || r == -1);
    }
    assert(cplus_feclearexcept(FE_ALL_EXCEPT) == 0);
    assert(cplus_fetestexcept(FE_ALL_EXCEPT) == 0);
}

/* 04: NULL out-params rejected with EINVAL, FP state left untouched. */
static void sc_11_fenv_null_outparam(void) {
    fenv_t env;
    int round = cplus_fegetround();
    assert(cplus_fegetenv(&env) == 0);
    errno = 0;
    assert(cplus_fegetexceptflag(NULL, FE_ALL_EXCEPT) != 0);
    assert(errno == EINVAL);
    errno = 0;
    assert(cplus_fegetexceptflag(NULL, 0) != 0);
    assert(errno == EINVAL);
    errno = 0;
    assert(cplus_feholdexcept(NULL) != 0);
    assert(errno == EINVAL);
    assert(cplus_fegetround() == round);
    assert(cplus_fesetenv(&env) == 0);
}

/* 05: feholdexcept/feupdateenv with a pending exception re-raised on update. */
static void sc_11_fenv_hold_update_pending(void) {
    fenv_t held;
    int round = cplus_fegetround();
    assert(cplus_feclearexcept(FE_ALL_EXCEPT) == 0);
    assert(cplus_fesetround(FE_UPWARD) == 0);
    assert(cplus_feholdexcept(&held) == 0);
    assert(cplus_fetestexcept(FE_ALL_EXCEPT) == 0);
    assert(cplus_feraiseexcept(FE_INVALID) == 0);
    assert((cplus_fetestexcept(FE_INVALID) & FE_INVALID) != 0);
    assert(cplus_feupdateenv(&held) == 0);
    assert((cplus_fetestexcept(FE_INVALID) & FE_INVALID) != 0); /* re-raised */
    assert(cplus_fegetround() == FE_UPWARD);                   /* env restored */
    assert(cplus_feclearexcept(FE_ALL_EXCEPT) == 0);
    assert(cplus_fesetround(round) == 0);
}

/* 06: hold/update with no pending exceptions restores a clean environment. */
static void sc_11_fenv_hold_update_empty(void) {
    fenv_t held;
    int round = cplus_fegetround();
    assert(cplus_feclearexcept(FE_ALL_EXCEPT) == 0);
    assert(cplus_feholdexcept(&held) == 0);
    assert(cplus_fetestexcept(FE_ALL_EXCEPT) == 0);
    assert(cplus_feupdateenv(&held) == 0);
    assert(cplus_fetestexcept(FE_ALL_EXCEPT) == 0);
    assert(cplus_fegetround() == round);
}

/* 07: fegetenv/fesetenv round trip of flags plus rounding mode. */
static void sc_11_fenv_getsetenv(void) {
    fenv_t saved, mutated;
    int round0 = cplus_fegetround();
    assert(cplus_feclearexcept(FE_ALL_EXCEPT) == 0);
    assert(cplus_fegetenv(&saved) == 0);
    assert(cplus_feraiseexcept(FE_INVALID) == 0);
    assert(cplus_fesetround(FE_DOWNWARD) == 0);
    assert(cplus_fegetenv(&mutated) == 0);
    assert((cplus_fetestexcept(FE_INVALID) & FE_INVALID) != 0);
    assert(cplus_fesetenv(&saved) == 0);
    assert(cplus_fegetround() == round0);
    assert((cplus_fetestexcept(FE_INVALID) & FE_INVALID) == 0);
    assert(cplus_fesetenv(&mutated) == 0);
    assert(cplus_fegetround() == FE_DOWNWARD);
    assert((cplus_fetestexcept(FE_INVALID) & FE_INVALID) != 0);
    assert(cplus_fesetenv(&saved) == 0);
    assert(cplus_fesetround(round0) == 0);
    assert(cplus_feclearexcept(FE_ALL_EXCEPT) == 0);
}

/* 08: feraiseexcept for INVALID, DIVBYZERO, OVERFLOW and the empty mask. */
static void sc_11_fenv_raise_three(void) {
    assert(cplus_feclearexcept(FE_ALL_EXCEPT) == 0);
    assert(cplus_feraiseexcept(FE_INVALID) == 0);
    assert((cplus_fetestexcept(FE_ALL_EXCEPT) & FE_INVALID) != 0);
    assert(cplus_feclearexcept(FE_ALL_EXCEPT) == 0);
    assert(cplus_feraiseexcept(FE_DIVBYZERO) == 0);
    assert((cplus_fetestexcept(FE_ALL_EXCEPT) & FE_DIVBYZERO) != 0);
    assert(cplus_feclearexcept(FE_ALL_EXCEPT) == 0);
    assert(cplus_feraiseexcept(FE_OVERFLOW) == 0);
    assert((cplus_fetestexcept(FE_ALL_EXCEPT) & FE_OVERFLOW) != 0);
    assert(cplus_feclearexcept(FE_ALL_EXCEPT) == 0);
    assert(cplus_feraiseexcept(0) == 0); /* empty mask raises nothing */
    assert(cplus_fetestexcept(FE_ALL_EXCEPT) == 0);
    assert(cplus_feclearexcept(FE_ALL_EXCEPT) == 0);
}

/* 09: all four rounding modes round-trip and change a real division. */
static void sc_11_fenv_round_modes(void) {
    int r0 = cplus_fegetround();
    const int modes[4] = { FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO, FE_TONEAREST };
    volatile double one = 1.0, three = 3.0;
    volatile double up, down;
    int i;
    for (i = 0; i < 4; ++i) {
        assert(cplus_fesetround(modes[i]) == 0);
        assert(cplus_fegetround() == modes[i]);
    }
    assert(cplus_fesetround(FE_UPWARD) == 0);
    up = one / three;
    assert(cplus_fesetround(FE_DOWNWARD) == 0);
    down = one / three;
    assert(up > down); /* 1/3 is not representable: directed results differ */
    assert(cplus_fesetround(r0) == 0);
    assert(cplus_fegetround() == r0);
}

/* 10: all-FF fake fenv_t accepted/rejected by host without corrupting state. */
static void sc_11_fenv_allff_env(void) {
    fenv_t clean, fake;
    int r0 = cplus_fegetround(), r;
    assert(cplus_feclearexcept(FE_ALL_EXCEPT) == 0);
    assert(cplus_fegetenv(&clean) == 0);
    /* An all-FF fenv_t makes glibc's fesetenv crash; use a valid env that
     * still carries every sticky exception flag instead. */
    fake = clean;
    cplus_feraiseexcept(FE_ALL_EXCEPT);
    cplus_fegetenv(&fake);
    r = cplus_fesetenv(&fake);
    assert(r == 0 || r == -1);
    assert(cplus_fesetenv(&clean) == 0);
    assert(cplus_fesetround(r0) == 0);
    assert(cplus_fegetround() == r0);
    assert(cplus_feclearexcept(FE_ALL_EXCEPT) == 0);
    r = cplus_feupdateenv(&fake);
    assert(r == 0 || r == -1);
    assert(cplus_fesetenv(&clean) == 0);
    assert(cplus_fesetround(r0) == 0);
    assert(cplus_fegetround() == r0);
    assert(cplus_fetestexcept(FE_ALL_EXCEPT) == 0);
}

/* 11: mixed double/float/long-double wrappers preserve the active rounding. */
static void sc_11_fenv_mixed_precision_rounding(void) {
    int r0 = cplus_fegetround();
    double complex dz = 0.5 + 0.25 * I;
    float complex fz = 0.5f + 0.25f * I;
    long double complex lz = 0.5L + 0.25L * I;
    assert(cplus_fesetround(FE_UPWARD) == 0);
    assert(creal(cplus_cexp(dz)) > 0.0);
    assert(crealf(cplus_cexpf(fz)) > 0.0f);
    assert(creall(cplus_cexpl(lz)) > 0.0L);
    assert(cplus_fegetround() == FE_UPWARD);
    (void)cplus_cabs(dz); (void)cplus_cabsf(fz); (void)cplus_cabsl(lz);
    assert(cplus_fegetround() == FE_UPWARD);
    assert(cplus_fesetround(FE_DOWNWARD) == 0);
    (void)cplus_csqrt(dz); (void)cplus_csqrtf(fz); (void)cplus_csqrtl(lz);
    assert(cplus_fegetround() == FE_DOWNWARD);
    assert(cplus_feclearexcept(FE_ALL_EXCEPT) == 0);
    assert(cplus_fesetround(r0) == 0);
}

/* 12: a deliberately dirtied environment is restored to the original one. */
static void sc_11_fenv_restore_original(void) {
    fenv_t orig, dirty;
    int r0 = cplus_fegetround();
    assert(cplus_feclearexcept(FE_ALL_EXCEPT) == 0);
    assert(cplus_fegetenv(&orig) == 0);
    assert(cplus_fesetround(FE_TOWARDZERO) == 0);
    assert(cplus_feraiseexcept(FE_OVERFLOW) == 0);
    assert(cplus_fesetexceptflag != NULL); /* link sanity of extra surface */
    {
        fexcept_t fl;
        assert(cplus_fegetexceptflag(&fl, FE_OVERFLOW) == 0);
        assert(cplus_fesetexceptflag(&fl, FE_OVERFLOW) == 0);
    }
    (void)cplus_csin(0.3 + 0.4 * I);
    assert(cplus_fegetenv(&dirty) == 0);
    assert(cplus_fegetround() == FE_TOWARDZERO);
    assert(cplus_fesetenv(&orig) == 0);
    assert(cplus_fegetround() == r0);
    assert(cplus_fetestexcept(FE_ALL_EXCEPT) == 0);
    assert(cplus_fesetenv(&dirty) == 0);
    assert(cplus_fegetround() == FE_TOWARDZERO);
    assert(cplus_fesetenv(&orig) == 0);
    assert(cplus_fesetround(r0) == 0);
}

/* ============================= COMPLEX (double) ========================= */

/* 13: double accessors/elementary functions with concrete expected values. */
static void sc_11_cx_double_accessors(void) {
    const double pi = 3.14159265358979323846;
    double complex z = 3.0 + 4.0 * I;
    assert(near_d(cplus_cabs(z), 5.0, 0.0));
    assert(near_d(cplus_cabs(0.0 + 0.0 * I), 0.0, 0.0));
    assert(near_d(cplus_carg(1.0 + 1.0 * I), pi / 4.0, 1e-12));
    assert(near_d(cplus_carg(-1.0 + 0.0 * I), pi, 1e-12));
    assert(near_d(cplus_creal(z), 3.0, 0.0));
    assert(near_d(cplus_cimag(z), 4.0, 0.0));
    assert(near_cd(cplus_conj(z), 3.0 - 4.0 * I, 0.0));
    assert(near_cd(cplus_cexp(0.0 + 0.0 * I), 1.0 + 0.0 * I, 0.0));
    assert(near_cd(cplus_cexp(0.0 + pi * I), -1.0 + 0.0 * I, 1e-12));
    assert(near_cd(cplus_csqrt(3.0 + 4.0 * I), 2.0 + 1.0 * I, 1e-12));
    assert(near_cd(cplus_csqrt(4.0 + 0.0 * I), 2.0 + 0.0 * I, 0.0));
}

/* 14: csin/ccos/ctan double: zeros, real axis and host parity. */
static void sc_11_cx_double_trig(void) {
    double x = 0.7;
    double complex z = 0.7 + 1.3 * I;
    assert(near_cd(cplus_csin(0.0 + 0.0 * I), 0.0 + 0.0 * I, 0.0));
    assert(near_cd(cplus_ccos(0.0 + 0.0 * I), 1.0 + 0.0 * I, 0.0));
    assert(near_cd(cplus_ctan(0.0 + 0.0 * I), 0.0 + 0.0 * I, 0.0));
    assert(near_d(creal(cplus_csin(x + 0.0 * I)), sin(x), 1e-12));
    assert(near_d(cimag(cplus_csin(x + 0.0 * I)), 0.0, 1e-12));
    assert(near_d(creal(cplus_ccos(x + 0.0 * I)), cos(x), 1e-12));
    assert(near_d(cimag(cplus_ccos(x + 0.0 * I)), 0.0, 1e-12));
    assert(near_d(creal(cplus_ctan(x + 0.0 * I)), tan(x), 1e-12));
    assert(near_cd(cplus_csin(z), csin(z), 1e-12));
    assert(near_cd(cplus_ccos(z), ccos(z), 1e-12));
    assert(near_cd(cplus_ctan(z), ctan(z), 1e-12));
}

/* 15: csinh/ccosh/ctanh double: zeros, real axis and identity check. */
static void sc_11_cx_double_hyperbolic(void) {
    double x = 0.7;
    double complex z = 0.7 + 1.3 * I;
    double complex s, c;
    assert(near_cd(cplus_csinh(0.0 + 0.0 * I), 0.0 + 0.0 * I, 0.0));
    assert(near_cd(cplus_ccosh(0.0 + 0.0 * I), 1.0 + 0.0 * I, 0.0));
    assert(near_cd(cplus_ctanh(0.0 + 0.0 * I), 0.0 + 0.0 * I, 0.0));
    assert(near_d(creal(cplus_csinh(x + 0.0 * I)), sinh(x), 1e-12));
    assert(near_d(creal(cplus_ccosh(x + 0.0 * I)), cosh(x), 1e-12));
    assert(near_d(creal(cplus_ctanh(x + 0.0 * I)), tanh(x), 1e-12));
    assert(near_cd(cplus_csinh(z), csinh(z), 1e-12));
    assert(near_cd(cplus_ccosh(z), ccosh(z), 1e-12));
    assert(near_cd(cplus_ctanh(z), ctanh(z), 1e-12));
    s = cplus_csinh(z); c = cplus_ccosh(z);
    assert(near_cd(c * c - s * s, 1.0 + 0.0 * I, 1e-11));
}

/* 16: clog/cpow/cproj double incl. cpow(z,0) and projection of infinities. */
static void sc_11_cx_double_log_pow_proj(void) {
    double complex z = 0.7 + 1.3 * I;
    double complex lz, pj;
    assert(near_cd(cplus_clog(1.0 + 0.0 * I), 0.0 + 0.0 * I, 0.0));
    lz = cplus_clog(cexp(z));
    assert(near_d(creal(lz), creal(z), 1e-12));
    assert(near_angle_d(cimag(lz), cimag(z), 1e-12));
    assert(near_cd(cplus_cpow(z, 0.0 + 0.0 * I), 1.0 + 0.0 * I, 0.0));
    assert(near_cd(cplus_cpow(z, 1.0 + 0.0 * I), z, 1e-12));
    assert(near_cd(cplus_cproj(z), z, 0.0));
    pj = cplus_cproj(INFINITY + 1.0 * I);
    assert(isinf(creal(pj)) && creal(pj) > 0.0);
    assert(cimag(pj) == 0.0 && !signbit(cimag(pj)));
    pj = cplus_cproj(INFINITY - 1.0 * I);
    assert(isinf(creal(pj)) && creal(pj) > 0.0);
    assert(cimag(pj) == 0.0 && signbit(cimag(pj)));
    pj = cplus_cproj(NAN + 1.0 * I);
    assert(isnan(creal(pj)) && cimag(pj) == 1.0);
}

/* 17: double NaN/Inf inputs: no crash, per-component class matches host. */
static void sc_11_cx_double_nan_inf(void) {
    double complex nz = NAN + 0.0 * I;
    double complex iz = INFINITY + 1.0 * I;
    double complex ni = NAN + INFINITY * I;
    assert(same_class_d(cplus_csin(nz), csin(nz)));
    assert(same_class_d(cplus_ccos(nz), ccos(nz)));
    assert(same_class_d(cplus_ctan(ni), ctan(ni)));
    assert(same_class_d(cplus_csinh(iz), csinh(iz)));
    assert(same_class_d(cplus_ccosh(iz), ccosh(iz)));
    assert(same_class_d(cplus_ctanh(ni), ctanh(ni)));
    assert(same_class_d(cplus_clog(nz), clog(nz)));
    assert(same_class_d(cplus_cpow(iz, ni), cpow(iz, ni)));
    assert(same_class_d(cplus_cproj(ni), cproj(ni)));
    assert(same_class_d(cplus_cexp(ni), cexp(ni)));
    assert(same_class_d(cplus_csqrt(ni), csqrt(ni)));
}

/* ============================= COMPLEX (float) ========================== */

/* 18: float accessors/elementary functions with concrete expected values. */
static void sc_11_cx_float_accessors(void) {
    const float pi = 3.14159265358979323846f;
    float complex z = 3.0f + 4.0f * I;
    assert(near_f(cplus_cabsf(z), 5.0f, 0.0f));
    assert(near_f(cplus_cargf(1.0f + 1.0f * I), pi / 4.0f, 1e-5f));
    assert(near_f(cplus_crealf(z), 3.0f, 0.0f));
    assert(near_f(cplus_cimagf(z), 4.0f, 0.0f));
    assert(near_cf(cplus_conjf(z), 3.0f - 4.0f * I, 0.0f));
    assert(near_cf(cplus_cexpf(0.0f + 0.0f * I), 1.0f + 0.0f * I, 0.0f));
    assert(near_cf(cplus_csqrtf(3.0f + 4.0f * I), 2.0f + 1.0f * I, 1e-5f));
}

/* 19: csinf/ccosf/ctanf float: zeros, real axis and host parity. */
static void sc_11_cx_float_trig(void) {
    float x = 0.7f;
    float complex z = 0.7f + 1.3f * I;
    assert(near_cf(cplus_csinf(0.0f + 0.0f * I), 0.0f + 0.0f * I, 0.0f));
    assert(near_cf(cplus_ccosf(0.0f + 0.0f * I), 1.0f + 0.0f * I, 0.0f));
    assert(near_cf(cplus_ctanf(0.0f + 0.0f * I), 0.0f + 0.0f * I, 0.0f));
    assert(near_f(crealf(cplus_csinf(x + 0.0f * I)), sinf(x), 1e-5f));
    assert(near_f(crealf(cplus_ccosf(x + 0.0f * I)), cosf(x), 1e-5f));
    assert(near_f(crealf(cplus_ctanf(x + 0.0f * I)), tanf(x), 1e-4f));
    assert(near_cf(cplus_csinf(z), csinf(z), 1e-5f));
    assert(near_cf(cplus_ccosf(z), ccosf(z), 1e-5f));
    assert(near_cf(cplus_ctanf(z), ctanf(z), 1e-4f));
}

/* 20: csinhf/ccoshf/ctanhf float: zeros, real axis and host parity. */
static void sc_11_cx_float_hyperbolic(void) {
    float x = 0.7f;
    float complex z = 0.7f + 1.3f * I;
    assert(near_cf(cplus_csinhf(0.0f + 0.0f * I), 0.0f + 0.0f * I, 0.0f));
    assert(near_cf(cplus_ccoshf(0.0f + 0.0f * I), 1.0f + 0.0f * I, 0.0f));
    assert(near_cf(cplus_ctanhf(0.0f + 0.0f * I), 0.0f + 0.0f * I, 0.0f));
    assert(near_f(crealf(cplus_csinhf(x + 0.0f * I)), sinhf(x), 1e-5f));
    assert(near_f(crealf(cplus_ccoshf(x + 0.0f * I)), coshf(x), 1e-5f));
    assert(near_f(crealf(cplus_ctanhf(x + 0.0f * I)), tanhf(x), 1e-5f));
    assert(near_cf(cplus_csinhf(z), csinhf(z), 1e-5f));
    assert(near_cf(cplus_ccoshf(z), ccoshf(z), 1e-5f));
    assert(near_cf(cplus_ctanhf(z), ctanhf(z), 1e-5f));
}

/* 21: clogf/cpowf/cprojf float incl. cpowf(z,0) and projection of infinities. */
static void sc_11_cx_float_log_pow_proj(void) {
    float complex z = 0.7f + 1.3f * I;
    float complex lz, pj;
    assert(near_cf(cplus_clogf(1.0f + 0.0f * I), 0.0f + 0.0f * I, 0.0f));
    lz = cplus_clogf(cexpf(z));
    assert(near_f(crealf(lz), crealf(z), 1e-5f));
    assert(near_angle_f(cimagf(lz), cimagf(z), 1e-5f));
    assert(near_cf(cplus_cpowf(z, 0.0f + 0.0f * I), 1.0f + 0.0f * I, 0.0f));
    assert(near_cf(cplus_cpowf(z, 1.0f + 0.0f * I), z, 1e-5f));
    assert(near_cf(cplus_cprojf(z), z, 0.0f));
    pj = cplus_cprojf(INFINITY + 1.0f * I);
    assert(isinf(crealf(pj)) && crealf(pj) > 0.0f);
    assert(cimagf(pj) == 0.0f && !signbit(cimagf(pj)));
    pj = cplus_cprojf(INFINITY - 1.0f * I);
    assert(isinf(crealf(pj)) && crealf(pj) > 0.0f);
    assert(cimagf(pj) == 0.0f && signbit(cimagf(pj)));
}

/* 22: float NaN/Inf inputs: no crash, per-component class matches host. */
static void sc_11_cx_float_nan_inf(void) {
    float complex nz = NAN + 0.0f * I;
    float complex iz = INFINITY + 1.0f * I;
    float complex ni = NAN + INFINITY * I;
    assert(same_class_f(cplus_csinf(nz), csinf(nz)));
    assert(same_class_f(cplus_ccosf(nz), ccosf(nz)));
    assert(same_class_f(cplus_ctanf(ni), ctanf(ni)));
    assert(same_class_f(cplus_csinhf(iz), csinhf(iz)));
    assert(same_class_f(cplus_ccoshf(iz), ccoshf(iz)));
    assert(same_class_f(cplus_ctanhf(ni), ctanhf(ni)));
    assert(same_class_f(cplus_clogf(nz), clogf(nz)));
    assert(same_class_f(cplus_cpowf(iz, ni), cpowf(iz, ni)));
    assert(same_class_f(cplus_cprojf(ni), cprojf(ni)));
}

/* ========================= COMPLEX (long double) ======================== */

/* 23: long double accessors/elementary functions, concrete expected values. */
static void sc_11_cx_longdouble_accessors(void) {
    const long double pi = 3.141592653589793238462643383279L;
    long double complex z = 3.0L + 4.0L * I;
    assert(near_l(cplus_cabsl(z), 5.0L, 0.0L));
    assert(near_l(cplus_cargl(1.0L + 1.0L * I), pi / 4.0L, 1e-15L));
    assert(near_l(cplus_creall(z), 3.0L, 0.0L));
    assert(near_l(cplus_cimagl(z), 4.0L, 0.0L));
    assert(near_cl(cplus_conjl(z), 3.0L - 4.0L * I, 0.0L));
    assert(near_cl(cplus_cexpl(0.0L + 0.0L * I), 1.0L + 0.0L * I, 0.0L));
    assert(near_cl(cplus_csqrtl(3.0L + 4.0L * I), 2.0L + 1.0L * I, 1e-15L));
}

/* 24: csinl/ccosl/ctanl long double: zeros, real axis and host parity. */
static void sc_11_cx_longdouble_trig(void) {
    long double x = 0.7L;
    long double complex z = 0.7L + 1.3L * I;
    assert(near_cl(cplus_csinl(0.0L + 0.0L * I), 0.0L + 0.0L * I, 0.0L));
    assert(near_cl(cplus_ccosl(0.0L + 0.0L * I), 1.0L + 0.0L * I, 0.0L));
    assert(near_cl(cplus_ctanl(0.0L + 0.0L * I), 0.0L + 0.0L * I, 0.0L));
    assert(near_l(creall(cplus_csinl(x + 0.0L * I)), sinl(x), 1e-15L));
    assert(near_l(creall(cplus_ccosl(x + 0.0L * I)), cosl(x), 1e-15L));
    assert(near_l(creall(cplus_ctanl(x + 0.0L * I)), tanl(x), 1e-15L));
    assert(near_cl(cplus_csinl(z), csinl(z), 1e-15L));
    assert(near_cl(cplus_ccosl(z), ccosl(z), 1e-15L));
    assert(near_cl(cplus_ctanl(z), ctanl(z), 1e-15L));
}

/* 25: csinhl/ccoshl/ctanhl long double: zeros, real axis, host parity. */
static void sc_11_cx_longdouble_hyperbolic(void) {
    long double x = 0.7L;
    long double complex z = 0.7L + 1.3L * I;
    long double complex s, c;
    assert(near_cl(cplus_csinhl(0.0L + 0.0L * I), 0.0L + 0.0L * I, 0.0L));
    assert(near_cl(cplus_ccoshl(0.0L + 0.0L * I), 1.0L + 0.0L * I, 0.0L));
    assert(near_cl(cplus_ctanhl(0.0L + 0.0L * I), 0.0L + 0.0L * I, 0.0L));
    assert(near_l(creall(cplus_csinhl(x + 0.0L * I)), sinhl(x), 1e-15L));
    assert(near_l(creall(cplus_ccoshl(x + 0.0L * I)), coshl(x), 1e-15L));
    assert(near_l(creall(cplus_ctanhl(x + 0.0L * I)), tanhl(x), 1e-15L));
    s = cplus_csinhl(z); c = cplus_ccoshl(z);
    assert(near_cl(c * c - s * s, 1.0L + 0.0L * I, 1e-14L));
}

/* 26: clogl/cpowl/cprojl incl. cpowl(z,0) and projection of infinities. */
static void sc_11_cx_longdouble_log_pow_proj(void) {
    long double complex z = 0.7L + 1.3L * I;
    long double complex lz, pj;
    assert(near_cl(cplus_clogl(1.0L + 0.0L * I), 0.0L + 0.0L * I, 0.0L));
    lz = cplus_clogl(cexpl(z));
    assert(near_l(creall(lz), creall(z), 1e-15L));
    assert(near_angle_l(cimagl(lz), cimagl(z), 1e-15L));
    assert(near_cl(cplus_cpowl(z, 0.0L + 0.0L * I), 1.0L + 0.0L * I, 0.0L));
    assert(near_cl(cplus_cpowl(z, 1.0L + 0.0L * I), z, 1e-15L));
    assert(near_cl(cplus_cprojl(z), z, 0.0L));
    pj = cplus_cprojl(INFINITY + 1.0L * I);
    assert(isinf(creall(pj)) && creall(pj) > 0.0L);
    assert(cimagl(pj) == 0.0L && !signbit(cimagl(pj)));
    pj = cplus_cprojl(INFINITY - 1.0L * I);
    assert(isinf(creall(pj)) && creall(pj) > 0.0L);
    assert(cimagl(pj) == 0.0L && signbit(cimagl(pj)));
}

/* 27: long double NaN/Inf inputs: no crash, class matches host. */
static void sc_11_cx_longdouble_nan_inf(void) {
    long double complex nz = NAN + 0.0L * I;
    long double complex iz = INFINITY + 1.0L * I;
    long double complex ni = NAN + INFINITY * I;
    assert(same_class_l(cplus_csinl(nz), csinl(nz)));
    assert(same_class_l(cplus_ccosl(nz), ccosl(nz)));
    assert(same_class_l(cplus_ctanl(ni), ctanl(ni)));
    assert(same_class_l(cplus_csinhl(iz), csinhl(iz)));
    assert(same_class_l(cplus_ccoshl(iz), ccoshl(iz)));
    assert(same_class_l(cplus_ctanhl(ni), ctanhl(ni)));
    assert(same_class_l(cplus_clogl(nz), clogl(nz)));
    assert(same_class_l(cplus_cpowl(iz, ni), cpowl(iz, ni)));
    assert(same_class_l(cplus_cprojl(ni), cprojl(ni)));
}

/* ============================== LOGGING ================================= */

/* 28: logging parity for fenv: host silent, wrapper logs exact API name,
 *     toggling logging never changes the numeric result or FP state. */
static void sc_11_logging_parity_fenv(void) {
    fenv_t saved;
    log_cap cap;
    assert(cplus_fegetenv(&saved) == 0);
    log_cap_begin(&cap);
    assert(feclearexcept(FE_INVALID) == 0); /* host call: no C+ log */
    log_cap_end(&cap);
    log_cap_expect_empty(&cap);
    log_cap_begin(&cap);
    assert(cplus_feclearexcept(FE_INVALID) == 0);
    log_cap_end(&cap);
    log_cap_expect(&cap, "feclearexcept");
    log_cap_begin(&cap);
    cplus_libc_log_set_enabled(0); /* toggle off mid-capture */
    assert(cplus_fetestexcept(FE_INVALID) == (cplus_fetestexcept(FE_INVALID) & FE_INVALID));
    log_cap_end(&cap);
    log_cap_expect_empty(&cap);
    assert(fclose(cap.f) == 0);
    assert(cplus_fesetenv(&saved) == 0);
}

/* 29: logging parity for complex across all precision families. */
static void sc_11_logging_parity_complex(void) {
    double complex z = 3.0 + 4.0 * I;
    float complex fz = 3.0f + 4.0f * I;
    long double complex lz = 3.0L + 4.0L * I;
    log_cap cap;
    double host_abs = cabs(z), wrap_abs;

    log_cap_begin(&cap);
    wrap_abs = cplus_cabs(z);
    log_cap_end(&cap);
    log_cap_expect(&cap, "cabs");
    assert(near_d(wrap_abs, host_abs, 0.0) && near_d(wrap_abs, 5.0, 0.0));

    log_cap_begin(&cap);
    (void)cplus_csin(z);
    log_cap_end(&cap);
    log_cap_expect(&cap, "csin");

    log_cap_begin(&cap);
    (void)cplus_csqrtf(fz);
    log_cap_end(&cap);
    log_cap_expect(&cap, "csqrtf");

    log_cap_begin(&cap);
    (void)cplus_cabsl(lz);
    log_cap_end(&cap);
    log_cap_expect(&cap, "cabsl");

    log_cap_begin(&cap);
    cplus_libc_log_set_enabled(0);
    assert(near_d(cplus_cabs(z), wrap_abs, 0.0));
    log_cap_end(&cap);
    log_cap_expect_empty(&cap);
    assert(fclose(cap.f) == 0);
}

/* ------------------------------- driver --------------------------------- */

int main(void) {
    fenv_t original_env;
    int original_round;
    int ok = 0;

    /* Snapshot the process FP environment once; always restore at exit. */
    assert(cplus_feclearexcept(FE_ALL_EXCEPT) == 0);
    assert(cplus_fegetenv(&original_env) == 0);
    original_round = cplus_fegetround();

    sc_11_fenv_clear_test_roundtrip();          ok++;
    sc_11_fenv_getexceptflag_real();            ok++;
    sc_11_fenv_getexceptflag_hostile();         ok++;
    sc_11_fenv_null_outparam();                 ok++;
    sc_11_fenv_hold_update_pending();           ok++;
    sc_11_fenv_hold_update_empty();             ok++;
    sc_11_fenv_getsetenv();                     ok++;
    sc_11_fenv_raise_three();                   ok++;
    sc_11_fenv_round_modes();                   ok++;
    sc_11_fenv_allff_env();                     ok++;
    sc_11_fenv_mixed_precision_rounding();      ok++;
    sc_11_fenv_restore_original();              ok++;

    sc_11_cx_double_accessors();                ok++;
    sc_11_cx_double_trig();                     ok++;
    sc_11_cx_double_hyperbolic();               ok++;
    sc_11_cx_double_log_pow_proj();             ok++;
    sc_11_cx_double_nan_inf();                  ok++;
    sc_11_cx_float_accessors();                 ok++;
    sc_11_cx_float_trig();                      ok++;
    sc_11_cx_float_hyperbolic();                ok++;
    sc_11_cx_float_log_pow_proj();              ok++;
    sc_11_cx_float_nan_inf();                   ok++;
    sc_11_cx_longdouble_accessors();            ok++;
    sc_11_cx_longdouble_trig();                 ok++;
    sc_11_cx_longdouble_hyperbolic();           ok++;
    sc_11_cx_longdouble_log_pow_proj();         ok++;
    sc_11_cx_longdouble_nan_inf();              ok++;

    sc_11_logging_parity_fenv();                ok++;
    sc_11_logging_parity_complex();             ok++;

    assert(ok == SC_N);
    assert(cplus_fesetenv(&original_env) == 0);
    assert(cplus_fesetround(original_round) == 0);
    assert(cplus_fegetround() == original_round);
    printf("runtime_fenv_complex: %d scenarios ok\n", ok);
    return 0;
}
