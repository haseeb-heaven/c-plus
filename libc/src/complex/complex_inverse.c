/* PLATFORM_BACKED: ISO C complex inverse-trigonometric wrappers (C+ libc slice).
 * Backend: the installed host libc <complex.h> (system C library / libm).
 * Each cplus_<name> below delegates directly to the identically named host
 * function; no algorithm is reimplemented here and no host symbol is
 * interposed. These are NOT a bundled/reimplemented complex-math library.
 */
#include "../../log/log.h"
#include <complex.h>

/* Double-precision inverse trig/hyperbolic. */
double complex cplus_cacos(double complex z){ cplus_libc_log("cacos","enter"); return cacos(z); }
double complex cplus_casin(double complex z){ cplus_libc_log("casin","enter"); return casin(z); }
double complex cplus_catan(double complex z){ cplus_libc_log("catan","enter"); return catan(z); }
double complex cplus_cacosh(double complex z){ cplus_libc_log("cacosh","enter"); return cacosh(z); }
double complex cplus_casinh(double complex z){ cplus_libc_log("casinh","enter"); return casinh(z); }
double complex cplus_catanh(double complex z){ cplus_libc_log("catanh","enter"); return catanh(z); }

/* Float variants (suffix f). */
float complex cplus_cacosf(float complex z){ cplus_libc_log("cacosf","enter"); return cacosf(z); }
float complex cplus_casinf(float complex z){ cplus_libc_log("casinf","enter"); return casinf(z); }
float complex cplus_catanf(float complex z){ cplus_libc_log("catanf","enter"); return catanf(z); }
float complex cplus_cacoshf(float complex z){ cplus_libc_log("cacoshf","enter"); return cacoshf(z); }
float complex cplus_casinhf(float complex z){ cplus_libc_log("casinhf","enter"); return casinhf(z); }
float complex cplus_catanhf(float complex z){ cplus_libc_log("catanhf","enter"); return catanhf(z); }

/* Long-double variants (suffix l). */
long double complex cplus_cacosl(long double complex z){ cplus_libc_log("cacosl","enter"); return cacosl(z); }
long double complex cplus_casinl(long double complex z){ cplus_libc_log("casinl","enter"); return casinl(z); }
long double complex cplus_catanl(long double complex z){ cplus_libc_log("catanl","enter"); return catanl(z); }
long double complex cplus_cacoshl(long double complex z){ cplus_libc_log("cacoshl","enter"); return cacoshl(z); }
long double complex cplus_casinhl(long double complex z){ cplus_libc_log("casinhl","enter"); return casinhl(z); }
long double complex cplus_catanhl(long double complex z){ cplus_libc_log("catanhl","enter"); return catanhl(z); }
