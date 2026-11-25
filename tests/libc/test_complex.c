#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <complex.h>

double cplus_cabs(double complex);
double cplus_carg(double complex);
double cplus_creal(double complex);
double cplus_cimag(double complex);
double complex cplus_conj(double complex);
double complex cplus_cexp(double complex);
double complex cplus_csqrt(double complex);

static int near(double a, double b){ double d = a - b; if (d < 0) d = -d; return d < 1e-9; }

int main(void){
    const double PI = 3.14159265358979323846;
    double complex z = 3.0 + 4.0 * I;
    double complex w = 1.0 - 2.0 * I;

    /* modulus / real / imaginary parts */
    assert(near(cplus_cabs(z), 5.0));
    assert(near(cplus_cabs(w), sqrt(5.0)));
    assert(near(cplus_creal(z), 3.0));
    assert(near(cplus_cimag(z), 4.0));
    assert(near(cplus_creal(w), 1.0));
    assert(near(cplus_cimag(w), -2.0));

    /* argument */
    assert(near(cplus_carg(1.0 + 0.0 * I), 0.0));
    assert(near(cplus_carg(0.0 + 1.0 * I), PI / 2.0));
    assert(near(cplus_carg(-1.0 + 0.0 * I), PI));

    /* conjugate flips the sign of the imaginary part */
    double complex cj = cplus_conj(z);
    assert(near(cplus_creal(cj), 3.0));
    assert(near(cplus_cimag(cj), -4.0));

    /* exp(0) == 1 */
    double complex e0 = cplus_cexp(0.0 + 0.0 * I);
    assert(near(cplus_creal(e0), 1.0));
    assert(near(cplus_cimag(e0), 0.0));

    /* Euler: exp(i*pi) == -1 */
    double complex euler = cplus_cexp(0.0 + PI * I);
    assert(near(cplus_creal(euler), -1.0));
    assert(near(cplus_cimag(euler), 0.0));

    /* principal square roots */
    double complex s = cplus_csqrt(4.0 + 0.0 * I);
    assert(near(cplus_creal(s), 2.0));
    assert(near(cplus_cimag(s), 0.0));
    double complex si = cplus_csqrt(-1.0 + 0.0 * I);
    assert(near(cplus_creal(si), 0.0));
    assert(near(cplus_cimag(si), 1.0));

    /* |exp(i*theta)| == 1 */
    assert(near(cplus_cabs(cplus_cexp(0.0 + (PI / 3.0) * I)), 1.0));

    /* conj(a*b) == conj(a)*conj(b), checked through the wrappers */
    double complex lhs = cplus_conj(z * w);
    double complex rhs = cplus_conj(z) * cplus_conj(w);
    assert(near(cplus_creal(lhs), cplus_creal(rhs)));
    assert(near(cplus_cimag(lhs), cplus_cimag(rhs)));

    /* |a*conj(a)| == |a|^2 */
    assert(near(cplus_creal(z * cplus_conj(z)), 25.0));
    assert(near(cplus_cimag(z * cplus_conj(z)), 0.0));

    puts("complex: ok");
    return 0;
}
