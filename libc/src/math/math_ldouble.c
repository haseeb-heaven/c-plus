/*
 * PLATFORM-BACKED ISO C17 <math.h> long double (suffix `l`) family.
 *
 * This file provides namespaced `cplus_` wrappers around the host C library's
 * identically named long double math functions. It contains NO from-scratch
 * math algorithms: every wrapper forwards directly to the corresponding host
 * symbol (e.g. cplus_sinl -> sinl, cplus_fmal -> fmal, cplus_nanl -> nanl).
 * The host libm is the single source of truth for all numerical results.
 *
 * Domain/range errors, errno values, and floating-point exception flags are
 * produced by the host backend; this layer does not reimplement or override
 * them. The only behavior added here is defined hardening for otherwise
 * undefined inputs: a NULL out-parameter (frexpl / modfl / remquol) sets errno
 * to EINVAL and returns 0.0L. Valid inputs are passed through unchanged.
 *
 * NOTE ON PRECISION: on macOS arm64 (this build host), `long double` is an
 * alias for `double` (both are IEEE-754 binary64, 64-bit). These `l` functions
 * therefore do not provide extended precision on this platform. That is a
 * property of the host ABI, not of this wrapper layer; the wrappers remain
 * correct and faithfully mirror the host on every supported target.
 */
#include "../../log/log.h"
#include <errno.h>
#include <math.h>

/* Trigonometric */
long double cplus_sinl(long double x){ cplus_libc_log("sinl","enter"); return sinl(x); }
long double cplus_cosl(long double x){ cplus_libc_log("cosl","enter"); return cosl(x); }
long double cplus_tanl(long double x){ cplus_libc_log("tanl","enter"); return tanl(x); }
long double cplus_asinl(long double x){ cplus_libc_log("asinl","enter"); return asinl(x); }
long double cplus_acosl(long double x){ cplus_libc_log("acosl","enter"); return acosl(x); }
long double cplus_atanl(long double x){ cplus_libc_log("atanl","enter"); return atanl(x); }
long double cplus_atan2l(long double x, long double y){ cplus_libc_log("atan2l","enter"); return atan2l(x,y); }

/* Hyperbolic */
long double cplus_sinhl(long double x){ cplus_libc_log("sinhl","enter"); return sinhl(x); }
long double cplus_coshl(long double x){ cplus_libc_log("coshl","enter"); return coshl(x); }
long double cplus_tanhl(long double x){ cplus_libc_log("tanhl","enter"); return tanhl(x); }
long double cplus_asinhl(long double x){ cplus_libc_log("asinhl","enter"); return asinhl(x); }
long double cplus_acoshl(long double x){ cplus_libc_log("acoshl","enter"); return acoshl(x); }
long double cplus_atanhl(long double x){ cplus_libc_log("atanhl","enter"); return atanhl(x); }

/* Exponential and logarithmic */
long double cplus_expl(long double x){ cplus_libc_log("expl","enter"); return expl(x); }
long double cplus_exp2l(long double x){ cplus_libc_log("exp2l","enter"); return exp2l(x); }
long double cplus_expm1l(long double x){ cplus_libc_log("expm1l","enter"); return expm1l(x); }
long double cplus_logl(long double x){ cplus_libc_log("logl","enter"); return logl(x); }
long double cplus_log2l(long double x){ cplus_libc_log("log2l","enter"); return log2l(x); }
long double cplus_log10l(long double x){ cplus_libc_log("log10l","enter"); return log10l(x); }
long double cplus_log1pl(long double x){ cplus_libc_log("log1pl","enter"); return log1pl(x); }

/* Power */
long double cplus_powl(long double x, long double y){ cplus_libc_log("powl","enter"); return powl(x,y); }
long double cplus_sqrtl(long double x){ cplus_libc_log("sqrtl","enter"); return sqrtl(x); }
long double cplus_cbrtl(long double x){ cplus_libc_log("cbrtl","enter"); return cbrtl(x); }
long double cplus_hypotl(long double x, long double y){ cplus_libc_log("hypotl","enter"); return hypotl(x,y); }

