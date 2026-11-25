/* C+ signal wrappers.
 *
 * The original handler/raise coverage is kept verbatim. F4 regression cases
 * are table-driven: each distinct wrapper signature gets a type-correct
 * adapter function so invoking it through the uniform `int (*)(void)` table
 * introduces no UB of its own. Only pointers the POSIX API requires to be
 * non-NULL are tested; ISO/POSIX-optional NULLs are asserted to still work. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>

void (*cplus_signal(int, void(*)(int)))(int);
int cplus_raise(int);
int cplus_sigemptyset(sigset_t*);
int cplus_sigfillset(sigset_t*);
int cplus_sigaddset(sigset_t*,int);
int cplus_sigdelset(sigset_t*,int);
int cplus_sigismember(const sigset_t*,int);
int cplus_sigaction(int,const struct sigaction*,struct sigaction*);
int cplus_sigprocmask(int,const sigset_t*,sigset_t*);
int cplus_sigsuspend(const sigset_t*);
int cplus_sigwait(const sigset_t*,int*);

static volatile sig_atomic_t got=0;
static void h(int s){ (void)s; got=1; }

/* ---- F4: required pointer args must reject NULL with EINVAL ---- */
static int null_sigemptyset(void){ return cplus_sigemptyset(NULL); }
static int null_sigfillset(void){ return cplus_sigfillset(NULL); }
static int null_sigaddset(void){ return cplus_sigaddset(NULL, SIGUSR1); }
static int null_sigdelset(void){ return cplus_sigdelset(NULL, SIGUSR1); }
static int null_sigismember(void){ return cplus_sigismember(NULL, SIGUSR1); }
static int null_sigsuspend(void){ return cplus_sigsuspend(NULL); }
static int null_sigwait_set(void){ int s = 0; return cplus_sigwait(NULL, &s); }
static int null_sigwait_sig(void){
    sigset_t set;
    assert(cplus_sigemptyset(&set) == 0);
    return cplus_sigwait(&set, NULL);
}

typedef int (*null_call_t)(void);
static const struct { const char *name; null_call_t call; } null_cases[] = {
    { "sigemptyset",  null_sigemptyset  },
    { "sigfillset",   null_sigfillset   },
    { "sigaddset",    null_sigaddset    },
    { "sigdelset",    null_sigdelset    },
    { "sigismember",  null_sigismember  },
    { "sigsuspend",   null_sigsuspend   },
    { "sigwait(set)", null_sigwait_set  },
    { "sigwait(sig)", null_sigwait_sig  },
};

static void test_required_null_args(void){
    for (size_t i = 0; i < sizeof null_cases / sizeof null_cases[0]; ++i) {
        errno = 0;
        int rc = null_cases[i].call();
        assert(rc != 0);          /* defined failure, never a crash */
        assert(errno == EINVAL);  /* uniform errno contract */
    }
}

static void test_optional_nulls_still_valid(void){
    sigset_t cur;
    /* set == NULL is a legal current-mask query; how is ignored (including the
     * SIG_BLOCK case the audit called a crash). */
    assert(cplus_sigprocmask(SIG_SETMASK, NULL, &cur) == 0);
    assert(cplus_sigprocmask(SIG_BLOCK, NULL, &cur) == 0);
    /* sigaction's act and oldact are both POSIX-optional. */
    assert(cplus_sigaction(SIGUSR1, NULL, NULL) == 0);
}

static void test_sigdfl_not_rejected(void){
    /* SIG_DFL is (void(*)(int))0 on this host. cplus_signal() therefore must
     * NOT guard its handler pointer with `if (!func)`, or this valid reset
     * would be rejected. No signal is raised while the default is installed. */
    void (*prev)(int) = cplus_signal(SIGUSR1, SIG_DFL);
    assert(prev != SIG_ERR);
    assert(cplus_signal(SIGUSR1, h) != SIG_ERR); /* restore a safe handler */
    (void)prev;
}

int main(void){
    /* ---- original valid-input coverage (unchanged) ---- */
    for(int i=0;i<10;i++){ got=0; cplus_signal(SIGUSR1,h); cplus_raise(SIGUSR1); assert(got==1); }

    /* ---- F4 regression additions ---- */
    test_required_null_args();
    test_optional_nulls_still_valid();
    test_sigdfl_not_rejected();
    return 0;
}
