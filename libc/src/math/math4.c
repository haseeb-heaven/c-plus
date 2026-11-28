/* Platform-backed math wrappers (C+ libc slice), batch 4. Delegate to the
 * installed system libm (no vendored upstream); logged per C+ convention.
 * NOTE: fdim() already exists in math3.c and is therefore not re-defined.
 * NOTE: nanf() now lives in the complete float family in math_float.c and is
 * therefore not re-defined here (avoids a duplicate cplus_nanf symbol). */
#include "../../log/log.h"
#include <errno.h>
#include <math.h>

double cplus_fma(double x, double y, double z) { cplus_libc_log("fma", "enter"); return fma(x, y, z); }
double cplus_remquo(double x, double y, int *quo) {
    cplus_libc_log("remquo", "enter");
    if (!quo) { errno = EINVAL; return 0.0; }
    return remquo(x, y, quo);
}
double cplus_logb(double x) { cplus_libc_log("logb", "enter"); return logb(x); }
int cplus_ilogb(double x) { cplus_libc_log("ilogb", "enter"); return ilogb(x); }
double cplus_exp2(double x) { cplus_libc_log("exp2", "enter"); return exp2(x); }
double cplus_nearbyint(double x) { cplus_libc_log("nearbyint", "enter"); return nearbyint(x); }
long cplus_lrint(double x) { cplus_libc_log("lrint", "enter"); return lrint(x); }
long long cplus_llrint(double x) { cplus_libc_log("llrint", "enter"); return llrint(x); }
double cplus_scalbn(double x, int n) { cplus_libc_log("scalbn", "enter"); return scalbn(x, n); }
