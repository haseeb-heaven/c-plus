#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <fenv.h>

int cplus_feclearexcept(int);
int cplus_fetestexcept(int);
int cplus_fegetround(void);
int cplus_fesetround(int);
int cplus_fegetenv(fenv_t*);
int cplus_fesetenv(const fenv_t*);
int cplus_fegetexceptflag(fexcept_t*,int);
int cplus_fesetexceptflag(const fexcept_t*,int);
int cplus_feholdexcept(fenv_t*);
int cplus_feupdateenv(const fenv_t*);

/* ---- F5: every pointer-taking fenv export must reject NULL with EINVAL ----
 * One type-correct adapter per signature keeps the uniform table free of UB. */
static int null_fegetenv(void){ return cplus_fegetenv(NULL); }
static int null_fesetenv(void){ return cplus_fesetenv(NULL); }
static int null_fegetexceptflag(void){ return cplus_fegetexceptflag(NULL, FE_ALL_EXCEPT); }
static int null_fesetexceptflag(void){ return cplus_fesetexceptflag(NULL, FE_ALL_EXCEPT); }
static int null_feholdexcept(void){ return cplus_feholdexcept(NULL); }
static int null_feupdateenv(void){ return cplus_feupdateenv(NULL); }

typedef int (*null_call_t)(void);
static const struct { const char *name; null_call_t call; } null_cases[] = {
    { "fegetenv",        null_fegetenv        },
    { "fesetenv",        null_fesetenv        },
    { "fegetexceptflag", null_fegetexceptflag },
    { "fesetexceptflag", null_fesetexceptflag },
    { "feholdexcept",    null_feholdexcept    },
    { "feupdateenv",     null_feupdateenv     },
};

static void test_required_null_args(void){
    for (size_t i = 0; i < sizeof null_cases / sizeof null_cases[0]; ++i) {
        errno = 0;
        int rc = null_cases[i].call();
        assert(rc != 0);          /* defined failure, never a crash */
        assert(errno == EINVAL);  /* uniform errno contract */
    }
}

int main(void){
    /* ---- original valid-input coverage (unchanged) ---- */
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

    /* ---- F5 regression additions ---- */
    test_required_null_args();
    puts("fenv: ok");
    return 0;
}
