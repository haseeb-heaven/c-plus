/* Standalone differential tests for the extra platform-backed <complex.h>
 * wrappers in libc/src/complex/complex_extra.c. */

#include <assert.h>
#include <complex.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

/* Local extern declarations for the wrappers under test. */
double complex cplus_csin(double complex);
double complex cplus_ccos(double complex);
double complex cplus_ctan(double complex);
double complex cplus_csinh(double complex);
double complex cplus_ccosh(double complex);
double complex cplus_ctanh(double complex);
double complex cplus_clog(double complex);
double complex cplus_cpow(double complex, double complex);
double complex cplus_cproj(double complex);

float cplus_cabsf(float complex);
float cplus_cargf(float complex);
float cplus_crealf(float complex);
float cplus_cimagf(float complex);
float complex cplus_conjf(float complex);
float complex cplus_cexpf(float complex);
float complex cplus_csqrtf(float complex);
float complex cplus_csinf(float complex);
float complex cplus_ccosf(float complex);
float complex cplus_ctanf(float complex);
float complex cplus_csinhf(float complex);
float complex cplus_ccoshf(float complex);
float complex cplus_ctanhf(float complex);
float complex cplus_clogf(float complex);
float complex cplus_cpowf(float complex, float complex);
float complex cplus_cprojf(float complex);

long double cplus_cabsl(long double complex);
long double cplus_cargl(long double complex);
long double cplus_creall(long double complex);
long double cplus_cimagl(long double complex);
long double complex cplus_conjl(long double complex);
long double complex cplus_cexpl(long double complex);
long double complex cplus_csqrtl(long double complex);
long double complex cplus_csinl(long double complex);
long double complex cplus_ccosl(long double complex);
long double complex cplus_ctanl(long double complex);
long double complex cplus_csinhl(long double complex);
long double complex cplus_ccoshl(long double complex);
long double complex cplus_ctanhl(long double complex);
long double complex cplus_clogl(long double complex);
long double complex cplus_cpowl(long double complex, long double complex);
long double complex cplus_cprojl(long double complex);

static int near_d(double a, double b, double tol){ double d = a - b; if (d < 0) d = -d; return d <= tol; }
static int near_f(float a, float b, float tol){ float d = a - b; if (d < 0) d = -d; return d <= tol; }
static int near_l(long double a, long double b, long double tol){ long double d = a - b; if (d < 0) d = -d; return d <= tol; }

static int near_cd(double complex a, double complex b, double tol){
    return near_d(creal(a), creal(b), tol) && near_d(cimag(a), cimag(b), tol);
}
static int near_cf(float complex a, float complex b, float tol){
    return near_f(crealf(a), crealf(b), tol) && near_f(cimagf(a), cimagf(b), tol);
}
static int near_cl(long double complex a, long double complex b, long double tol){
    return near_l(creall(a), creall(b), tol) && near_l(cimagl(a), cimagl(b), tol);
}

/* Compare an angle modulo 2*pi (used when recovering z from clog(exp(z))). */
static int near_angle_d(double a, double b, double tol){
    const double two_pi = 6.283185307179586476925286766559;
    const double pi = 3.14159265358979323846;
    double d = a - b;
    while (d > pi) d -= two_pi;
    while (d < -pi) d += two_pi;
    return near_d(d, 0.0, tol);
}
static int near_angle_f(float a, float b, float tol){
    const float two_pi = 6.283185307179586476925286766559f;
    const float pi = 3.14159265358979323846f;
    float d = a - b;
    while (d > pi) d -= two_pi;
    while (d < -pi) d += two_pi;
    return near_f(d, 0.0f, tol);
}
static int near_angle_l(long double a, long double b, long double tol){
    const long double two_pi = 6.283185307179586476925286766559L;
    const long double pi = 3.14159265358979323846L;
    long double d = a - b;
    while (d > pi) d -= two_pi;
    while (d < -pi) d += two_pi;
    return near_l(d, 0.0L, tol);
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
    double complex z = 0.7 + 1.3 * I;

    /* csin (and friends) differential against the host, on a known value. */
    assert(near_cd(cplus_csin(z), csin(z), tol));
    assert(near_cd(cplus_ccos(z), ccos(z), tol));
    assert(near_cd(cplus_ctan(z), ctan(z), tol));
    assert(near_cd(cplus_csinh(z), csinh(z), tol));
    assert(near_cd(cplus_ctanh(z), ctanh(z), tol));

    /* ccosh(z) == (cexp(z) + cexp(-z)) / 2. */
    assert(near_cd(cplus_ccosh(z), (cexp(z) + cexp(-z)) / 2.0, tol));

    /* clog(exp(z)) recovers z; imaginary part compared modulo 2*pi. */
    double complex lz = cplus_clog(cexp(z));
    assert(near_d(creal(lz), creal(z), tol));
    assert(near_angle_d(cimag(lz), cimag(z), tol));

    /* cpow(z, 0) == 1 + 0i. */
    assert(near_cd(cplus_cpow(z, 0.0 + 0.0 * I), 1.0 + 0.0 * I, tol));

    /* cproj of a finite value equals the value. */
    assert(near_cd(cplus_cproj(z), z, tol));

    /* NaN/Inf inputs: no crash, classification matches the host. */
    double complex nan_z = NAN + 0.0 * I;
    double complex inf_z = INFINITY + 1.0 * I;
    assert(same_class_d(cplus_csin(nan_z), csin(nan_z)));
    assert(same_class_d(cplus_clog(inf_z), clog(inf_z)));
    assert(same_class_d(cplus_csinh(inf_z), csinh(inf_z)));
}

