/* PLATFORM-BACKED float-suffix <math.h> wrappers (C+ libc slice).
 *
 * Every cplus_<name>f() function below delegates directly to the identically
 * named host <math.h> float function from the installed system libm (e.g.
 * cplus_sinf delegates to host sinf, cplus_nanf to host nanf). There is no
 * bundled/from-scratch replacement algorithm: all standard ISO C17 math
 * domain/range/error semantics (errno, NaN/Inf classification, signed zero,
 * rounding) are produced by the host libm and are intentionally NOT
 * re-implemented here. Calls are logged per the C+ convention.
 *
 * Hardening extension (valid inputs are never affected): the three functions
 * with pointer out-parameters (frexpf, modff, remquof) guard a NULL pointer,
 * which would otherwise be undefined behavior, by setting errno = EINVAL and
 * returning 0.0f.
 */
#include "../../log/log.h"
#include <errno.h>
#include <math.h>

float cplus_sinf(float x){ cplus_libc_log("sinf","enter"); return sinf(x); }
float cplus_cosf(float x){ cplus_libc_log("cosf","enter"); return cosf(x); }
float cplus_tanf(float x){ cplus_libc_log("tanf","enter"); return tanf(x); }
float cplus_asinf(float x){ cplus_libc_log("asinf","enter"); return asinf(x); }
float cplus_acosf(float x){ cplus_libc_log("acosf","enter"); return acosf(x); }
float cplus_atanf(float x){ cplus_libc_log("atanf","enter"); return atanf(x); }
float cplus_atan2f(float y,float x){ cplus_libc_log("atan2f","enter"); return atan2f(y,x); }
float cplus_sinhf(float x){ cplus_libc_log("sinhf","enter"); return sinhf(x); }
float cplus_coshf(float x){ cplus_libc_log("coshf","enter"); return coshf(x); }
float cplus_tanhf(float x){ cplus_libc_log("tanhf","enter"); return tanhf(x); }
float cplus_asinhf(float x){ cplus_libc_log("asinhf","enter"); return asinhf(x); }
float cplus_acoshf(float x){ cplus_libc_log("acoshf","enter"); return acoshf(x); }
float cplus_atanhf(float x){ cplus_libc_log("atanhf","enter"); return atanhf(x); }
float cplus_expf(float x){ cplus_libc_log("expf","enter"); return expf(x); }
float cplus_exp2f(float x){ cplus_libc_log("exp2f","enter"); return exp2f(x); }
float cplus_expm1f(float x){ cplus_libc_log("expm1f","enter"); return expm1f(x); }
float cplus_logf(float x){ cplus_libc_log("logf","enter"); return logf(x); }
float cplus_log2f(float x){ cplus_libc_log("log2f","enter"); return log2f(x); }
float cplus_log10f(float x){ cplus_libc_log("log10f","enter"); return log10f(x); }
float cplus_log1pf(float x){ cplus_libc_log("log1pf","enter"); return log1pf(x); }
float cplus_powf(float x,float y){ cplus_libc_log("powf","enter"); return powf(x,y); }
float cplus_sqrtf(float x){ cplus_libc_log("sqrtf","enter"); return sqrtf(x); }
float cplus_cbrtf(float x){ cplus_libc_log("cbrtf","enter"); return cbrtf(x); }
float cplus_hypotf(float x,float y){ cplus_libc_log("hypotf","enter"); return hypotf(x,y); }
float cplus_floorf(float x){ cplus_libc_log("floorf","enter"); return floorf(x); }
float cplus_ceilf(float x){ cplus_libc_log("ceilf","enter"); return ceilf(x); }
float cplus_truncf(float x){ cplus_libc_log("truncf","enter"); return truncf(x); }
float cplus_roundf(float x){ cplus_libc_log("roundf","enter"); return roundf(x); }
float cplus_nearbyintf(float x){ cplus_libc_log("nearbyintf","enter"); return nearbyintf(x); }
float cplus_rintf(float x){ cplus_libc_log("rintf","enter"); return rintf(x); }
long cplus_lrintf(float x){ cplus_libc_log("lrintf","enter"); return lrintf(x); }
long long cplus_llrintf(float x){ cplus_libc_log("llrintf","enter"); return llrintf(x); }
long cplus_lroundf(float x){ cplus_libc_log("lroundf","enter"); return lroundf(x); }
long long cplus_llroundf(float x){ cplus_libc_log("llroundf","enter"); return llroundf(x); }
float cplus_fabsf(float x){ cplus_libc_log("fabsf","enter"); return fabsf(x); }
float cplus_fmodf(float x,float y){ cplus_libc_log("fmodf","enter"); return fmodf(x,y); }
float cplus_remainderf(float x,float y){ cplus_libc_log("remainderf","enter"); return remainderf(x,y); }
float cplus_fdimf(float x,float y){ cplus_libc_log("fdimf","enter"); return fdimf(x,y); }
float cplus_fminf(float x,float y){ cplus_libc_log("fminf","enter"); return fminf(x,y); }
float cplus_fmaxf(float x,float y){ cplus_libc_log("fmaxf","enter"); return fmaxf(x,y); }
float cplus_fmaf(float x,float y,float z){ cplus_libc_log("fmaf","enter"); return fmaf(x,y,z); }
float cplus_copysignf(float x,float y){ cplus_libc_log("copysignf","enter"); return copysignf(x,y); }
float cplus_ldexpf(float x,int e){ cplus_libc_log("ldexpf","enter"); return ldexpf(x,e); }
float cplus_scalbnf(float x,int n){ cplus_libc_log("scalbnf","enter"); return scalbnf(x,n); }
float cplus_scalblnf(float x,long n){ cplus_libc_log("scalblnf","enter"); return scalblnf(x,n); }
int cplus_ilogbf(float x){ cplus_libc_log("ilogbf","enter"); return ilogbf(x); }
float cplus_logbf(float x){ cplus_libc_log("logbf","enter"); return logbf(x); }
float cplus_erff(float x){ cplus_libc_log("erff","enter"); return erff(x); }
float cplus_erfcf(float x){ cplus_libc_log("erfcf","enter"); return erfcf(x); }
float cplus_tgammaf(float x){ cplus_libc_log("tgammaf","enter"); return tgammaf(x); }
float cplus_lgammaf(float x){ cplus_libc_log("lgammaf","enter"); return lgammaf(x); }
float cplus_nextafterf(float x,float y){ cplus_libc_log("nextafterf","enter"); return nextafterf(x,y); }
float cplus_nexttowardf(float x,long double y){ cplus_libc_log("nexttowardf","enter"); return nexttowardf(x,y); }
float cplus_nanf(const char *tagp){ cplus_libc_log("nanf","enter"); return nanf(tagp); }

/* Hardened pointer-out-parameter paths: NULL would be undefined behavior. */
float cplus_frexpf(float x,int *e){
    cplus_libc_log("frexpf","enter");
    if(!e){ errno = EINVAL; return 0.0f; }
    return frexpf(x,e);
}
float cplus_modff(float x,float *ip){
    cplus_libc_log("modff","enter");
    if(!ip){ errno = EINVAL; return 0.0f; }
    return modff(x,ip);
}
float cplus_remquof(float x,float y,int *quo){
    cplus_libc_log("remquof","enter");
    if(!quo){ errno = EINVAL; return 0.0f; }
    return remquof(x,y,quo);
}