/* Rounding */
long double cplus_floorl(long double x){ cplus_libc_log("floorl","enter"); return floorl(x); }
long double cplus_ceill(long double x){ cplus_libc_log("ceill","enter"); return ceill(x); }
long double cplus_truncl(long double x){ cplus_libc_log("truncl","enter"); return truncl(x); }
long double cplus_roundl(long double x){ cplus_libc_log("roundl","enter"); return roundl(x); }
long double cplus_nearbyintl(long double x){ cplus_libc_log("nearbyintl","enter"); return nearbyintl(x); }
long double cplus_rintl(long double x){ cplus_libc_log("rintl","enter"); return rintl(x); }
long cplus_lrintl(long double x){ cplus_libc_log("lrintl","enter"); return lrintl(x); }
long long cplus_llrintl(long double x){ cplus_libc_log("llrintl","enter"); return llrintl(x); }
long cplus_lroundl(long double x){ cplus_libc_log("lroundl","enter"); return lroundl(x); }
long long cplus_llroundl(long double x){ cplus_libc_log("llroundl","enter"); return llroundl(x); }

/* Error and gamma */
long double cplus_erfl(long double x){ cplus_libc_log("erfl","enter"); return erfl(x); }
long double cplus_erfcl(long double x){ cplus_libc_log("erfcl","enter"); return erfcl(x); }
long double cplus_tgammal(long double x){ cplus_libc_log("tgammal","enter"); return tgammal(x); }
long double cplus_lgammal(long double x){ cplus_libc_log("lgammal","enter"); return lgammal(x); }

/* Floating-point manipulation and assistance */
long double cplus_fabsl(long double x){ cplus_libc_log("fabsl","enter"); return fabsl(x); }
long double cplus_fmodl(long double x, long double y){ cplus_libc_log("fmodl","enter"); return fmodl(x,y); }
long double cplus_remainderl(long double x, long double y){ cplus_libc_log("remainderl","enter"); return remainderl(x,y); }

long double cplus_remquol(long double x, long double y, int *quo){
    cplus_libc_log("remquol","enter");
    if(!quo){ errno = EINVAL; return 0.0L; }
    return remquol(x,y,quo);
}

long double cplus_fdiml(long double x, long double y){ cplus_libc_log("fdiml","enter"); return fdiml(x,y); }
long double cplus_fminl(long double x, long double y){ cplus_libc_log("fminl","enter"); return fminl(x,y); }
long double cplus_fmaxl(long double x, long double y){ cplus_libc_log("fmaxl","enter"); return fmaxl(x,y); }
long double cplus_fmal(long double x, long double y, long double z){ cplus_libc_log("fmal","enter"); return fmal(x,y,z); }
long double cplus_copysignl(long double x, long double y){ cplus_libc_log("copysignl","enter"); return copysignl(x,y); }
long double cplus_ldexpl(long double x, int n){ cplus_libc_log("ldexpl","enter"); return ldexpl(x,n); }

long double cplus_frexpl(long double x, int *exp){
    cplus_libc_log("frexpl","enter");
    if(!exp){ errno = EINVAL; return 0.0L; }
    return frexpl(x,exp);
}

long double cplus_modfl(long double x, long double *iptr){
    cplus_libc_log("modfl","enter");
    if(!iptr){ errno = EINVAL; return 0.0L; }
    return modfl(x,iptr);
}

long double cplus_scalbnl(long double x, int n){ cplus_libc_log("scalbnl","enter"); return scalbnl(x,n); }
long double cplus_scalblnl(long double x, long n){ cplus_libc_log("scalblnl","enter"); return scalblnl(x,n); }
int cplus_ilogbl(long double x){ cplus_libc_log("ilogbl","enter"); return ilogbl(x); }
long double cplus_logbl(long double x){ cplus_libc_log("logbl","enter"); return logbl(x); }
long double cplus_nextafterl(long double x, long double y){ cplus_libc_log("nextafterl","enter"); return nextafterl(x,y); }
long double cplus_nexttowardl(long double x, long double y){ cplus_libc_log("nexttowardl","enter"); return nexttowardl(x,y); }
long double cplus_nanl(const char *tagp){ cplus_libc_log("nanl","enter"); return nanl(tagp); }
