/* Standalone differential tests for the platform-backed ISO C complex
 * inverse-trigonometric wrappers in libc/src/complex/complex_inverse.c.
 * Each wrapper must match the identically named host <complex.h> function. */

/* Resolves to libc/install-include/cplus_libc.h via the -Ilibc include path. */
#include "install-include/cplus_libc.h"

#include <assert.h>
#include <complex.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Local extern declarations for the wrappers under test. The installed header
 * may not yet list these symbols; repeated identical declarations are legal. */
double complex cplus_cacos(double complex);
double complex cplus_casin(double complex);
double complex cplus_catan(double complex);
double complex cplus_cacosh(double complex);
double complex cplus_casinh(double complex);
double complex cplus_catanh(double complex);

float complex cplus_cacosf(float complex);
float complex cplus_casinf(float complex);
float complex cplus_catanf(float complex);
float complex cplus_cacoshf(float complex);
float complex cplus_casinhf(float complex);
float complex cplus_catanhf(float complex);

long double complex cplus_cacosl(long double complex);
long double complex cplus_casinl(long double complex);
long double complex cplus_catanl(long double complex);
long double complex cplus_cacoshl(long double complex);
long double complex cplus_casinhl(long double complex);
long double complex cplus_catanhl(long double complex);

/* Tolerance-based complex comparison with a harmless non-NaN fallback so the
 * comparison itself never introduces indeterminate results. */
static int near_d(double a, double b, double tol){
    double d = a - b; if (!isfinite(d)) return a == b; return d >= -tol && d <= tol;
}
static int near_f(float a, float b, float tol){
    float d = a - b; if (!isfinite(d)) return a == b; return d >= -tol && d <= tol;
}
static int near_l(long double a, long double b, long double tol){
    long double d = a - b; if (!isfinite(d)) return a == b; return d >= -tol && d <= tol;
}
static int near_cd(double complex a, double complex b, double tol){
    return near_d(creal(a), creal(b), tol) && near_d(cimag(a), cimag(b), tol);
}
static int near_cf(float complex a, float complex b, float tol){
    return near_f(crealf(a), crealf(b), tol) && near_f(cimagf(a), cimagf(b), tol);
}
static int near_cl(long double complex a, long double complex b, long double tol){
    return near_l(creall(a), creall(b), tol) && near_l(cimagl(a), cimagl(b), tol);
}

/* Classification: 2 = NaN, 1 = Inf, 0 = finite. */
static int class_d(double v){ if (isnan(v)) return 2; if (isinf(v)) return 1; return 0; }
static int class_f(float v){ if (isnan(v)) return 2; if (isinf(v)) return 1; return 0; }
static int class_l(long double v){ if (isnan(v)) return 2; if (isinf(v)) return 1; return 0; }
static int same_class_d(double complex a, double complex b){
    return class_d(creal(a)) == class_d(creal(b)) && class_d(cimag(a)) == class_d(cimag(b));
}
static int same_class_f(float complex a, float complex b){
    return class_f(crealf(a)) == class_f(crealf(b)) && class_f(cimagf(a)) == class_f(cimagf(b));
}
static int same_class_l(long double complex a, long double complex b){
    return class_l(creall(a)) == class_l(creall(b)) && class_l(cimagl(a)) == class_l(cimagl(b));
}

static void test_double(void){
    const double tol = 1e-12;
    double complex z = 0.7 + 1.3 * I; /* finite, away from branch cuts */

    assert(near_cd(cplus_cacos(z),  cacos(z),  tol));
    assert(near_cd(cplus_casin(z),  casin(z),  tol));
    assert(near_cd(cplus_catan(z),  catan(z),  tol));
    assert(near_cd(cplus_cacosh(z), cacosh(z), tol));
    assert(near_cd(cplus_casinh(z), casinh(z), tol));
    assert(near_cd(cplus_catanh(z), catanh(z), tol));

    /* NaN/Inf inputs: no crash, classification parity with the host. */
    double complex nan_z = NAN + 0.0 * I;
    double complex inf_z = INFINITY + 1.0 * I;
    assert(same_class_d(cplus_cacos(nan_z),  cacos(nan_z)));
    assert(same_class_d(cplus_casin(nan_z),  casin(nan_z)));
    assert(same_class_d(cplus_catan(inf_z),  catan(inf_z)));
    assert(same_class_d(cplus_cacosh(nan_z), cacosh(nan_z)));
    assert(same_class_d(cplus_casinh(inf_z), casinh(inf_z)));
    assert(same_class_d(cplus_catanh(nan_z), catanh(nan_z)));
}

