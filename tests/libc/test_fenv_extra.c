#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <fenv.h>

int cplus_feraiseexcept(int);
int cplus_fegetexceptflag(fexcept_t*, int);
int cplus_fesetexceptflag(const fexcept_t*, int);
int cplus_feholdexcept(fenv_t*);
int cplus_feupdateenv(const fenv_t*);

int main(void){
    /* Start from a clean floating-point exception state. */
    assert(feclearexcept(FE_ALL_EXCEPT) == 0);
    assert(fetestexcept(FE_ALL_EXCEPT) == 0);

    /* (1) feraiseexcept(FE_INVALID) raises a real, detectable exception.
     *     Use flag-presence rather than exact-mask equality: a host may raise
     *     additional flags alongside the requested one. */
    assert(cplus_feraiseexcept(FE_INVALID) == 0);
    assert((fetestexcept(FE_INVALID) & FE_INVALID) != 0);
    assert(feclearexcept(FE_ALL_EXCEPT) == 0);

    /* (2) fegetexceptflag / fesetexceptflag round-trip. Save the raised
     *     FE_INVALID flag, clear it, then restore it from the saved object. */
    fexcept_t flags; /* ISO C opaque exception-flag object */
    assert(cplus_feraiseexcept(FE_INVALID) == 0);
    assert((fetestexcept(FE_INVALID) & FE_INVALID) != 0);
    assert(cplus_fegetexceptflag(&flags, FE_INVALID) == 0);
    assert(feclearexcept(FE_ALL_EXCEPT) == 0);
    assert((fetestexcept(FE_INVALID) & FE_INVALID) == 0);
    assert(cplus_fesetexceptflag(&flags, FE_INVALID) == 0);
    assert((fetestexcept(FE_INVALID) & FE_INVALID) != 0);
    assert(feclearexcept(FE_ALL_EXCEPT) == 0);

    /* (3) feholdexcept saves the environment and switches to non-stop mode;
     *     feupdateenv restores it and re-raises flags pending at the call. */
    assert(feclearexcept(FE_ALL_EXCEPT) == 0);
    fenv_t held;
    assert(cplus_feholdexcept(&held) == 0);
    assert(fetestexcept(FE_ALL_EXCEPT) == 0);
    assert(cplus_feraiseexcept(FE_INVALID) == 0);
    assert((fetestexcept(FE_INVALID) & FE_INVALID) != 0);
    assert(cplus_feupdateenv(&held) == 0);
    assert((fetestexcept(FE_INVALID) & FE_INVALID) != 0);
    assert(feclearexcept(FE_ALL_EXCEPT) == 0);

    /* (4) Save/restore of the whole environment with fegetenv/fesetenv still
     *     works alongside the new wrappers (rounding mode round-trips). */
    assert(feclearexcept(FE_ALL_EXCEPT) == 0);
    fenv_t saved;
    int original_round = fegetround();
    assert(fegetenv(&saved) == 0);
    assert(fesetround(FE_UPWARD) == 0);
    assert(fegetround() == FE_UPWARD);
    assert(fesetenv(&saved) == 0);
    assert(fegetround() == original_round);

    /* (5) NULL out-param error paths: rejected with EINVAL and nonzero. */
    errno = 0;
    assert(cplus_fegetexceptflag(NULL, FE_ALL_EXCEPT) != 0);
    assert(errno == EINVAL);
    errno = 0;
    assert(cplus_feholdexcept(NULL) != 0);
    assert(errno == EINVAL);

    /* Leave the floating-point state clean for the caller. */
    assert(fesetround(original_round) == 0);
    assert(feclearexcept(FE_ALL_EXCEPT) == 0);
    puts("fenv_extra: ok");
    return 0;
}
