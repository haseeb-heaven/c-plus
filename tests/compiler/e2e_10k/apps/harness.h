/* Fork/capture harness for the e2e_10k/apps end-to-end suite.
 *
 * Every scenario is a void(void) function.  The parent forks one child per
 * scenario, captures stdout (fd 1), stderr (fd 2) and a completion token
 * (fd 3), then requires an EXACT byte-for-byte match against the expected
 * stdout, expected stderr and expected exit code baked from the reference
 * compiler.  Trap scenarios therefore cannot kill the runner, and their
 * diagnostics are asserted along with their exit status.
 *
 * Harness output uses write(2) only, so safe-C stdio remapping macros cannot
 * perturb the report.  A SIGALRM watchdog in the child bounds runaway code.
 */
#ifndef E2E10K_APPS_HARNESS_H
#define E2E10K_APPS_HARNESS_H
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

typedef struct {
    const char *name;       /* "<category>|<api>|<edge>" */
    void (*fn)(void);
    const char *exp_out;    /* exact stdout bytes */
    const char *exp_err;    /* exact stderr bytes */
    int exp_exit;           /* expected child exit code */
} e2e_scenario;

static void e2e_wstr(const char *s) { (void)write(1, s, strlen(s)); }

static void e2e_num(long long v) {
    char buf[80], rev[48];
    int i = 0, j = 0;
    unsigned long long u;
    if (v < 0) { e2e_wstr("-"); u = (unsigned long long)(-(v + 1)) + 1ULL; }
    else u = (unsigned long long)v;
    do { rev[j++] = (char)('0' + (int)(u % 10ULL)); u /= 10ULL; } while (u);
    while (j > 0) buf[i++] = rev[--j];
    buf[i] = 0;
    e2e_wstr(buf);
}
static void e2e_emit_ll(const char *tag, long long v) {
    e2e_wstr(tag); e2e_wstr("="); e2e_num(v); e2e_wstr("\n");
}
static void e2e_emit_str(const char *tag, const char *s) {
    e2e_wstr(tag); e2e_wstr("="); e2e_wstr(s ? s : "(null)"); e2e_wstr("\n");
}
/* Emission helpers used by every generated scenario. */
#define E(tag, val)  e2e_emit_ll(#tag, (long long)(val))
#define ES(tag, str) e2e_emit_str(#tag, (str))

static void e2e_hex(const char *s, size_t n) {
    static const char h[] = "0123456789abcdef";
    char o[2];
    size_t i;
    for (i = 0; i < n; ++i) {
        o[0] = h[((unsigned char)s[i]) >> 4];
        o[1] = h[((unsigned char)s[i]) & 15];
        (void)write(1, o, 2);
    }
}

static int e2e_drain(int fd, char *buf, size_t cap) {
    size_t len = 0;
    ssize_t k;
    while (len < cap - 1 && (k = read(fd, buf + len, cap - 1 - len)) > 0)
        len += (size_t)k;
    buf[len] = 0;
    return (int)len;
}

/* E2E_TABLE_SIZE is emitted by generate.py before this header is included.
 * A statically sized array parameter keeps the runner acceptable to c+
 * --strict (which rejects pointer parameters of unverifiable extent). */
#ifndef E2E_TABLE_SIZE
#define E2E_TABLE_SIZE 1
#endif
static int e2e_run(e2e_scenario tab[static E2E_TABLE_SIZE]) {
    static const unsigned char MAGIC[8] = {'E','2','E','D','O','N','E','!'};
    int pass = 0, fail = 0, i;
    const int e2e_n = E2E_TABLE_SIZE;
    for (i = 0; i < e2e_n; ++i) {
        int outp[2], errp[2], resp[2];
        pid_t pid;
        char ob[16384], eb[16384];
        int ol, el, status, ex, normal, ok;
        unsigned char rb[16];
        ssize_t rn;
        if (pipe(outp) || pipe(errp) || pipe(resp)) {
            e2e_wstr("SCENARIO "); e2e_wstr(tab[i].name); e2e_wstr(" FAIL pipe\n");
            ++fail;
            continue;
        }
        pid = fork();
        if (pid < 0) {
            e2e_wstr("SCENARIO "); e2e_wstr(tab[i].name); e2e_wstr(" FAIL fork\n");
            ++fail;
            continue;
        }
        if (pid == 0) {
            close(outp[0]); close(errp[0]); close(resp[0]);
            (void)dup2(outp[1], 1);
            (void)dup2(errp[1], 2);
            (void)dup2(resp[1], 3);
            close(outp[1]); close(errp[1]); close(resp[1]);
            (void)signal(SIGALRM, SIG_DFL);
            (void)alarm(10);                 /* hard per-scenario watchdog */
            tab[i].fn();
            (void)fflush(NULL);              /* flush stdio before _exit */
            (void)write(3, MAGIC, sizeof(MAGIC));
            _exit(0);
        }
        close(outp[1]); close(errp[1]); close(resp[1]);
        ol = e2e_drain(outp[0], ob, sizeof(ob));
        el = e2e_drain(errp[0], eb, sizeof(eb));
        rn = read(resp[0], rb, sizeof(rb));
        (void)waitpid(pid, &status, 0);
        close(outp[0]); close(errp[0]); close(resp[0]);
        ex = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        normal = (rn == (ssize_t)sizeof(MAGIC) &&
                  memcmp(rb, MAGIC, sizeof(MAGIC)) == 0);
        ok = (normal == (tab[i].exp_exit == 0)) &&
             ex == tab[i].exp_exit &&
             ol == (int)strlen(tab[i].exp_out) &&
             memcmp(ob, tab[i].exp_out, (size_t)ol) == 0 &&
             el == (int)strlen(tab[i].exp_err) &&
             memcmp(eb, tab[i].exp_err, (size_t)el) == 0;
        if (ok) ++pass; else ++fail;
        e2e_wstr("SCENARIO "); e2e_wstr(tab[i].name); e2e_wstr(" ");
        e2e_wstr(ok ? "PASS" : "FAIL");
        e2e_wstr(" out="); e2e_hex(ob, (size_t)ol);
        e2e_wstr(" err="); e2e_hex(eb, (size_t)el);
        e2e_wstr(" exit="); e2e_num(ex);
        e2e_wstr(" expo="); e2e_hex(tab[i].exp_out, strlen(tab[i].exp_out));
        e2e_wstr(" experr="); e2e_hex(tab[i].exp_err, strlen(tab[i].exp_err));
        e2e_wstr(" expe="); e2e_num(tab[i].exp_exit);
        e2e_wstr("\n");
    }
    e2e_wstr("SUMMARY total="); e2e_num(e2e_n); e2e_wstr("\n");
    e2e_wstr("SUMMARY pass="); e2e_num(pass); e2e_wstr("\n");
    e2e_wstr("SUMMARY fail="); e2e_num(fail); e2e_wstr("\n");
    return fail == 0 ? 0 : 1;
}
#endif /* E2E10K_APPS_HARNESS_H */
