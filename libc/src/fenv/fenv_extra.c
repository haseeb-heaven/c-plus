/* Platform-backed floating-point environment extras (C+ libc slice).
 *
 * These five routines are ISO C <fenv.h> functions available on Linux, macOS,
 * and Windows. Each wrapper delegates directly to the identically named host
 * function; the host runtime is the single source of truth for all floating-
 * point exception and rounding-mode semantics. This file bundles NO
 * reimplementation of the FP environment: it only forwards, and it exports
 * only cplus_* names (host symbols are neither interposed nor overridden).
 * Calls are logged per the C+ libc convention (opt-in, atomic-guarded).
 *
 * Hardening: a NULL out-param (fexcept_t * / fenv_t * that the host would write
 * through) is rejected with errno = EINVAL and a nonzero return. This affects
 * only inputs that are undefined behaviour for the ISO C function; valid-input
 * behaviour is passed through to the host unchanged. */
#include "../../log/log.h"
#include <errno.h>
#include <fenv.h>

/* ISO C feraiseexcept(): raise the named floating-point exceptions. */
int cplus_feraiseexcept(int excepts) {
    cplus_libc_log("feraiseexcept", "enter");
    return feraiseexcept(excepts);
}

/* ISO C fegetexceptflag(): store the named exception flags in *flagp.
 * The public signature uses the ISO C fexcept_t * opaque flag-object type,
 * so the host call is a verbatim pass-through with no reinterpretation. */
int cplus_fegetexceptflag(fexcept_t *flagp, int excepts) {
    cplus_libc_log("fegetexceptflag", "enter");
    if (!flagp) { errno = EINVAL; return -1; }
    return fegetexceptflag(flagp, excepts);
}

/* ISO C fesetexceptflag(): set the named exception flags from *flagp. */
int cplus_fesetexceptflag(const fexcept_t *flagp, int excepts) {
    cplus_libc_log("fesetexceptflag", "enter");
    return fesetexceptflag(flagp, excepts);
}

/* ISO C feholdexcept(): save the environment in *envp, then install
 * non-stop (non-trapping) mode. */
int cplus_feholdexcept(fenv_t *envp) {
    cplus_libc_log("feholdexcept", "enter");
    if (!envp) { errno = EINVAL; return -1; }
    return feholdexcept(envp);
}

/* ISO C feupdateenv(): restore the environment from *envp, then re-raise the
 * exceptions that were set at the time of the call. */
int cplus_feupdateenv(const fenv_t *envp) {
    cplus_libc_log("feupdateenv", "enter");
    return feupdateenv(envp);
}