static void test_float(void){
    const float tol = 1e-5f;
    float complex z = 0.7f + 1.3f * I;

    /* Scalar accessors / elementary functions consistent with the host. */
    assert(near_f(cplus_cabsf(z), cabsf(z), tol));
    assert(near_f(cplus_cargf(z), cargf(z), tol));
    assert(near_f(cplus_crealf(z), crealf(z), tol));
    assert(near_f(cplus_cimagf(z), cimagf(z), tol));
    assert(near_cf(cplus_conjf(z), conjf(z), tol));
    assert(near_cf(cplus_cexpf(z), cexpf(z), tol));
    assert(near_cf(cplus_csqrtf(z), csqrtf(z), tol));

    /* csinf differential on a known value. */
    assert(near_cf(cplus_csinf(z), csinf(z), tol));
    assert(near_cf(cplus_ccosf(z), ccosf(z), tol));
    assert(near_cf(cplus_ctanf(z), ctanf(z), tol));
    assert(near_cf(cplus_csinhf(z), csinhf(z), tol));
    assert(near_cf(cplus_ctanhf(z), ctanhf(z), tol));

    /* ccoshf identity. */
    assert(near_cf(cplus_ccoshf(z), (cexpf(z) + cexpf(-z)) / 2.0f, tol));

    /* clogf(expf(z)) recovers z, imaginary part modulo 2*pi. */
    float complex lz = cplus_clogf(cexpf(z));
    assert(near_f(crealf(lz), crealf(z), tol));
    assert(near_angle_f(cimagf(lz), cimagf(z), tol));

    assert(near_cf(cplus_cpowf(z, 0.0f + 0.0f * I), 1.0f + 0.0f * I, tol));
    assert(near_cf(cplus_cprojf(z), z, tol));

    float complex nan_z = NAN + 0.0f * I;
    float complex inf_z = INFINITY + 1.0f * I;
    assert(same_class_f(cplus_csinf(nan_z), csinf(nan_z)));
    assert(same_class_f(cplus_clogf(inf_z), clogf(inf_z)));
    assert(same_class_f(cplus_csinhf(inf_z), csinhf(inf_z)));
}

static void test_long_double(void){
    const long double tol = 1e-15L;
    long double complex z = 0.7L + 1.3L * I;

    /* Scalar accessors / elementary functions consistent with the host. */
    assert(near_l(cplus_cabsl(z), cabsl(z), tol));
    assert(near_l(cplus_cargl(z), cargl(z), tol));
    assert(near_l(cplus_creall(z), creall(z), tol));
    assert(near_l(cplus_cimagl(z), cimagl(z), tol));
    assert(near_cl(cplus_conjl(z), conjl(z), tol));
    assert(near_cl(cplus_cexpl(z), cexpl(z), tol));
    assert(near_cl(cplus_csqrtl(z), csqrtl(z), tol));

    /* csinl differential on a known value. */
    assert(near_cl(cplus_csinl(z), csinl(z), tol));
    assert(near_cl(cplus_ccosl(z), ccosl(z), tol));
    assert(near_cl(cplus_ctanl(z), ctanl(z), tol));
    assert(near_cl(cplus_csinhl(z), csinhl(z), tol));
    assert(near_cl(cplus_ctanhl(z), ctanhl(z), tol));

    /* ccoshl identity. */
    assert(near_cl(cplus_ccoshl(z), (cexpl(z) + cexpl(-z)) / 2.0L, tol));

    /* clogl(expl(z)) recovers z, imaginary part modulo 2*pi. */
    long double complex lz = cplus_clogl(cexpl(z));
    assert(near_l(creall(lz), creall(z), tol));
    assert(near_angle_l(cimagl(lz), cimagl(z), tol));

    assert(near_cl(cplus_cpowl(z, 0.0L + 0.0L * I), 1.0L + 0.0L * I, tol));
    assert(near_cl(cplus_cprojl(z), z, tol));

    long double complex nan_z = NAN + 0.0L * I;
    long double complex inf_z = INFINITY + 1.0L * I;
    assert(same_class_l(cplus_csinl(nan_z), csinl(nan_z)));
    assert(same_class_l(cplus_clogl(inf_z), clogl(inf_z)));
    assert(same_class_l(cplus_csinhl(inf_z), csinhl(inf_z)));
}

int main(void){
    test_double();
    test_float();
    test_long_double();
    puts("complex_extra: ok");
    return 0;
}
