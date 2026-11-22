/* Platform-backed POSIX signal-set / signal-control wrappers (C+ libc slice),
 * batch 7. Delegate to the installed system libc; logged per C+ convention.
 *
 * Host probe (Apple clang 16 / Darwin, _POSIX_C_SOURCE 200809L): sigemptyset,
 * sigfillset, sigaddset, sigdelset, sigismember, sigaction, sigprocmask,
 * sigsuspend, kill and sigwait are all present. cplus_sigaction keeps the
 * POSIX signature `int (int, const struct sigaction *, struct sigaction *)`. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include "../../log/log.h"
#include <signal.h>
#include <sys/types.h>   /* pid_t for kill() */

int cplus_sigemptyset(sigset_t *set) {
    cplus_libc_log("sigemptyset", "enter");
    return sigemptyset(set);
}
int cplus_sigfillset(sigset_t *set) {
    cplus_libc_log("sigfillset", "enter");
    return sigfillset(set);
}
int cplus_sigaddset(sigset_t *set, int signo) {
    cplus_libc_log("sigaddset", "enter");
    return sigaddset(set, signo);
}
int cplus_sigdelset(sigset_t *set, int signo) {
    cplus_libc_log("sigdelset", "enter");
    return sigdelset(set, signo);
}
int cplus_sigismember(const sigset_t *set, int signo) {
    cplus_libc_log("sigismember", "enter");
    return sigismember(set, signo);
}
int cplus_sigaction(int signo, const struct sigaction *act, struct sigaction *oldact) {
    cplus_libc_log("sigaction", "enter");
    return sigaction(signo, act, oldact);
}
int cplus_sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
    cplus_libc_log("sigprocmask", "enter");
    return sigprocmask(how, set, oldset);
}
int cplus_sigsuspend(const sigset_t *mask) {
    cplus_libc_log("sigsuspend", "enter");
    return sigsuspend(mask);
}
int cplus_kill(pid_t pid, int signo) {
    cplus_libc_log("kill", "enter");
    return kill(pid, signo);
}
int cplus_sigwait(const sigset_t *set, int *sig) {
    cplus_libc_log("sigwait", "enter");
    return sigwait(set, sig);
}
