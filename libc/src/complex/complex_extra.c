/* Platform-backed complex-number wrappers (C+ libc slice) - extra ISO C17 surface.
 * Delegate to the identically-named installed system libc functions; logged per
 * C+ convention. cadd/cmul are NOT provided by the host <complex.h> (verified by
 * compiling a probe), so like complex.c they are intentionally omitted here. */
#include "../../log/log.h"
#include <complex.h>

/* Double-precision missing functions. */
double complex cplus_csin(double complex z){ cplus_libc_log("csin","enter"); return csin(z); }
double complex cplus_ccos(double complex z){ cplus_libc_log("ccos","enter"); return ccos(z); }
double complex cplus_ctan(double complex z){ cplus_libc_log("ctan","enter"); return ctan(z); }
double complex cplus_csinh(double complex z){ cplus_libc_log("csinh","enter"); return csinh(z); }
double complex cplus_ccosh(double complex z){ cplus_libc_log("ccosh","enter"); return ccosh(z); }
double complex cplus_ctanh(double complex z){ cplus_libc_log("ctanh","enter"); return ctanh(z); }
double complex cplus_clog(double complex z){ cplus_libc_log("clog","enter"); return clog(z); }
double complex cplus_cpow(double complex x, double complex y){ cplus_libc_log("cpow","enter"); return cpow(x, y); }
double complex cplus_cproj(double complex z){ cplus_libc_log("cproj","enter"); return cproj(z); }

/* Float variants (suffix f). */
float cplus_cabsf(float complex z){ cplus_libc_log("cabsf","enter"); return cabsf(z); }
float cplus_cargf(float complex z){ cplus_libc_log("cargf","enter"); return cargf(z); }
float cplus_crealf(float complex z){ cplus_libc_log("crealf","enter"); return crealf(z); }
float cplus_cimagf(float complex z){ cplus_libc_log("cimagf","enter"); return cimagf(z); }
float complex cplus_conjf(float complex z){ cplus_libc_log("conjf","enter"); return conjf(z); }
float complex cplus_cexpf(float complex z){ cplus_libc_log("cexpf","enter"); return cexpf(z); }
float complex cplus_csqrtf(float complex z){ cplus_libc_log("csqrtf","enter"); return csqrtf(z); }
float complex cplus_csinf(float complex z){ cplus_libc_log("csinf","enter"); return csinf(z); }
float complex cplus_ccosf(float complex z){ cplus_libc_log("ccosf","enter"); return ccosf(z); }
float complex cplus_ctanf(float complex z){ cplus_libc_log("ctanf","enter"); return ctanf(z); }
float complex cplus_csinhf(float complex z){ cplus_libc_log("csinhf","enter"); return csinhf(z); }
float complex cplus_ccoshf(float complex z){ cplus_libc_log("ccoshf","enter"); return ccoshf(z); }
float complex cplus_ctanhf(float complex z){ cplus_libc_log("ctanhf","enter"); return ctanhf(z); }
float complex cplus_clogf(float complex z){ cplus_libc_log("clogf","enter"); return clogf(z); }
float complex cplus_cpowf(float complex x, float complex y){ cplus_libc_log("cpowf","enter"); return cpowf(x, y); }
float complex cplus_cprojf(float complex z){ cplus_libc_log("cprojf","enter"); return cprojf(z); }

/* Long-double variants (suffix l). */
long double cplus_cabsl(long double complex z){ cplus_libc_log("cabsl","enter"); return cabsl(z); }
long double cplus_cargl(long double complex z){ cplus_libc_log("cargl","enter"); return cargl(z); }
long double cplus_creall(long double complex z){ cplus_libc_log("creall","enter"); return creall(z); }
long double cplus_cimagl(long double complex z){ cplus_libc_log("cimagl","enter"); return cimagl(z); }
long double complex cplus_conjl(long double complex z){ cplus_libc_log("conjl","enter"); return conjl(z); }
long double complex cplus_cexpl(long double complex z){ cplus_libc_log("cexpl","enter"); return cexpl(z); }
long double complex cplus_csqrtl(long double complex z){ cplus_libc_log("csqrtl","enter"); return csqrtl(z); }
long double complex cplus_csinl(long double complex z){ cplus_libc_log("csinl","enter"); return csinl(z); }
long double complex cplus_ccosl(long double complex z){ cplus_libc_log("ccosl","enter"); return ccosl(z); }
long double complex cplus_ctanl(long double complex z){ cplus_libc_log("ctanl","enter"); return ctanl(z); }
long double complex cplus_csinhl(long double complex z){ cplus_libc_log("csinhl","enter"); return csinhl(z); }
long double complex cplus_ccoshl(long double complex z){ cplus_libc_log("ccoshl","enter"); return ccoshl(z); }
long double complex cplus_ctanhl(long double complex z){ cplus_libc_log("ctanhl","enter"); return ctanhl(z); }
long double complex cplus_clogl(long double complex z){ cplus_libc_log("clogl","enter"); return clogl(z); }
long double complex cplus_cpowl(long double complex x, long double complex y){ cplus_libc_log("cpowl","enter"); return cpowl(x, y); }
long double complex cplus_cprojl(long double complex z){ cplus_libc_log("cprojl","enter"); return cprojl(z); }
