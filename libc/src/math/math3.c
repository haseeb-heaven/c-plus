/* Platform-backed math wrappers (C+ libc slice), batch 3. Delegate to the
 * installed system libm (no vendored upstream); logged per C+ convention. */
#include "../../log/log.h"
#include <math.h>

double cplus_nan(const char *tagp){ cplus_libc_log("nan","enter"); return nan(tagp); }
double cplus_nextafter(double x,double y){ cplus_libc_log("nextafter","enter"); return nextafter(x,y); }
double cplus_fdim(double x,double y){ cplus_libc_log("fdim","enter"); return fdim(x,y); }
double cplus_remainder(double x,double y){ cplus_libc_log("remainder","enter"); return remainder(x,y); }
double cplus_log1p(double x){ cplus_libc_log("log1p","enter"); return log1p(x); }
double cplus_expm1(double x){ cplus_libc_log("expm1","enter"); return expm1(x); }
double cplus_asinh(double x){ cplus_libc_log("asinh","enter"); return asinh(x); }
double cplus_acosh(double x){ cplus_libc_log("acosh","enter"); return acosh(x); }
double cplus_atanh(double x){ cplus_libc_log("atanh","enter"); return atanh(x); }
double cplus_erf(double x){ cplus_libc_log("erf","enter"); return erf(x); }
double cplus_erfc(double x){ cplus_libc_log("erfc","enter"); return erfc(x); }
double cplus_tgamma(double x){ cplus_libc_log("tgamma","enter"); return tgamma(x); }
double cplus_lgamma(double x){ cplus_libc_log("lgamma","enter"); return lgamma(x); }
