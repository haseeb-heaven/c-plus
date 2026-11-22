#include <assert.h>
#include <stdio.h>
#include <fenv.h>

int cplus_feclearexcept(int);
int cplus_fetestexcept(int);
int cplus_fegetround(void);
int cplus_fesetround(int);
int cplus_fegetenv(fenv_t*);
int cplus_fesetenv(const fenv_t*);

int main(void){
    /* clearing then testing yields a zero exception mask */
    assert(cplus_feclearexcept(FE_ALL_EXCEPT) == 0);
    assert(cplus_fetestexcept(FE_ALL_EXCEPT) == 0);

    /* the current rounding mode is one of the defined values */
    int saved = cplus_fegetround();
    assert(saved == FE_TONEAREST || saved == FE_DOWNWARD ||
           saved == FE_UPWARD  || saved == FE_TOWARDZERO);

    /* each rounding direction round-trips */
    assert(cplus_fesetround(FE_TONEAREST) == 0);
    assert(cplus_fegetround() == FE_TONEAREST);
    assert(cplus_fesetround(FE_DOWNWARD) == 0);
    assert(cplus_fegetround() == FE_DOWNWARD);
    assert(cplus_fesetround(FE_UPWARD) == 0);
    assert(cplus_fegetround() == FE_UPWARD);
    assert(cplus_fesetround(FE_TOWARDZERO) == 0);
    assert(cplus_fegetround() == FE_TOWARDZERO);

    /* saving and restoring the whole environment undoes a mode change */
    fenv_t env;
    assert(cplus_fesetround(FE_TONEAREST) == 0);
    assert(cplus_fegetenv(&env) == 0);
    assert(cplus_fesetround(FE_UPWARD) == 0);
    assert(cplus_fegetround() == FE_UPWARD);
    assert(cplus_fesetenv(&env) == 0);
    assert(cplus_fegetround() == FE_TONEAREST);

    /* restoring a saved environment after clearing exceptions succeeds */
    assert(cplus_feclearexcept(FE_ALL_EXCEPT) == 0);
    assert(cplus_fesetenv(&env) == 0);
    assert(cplus_fetestexcept(FE_ALL_EXCEPT) == 0);

    assert(cplus_fesetround(saved) == 0);
    puts("fenv: ok");
    return 0;
}
