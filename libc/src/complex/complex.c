/* Platform-backed complex-number wrappers (C+ libc slice).
 * Delegate to the installed system libc; logged per C+ convention.
 * NOTE: cadd/cmul are NOT provided by the host <complex.h> (verified by
 * compiling a probe), so they are intentionally omitted here. */
#include "../../log/log.h"
#include <complex.h>

double cplus_cabs(double complex z){ cplus_libc_log("cabs","enter"); return cabs(z); }
double cplus_carg(double complex z){ cplus_libc_log("carg","enter"); return carg(z); }
double cplus_creal(double complex z){ cplus_libc_log("creal","enter"); return creal(z); }
double cplus_cimag(double complex z){ cplus_libc_log("cimag","enter"); return cimag(z); }
double complex cplus_conj(double complex z){ cplus_libc_log("conj","enter"); return conj(z); }
double complex cplus_cexp(double complex z){ cplus_libc_log("cexp","enter"); return cexp(z); }
double complex cplus_csqrt(double complex z){ cplus_libc_log("csqrt","enter"); return csqrt(z); }
