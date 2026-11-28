/* Covers the platform-backed POSIX signal wrappers added in batch 7
 * (sigemptyset / sigfillset / sigaddset / sigdelset / sigismember / sigaction /
 * sigprocmask / sigsuspend / kill / sigwait) and the int-form strerror_r()
 * wrapper.
 *
 * Only defined, safe behaviour is exercised: signal-set bookkeeping, a SIGUSR1
 * handler installed through cplus_sigaction() and raised with cplus_raise(),
 * mask queries via sigprocmask(), a self-delivered SIGCONT (whose default
 * action is a harmless "continue"), and strerror_r() for a known errno. The
 * blocking primitives cplus_sigsuspend() / cplus_sigwait() are exported but
 * deliberately not called here (they would block without a delivered signal).
 * SIGSEGV / SIGKILL (and every other process-killing signal) are never used. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <assert.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

int cplus_sigemptyset(sigset_t*);
int cplus_sigfillset(sigset_t*);
int cplus_sigaddset(sigset_t*,int);
int cplus_sigdelset(sigset_t*,int);
int cplus_sigismember(const sigset_t*,int);
int cplus_sigaction(int,const struct sigaction*,struct sigaction*);
int cplus_sigprocmask(int,const sigset_t*,sigset_t*);
int cplus_sigsuspend(const sigset_t*);
int cplus_kill(pid_t,int);
int cplus_sigwait(const sigset_t*,int*);
int cplus_strerror_r(int,char*,size_t);
int cplus_raise(int);

static volatile sig_atomic_t got = 0;
static void h(int s){ (void)s; got = 1; }

int main(void){
    /* ---- sigemptyset / sigaddset / sigdelset / sigismember bookkeeping ---- */
    sigset_t set;
    assert(cplus_sigemptyset(&set) == 0);            /* 1  empty set */
    assert(cplus_sigismember(&set, SIGUSR1) == 0);   /* 2  empty => not a member */
    assert(cplus_sigaddset(&set, SIGUSR1) == 0);     /* 3  add */
    assert(cplus_sigismember(&set, SIGUSR1) == 1);   /* 4  now a member */
    assert(cplus_sigaddset(&set, SIGUSR2) == 0);     /* 5  add a second */
    assert(cplus_sigismember(&set, SIGUSR2) == 1);   /* 6  both present */
    assert(cplus_sigismember(&set, SIGINT) == 0);    /* 7  un-added signal absent */
    assert(cplus_sigdelset(&set, SIGUSR1) == 0);     /* 8  remove one */
    assert(cplus_sigismember(&set, SIGUSR1) == 0);   /* 9  removed */
    assert(cplus_sigismember(&set, SIGUSR2) == 1);   /* 10 removal was selective */

    /* sigfillset: every signal becomes a member */
    assert(cplus_sigfillset(&set) == 0);             /* 11 */
    assert(cplus_sigismember(&set, SIGINT) == 1);    /* 12 */
    assert(cplus_sigismember(&set, SIGUSR1) == 1);   /* 13 */

    /* ---- install a SIGUSR1 handler via cplus_sigaction and raise it ---- */
    struct sigaction sa, old;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = h;
    assert(cplus_sigemptyset(&sa.sa_mask) == 0);     /* 14 */
    sa.sa_flags = 0;
    assert(cplus_sigaction(SIGUSR1, &sa, &old) == 0);/* 15 install */
    got = 0;
    assert(cplus_raise(SIGUSR1) == 0);               /* 16 deliver */
    assert(got == 1);                                /* 17 handler ran */
    assert(cplus_sigaction(SIGUSR1, &old, NULL) == 0);/* 18 restore old dsp */

    /* ---- sigprocmask: block SIGUSR2, query it, then restore ---- */
    sigset_t blocked, prev;
    assert(cplus_sigemptyset(&blocked) == 0);        /* 19 */
    assert(cplus_sigaddset(&blocked, SIGUSR2) == 0); /* 20 */
    assert(cplus_sigprocmask(SIG_BLOCK, &blocked, &prev) == 0); /* 21 block */
    {
        sigset_t cur;
        assert(cplus_sigprocmask(SIG_SETMASK, NULL, &cur) == 0); /* 22 query */
        assert(cplus_sigismember(&cur, SIGUSR2) == 1);           /* 23 blocked */
    }
    assert(cplus_sigprocmask(SIG_SETMASK, &prev, NULL) == 0);    /* 24 restore */

    /* ---- kill(): self-deliver SIGCONT (default action: harmless continue) ---- */
    assert(cplus_kill(getpid(), SIGCONT) == 0);      /* 25 */

    /* ---- strerror_r() int form: fills a buffer for a known errno ---- */
    char buf[128];
    buf[0] = '\0';
    assert(cplus_strerror_r(EINVAL, buf, sizeof buf) == 0); /* 26 */
    assert(buf[0] != '\0');                                 /* 27 non-empty */
    assert(strlen(buf) < sizeof buf);                       /* 28 terminated */
    return 0;
}