static void test_float(void){
    const float tol = 1e-5f;
    float complex z = 0.7f + 1.3f * I;

    assert(near_cf(cplus_cacosf(z),  cacosf(z),  tol));
    assert(near_cf(cplus_casinf(z),  casinf(z),  tol));
    assert(near_cf(cplus_catanf(z),  catanf(z),  tol));
    assert(near_cf(cplus_cacoshf(z), cacoshf(z), tol));
    assert(near_cf(cplus_casinhf(z), casinhf(z), tol));
    assert(near_cf(cplus_catanhf(z), catanhf(z), tol));

    float complex nan_z = NAN + 0.0f * I;
    float complex inf_z = INFINITY + 1.0f * I;
    assert(same_class_f(cplus_cacosf(nan_z),  cacosf(nan_z)));
    assert(same_class_f(cplus_casinf(nan_z),  casinf(nan_z)));
    assert(same_class_f(cplus_catanf(inf_z),  catanf(inf_z)));
    assert(same_class_f(cplus_cacoshf(nan_z), cacoshf(nan_z)));
    assert(same_class_f(cplus_casinhf(inf_z), casinhf(inf_z)));
    assert(same_class_f(cplus_catanhf(nan_z), catanhf(nan_z)));
}

static void test_long_double(void){
    const long double tol = 1e-15L;
    long double complex z = 0.7L + 1.3L * I;

    assert(near_cl(cplus_cacosl(z),  cacosl(z),  tol));
    assert(near_cl(cplus_casinl(z),  casinl(z),  tol));
    assert(near_cl(cplus_catanl(z),  catanl(z),  tol));
    assert(near_cl(cplus_cacoshl(z), cacoshl(z), tol));
    assert(near_cl(cplus_casinhl(z), casinhl(z), tol));
    assert(near_cl(cplus_catanhl(z), catanhl(z), tol));

    long double complex nan_z = NAN + 0.0L * I;
    long double complex inf_z = INFINITY + 1.0L * I;
    assert(same_class_l(cplus_cacosl(nan_z),  cacosl(nan_z)));
    assert(same_class_l(cplus_casinl(nan_z),  casinl(nan_z)));
    assert(same_class_l(cplus_catanl(inf_z),  catanl(inf_z)));
    assert(same_class_l(cplus_cacoshl(nan_z), cacoshl(nan_z)));
    assert(same_class_l(cplus_casinhl(inf_z), casinhl(inf_z)));
    assert(same_class_l(cplus_catanhl(nan_z), catanhl(nan_z)));
}

/* The opt-in logger must never perturb results. Compare bit patterns of the
 * returned components across logging OFF / ON for three functions spanning
 * all three precisions. Finite inputs keep the equality check well-defined. */
static void test_logging_parity(void){
    double complex dz = 0.7 + 1.3 * I;
    float complex fz = 0.7f + 1.3f * I;
    long double complex lz = 0.7L + 1.3L * I;
    double complex d_off, d_on;
    float complex f_off, f_on;
    long double complex l_off, l_on;

    cplus_libc_log_set_enabled(0);
    d_off = cplus_cacos(dz);
    f_off = cplus_casinf(fz);
    l_off = cplus_catanhl(lz);

    cplus_libc_log_set_enabled(1);
    d_on = cplus_cacos(dz);
    f_on = cplus_casinf(fz);
    l_on = cplus_catanhl(lz);
    cplus_libc_log_set_enabled(0); /* restore default: quiet for other tests */

    assert(memcmp(&d_off, &d_on, sizeof d_off) == 0);
    assert(memcmp(&f_off, &f_on, sizeof f_off) == 0);
    assert(memcmp(&l_off, &l_on, sizeof l_off) == 0);
}

int main(void){
    test_double();
    test_float();
    test_long_double();
    test_logging_parity();
    puts("complex_inverse: ok");
    return 0;
}
