/* Platform-backed math wrappers (C+ libc slice). Delegate to the installed
 * system libm (no vendored upstream); logged per C+ convention. */
#include "../../log/log.h"
#include <errno.h>
#include <math.h>

double cplus_atan2(double y,double x){ cplus_libc_log("atan2","enter"); return atan2(y,x); }
double cplus_asin(double x){ cplus_libc_log("asin","enter"); return asin(x); }
double cplus_acos(double x){ cplus_libc_log("acos","enter"); return acos(x); }
double cplus_atan(double x){ cplus_libc_log("atan","enter"); return atan(x); }
double cplus_sinh(double x){ cplus_libc_log("sinh","enter"); return sinh(x); }
double cplus_cosh(double x){ cplus_libc_log("cosh","enter"); return cosh(x); }
double cplus_tanh(double x){ cplus_libc_log("tanh","enter"); return tanh(x); }
double cplus_log2(double x){ cplus_libc_log("log2","enter"); return log2(x); }
double cplus_cbrt(double x){ cplus_libc_log("cbrt","enter"); return cbrt(x); }
double cplus_hypot(double x,double y){ cplus_libc_log("hypot","enter"); return hypot(x,y); }
double cplus_round(double x){ cplus_libc_log("round","enter"); return round(x); }
double cplus_trunc(double x){ cplus_libc_log("trunc","enter"); return trunc(x); }
double cplus_fmin(double x,double y){ cplus_libc_log("fmin","enter"); return fmin(x,y); }
double cplus_fmax(double x,double y){ cplus_libc_log("fmax","enter"); return fmax(x,y); }
double cplus_copysign(double x,double y){ cplus_libc_log("copysign","enter"); return copysign(x,y); }
double cplus_ldexp(double x,int e){ cplus_libc_log("ldexp","enter"); return ldexp(x,e); }
double cplus_frexp(double x,int *e){ cplus_libc_log("frexp","enter");
    if (!e) { errno = EINVAL; return 0.0; } return frexp(x,e); }
double cplus_modf(double x,double *ip){ cplus_libc_log("modf","enter");
    if (!ip) { errno = EINVAL; return 0.0; } return modf(x,ip); }
