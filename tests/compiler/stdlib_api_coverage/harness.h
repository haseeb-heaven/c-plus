/* Fork/capture harness for the generated safe-C API coverage suite.
 *
 * Each scenario is a void(void) function that calls exactly ONE exported API
 * (plus setup/teardown and the required assertion-specific queries), and emits
 * its observed result with the E()/ES() helpers below.  The harness runs every
 * scenario in a forked child so an intentional trap (exit 70) cannot kill the
 * runner, captures the child's exact stdout, and compares it with the exact
 * expected stdout and exit code recorded in the scenario table.
 *
 * All harness output uses write(2), never stdio, so stdio-remapping macros in
 * safe_c.h cannot perturb the harness itself.
 */
#ifndef COV_HARNESS_H
#define COV_HARNESS_H
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

typedef struct {
    const char *name;      /* "<api>|<edge>" */
    void (*fn)(void);
    const char *exp_out;   /* exact stdout bytes */
    int exp_exit;          /* expected child exit code */
} cov_scenario;

static void wstr(const char *s) { (void)write(1, s, strlen(s)); }

static void cov_emit_ll(const char *tag, long long v) {
    char buf[80];
    char rev[48];
    int i = 0, j = 0;
    unsigned long long u;
    wstr(tag);
    wstr("=");
    if (v < 0) { wstr("-"); u = (unsigned long long)(-(v + 1)) + 1ULL; }
    else u = (unsigned long long)v;
    do { rev[j++] = (char)('0' + (int)(u % 10ULL)); u /= 10ULL; } while (u);
    while (j > 0) buf[i++] = rev[--j];
    buf[i] = 0;
    wstr(buf);
    wstr("\n");
}
static void cov_emit_str(const char *tag, const char *s) {
    wstr(tag);
    wstr("=");
    wstr(s ? s : "(null)");
    wstr("\n");
}
#define E(tag, val)  cov_emit_ll(#tag, (long long)(val))
#define ES(tag, str) cov_emit_str(#tag, (str))

static void cov_hex(const char *s, size_t n) {
    static const char h[] = "0123456789abcdef";
    char o[2];
    size_t i;
    for (i = 0; i < n; ++i) {
        o[0] = h[((unsigned char)s[i]) >> 4];
        o[1] = h[((unsigned char)s[i]) & 15];
        (void)write(1, o, 2);
    }
}
static void cov_num(long long v) {
    char buf[80];
    char rev[48];
    int i = 0, j = 0;
    unsigned long long u;
    if (v < 0) { wstr("-"); u = (unsigned long long)(-(v + 1)) + 1ULL; }
    else u = (unsigned long long)v;
    do { rev[j++] = (char)('0' + (int)(u % 10ULL)); u /= 10ULL; } while (u);
    while (j > 0) buf[i++] = rev[--j];
    buf[i] = 0;
    wstr(buf);
}

static int cov_run(const cov_scenario *tab, int n) {
    static const unsigned char MAGIC[8] = {'C','O','V','D','O','N','E','!'};
    int pass = 0, fail = 0, i;
    for (i = 0; i < n; ++i) {
        int outp[2], resp[2];
        pid_t pid;
        char buf[16384];
        size_t len = 0;
        unsigned char rb[16];
        ssize_t k, rn;
        int status, ex;
        int normal;
        int ok;
        if (pipe(outp) != 0 || pipe(resp) != 0) {
            wstr("SCENARIO "); wstr(tab[i].name); wstr(" FAIL pipe\n");
            ++fail;
            continue;
        }
        pid = fork();
        if (pid < 0) {
            wstr("SCENARIO "); wstr(tab[i].name); wstr(" FAIL fork\n");
            ++fail;
            continue;
        }
        if (pid == 0) {
            int dn;
            close(outp[0]); close(resp[0]);
            (void)dup2(outp[1], 1);
            (void)dup2(resp[1], 3);
            dn = open("/dev/null", O_WRONLY);   /* library diagnostics go here */
            if (dn >= 0) (void)dup2(dn, 2);
            close(outp[1]); close(resp[1]);
            if (dn >= 0) close(dn);
            tab[i].fn();
            (void)fflush(stdout);
            (void)write(3, MAGIC, sizeof(MAGIC));
            _exit(0);
        }
        close(outp[1]); close(resp[1]);
        while (len < sizeof(buf) - 1 &&
               (k = read(outp[0], buf + len, sizeof(buf) - 1 - len)) > 0)
            len += (size_t)k;
        buf[len] = 0;
        rn = read(resp[0], rb, sizeof(rb));
        (void)waitpid(pid, &status, 0);
        close(outp[0]); close(resp[0]);
        ex = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        normal = (rn == (ssize_t)sizeof(MAGIC) &&
                  memcmp(rb, MAGIC, sizeof(MAGIC)) == 0);
        ok = (normal == (tab[i].exp_exit == 0)) &&
             ex == tab[i].exp_exit &&
             len == strlen(tab[i].exp_out) &&
             memcmp(buf, tab[i].exp_out, len) == 0;
        if (ok) ++pass; else ++fail;
        wstr("SCENARIO "); wstr(tab[i].name); wstr(" ");
        wstr(ok ? "PASS" : "FAIL");
        wstr(" out="); cov_hex(buf, len);
        wstr(" exit="); cov_num(ex);
        wstr(" expo="); cov_hex(tab[i].exp_out, strlen(tab[i].exp_out));
        wstr(" expe="); cov_num(tab[i].exp_exit);
        wstr("\n");
    }
    wstr("SUMMARY total="); cov_num(n); wstr("\n");
    wstr("SUMMARY pass="); cov_num(pass); wstr("\n");
    wstr("SUMMARY fail="); cov_num(fail); wstr("\n");
    return fail == 0 ? 0 : 1;
}
#endif /* COV_HARNESS_H */
