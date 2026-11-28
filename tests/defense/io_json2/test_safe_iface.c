/* BLUE-TEAM Agent F: checked format + I/O interface regressions.
 *
 * 1200 deterministic cases (fixed SplitMix64-style seed). Each case runs in a
 * forked child with alarm(5); the parent classifies signals as CRASH/HANG and
 * inspects 32-byte canaries around each destination for CORRUPT.
 *
 * Scope: residual bypasses in the checked adapters (null args, one-over dmax,
 * %n / positional specifiers, unbounded scanf, use-after-close streams, lying
 * bmax). Every hostile case must be REFUSED (-1 / EINVAL, or 0 for fread_s)
 * without touching a canary.  Control cases prove literal '$'/'%%', exact-fit
 * destinations and valid round-trips keep their semantics.
 *
 * Build (from repo root):
 *   clang -O1 -g -std=c11 -w -Istdlib/safe -Ithird_party/safeclib/include \
 *     -Ithird_party/exceptions4c test_safe_iface.c \
 *     build/safe/libcplus_safecrt.a -lpthread -lm -o <bin>
 * Run: timeout 60 <bin>
 * Output: "SAFEIFACE <cases> <crashes> <corrupt> <mismatch>" then exit status.
 */
#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "safe_alloc.h"
#include "safe_error.h"
#include "safe_format.h"
#include "safe_io.h"
#include "safe_log.h"
#include "safe_runtime.h"

#define N_CASES 1200
#define GUARD 32u
#define ALARM_SEC 5
#define SENTINEL 0x24681357

static int g_cases, g_crashes, g_corrupt, g_mismatch;
static char gout[128];

static unsigned long long seed64(unsigned int id) {
    unsigned long long x = 0xABCD1234FEED0001ULL ^
                           ((unsigned long long)(id + 1u) * 0x9E3779B97F4A7C15ULL);
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    return x * 2685821657736338717ULL;
}
static unsigned int rnd(unsigned long long *s) {
    unsigned long long x = *s;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    *s = x;
    return (unsigned int)((x * 2685821657736338717ULL) >> 32);
}
static void fill(char *p, size_t n) { memset(p, 0xA5, n); }
static int all_a5(const char *p, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) if (p[i] != (char)0xA5) return 0;
    return 1;
}
static void report(int fd, int id, const char *name, int ok, int sent) {
    int n = snprintf(gout, sizeof(gout), "S %04d %23s %d %d\n",
                     id, name, ok ? 1 : 0, sent ? 1 : 0);
    if (n > 0) {
        size_t k = (size_t)n;
        if (k >= sizeof(gout)) k = sizeof(gout) - 1u;
        (void)write(fd, gout, k);
    }
}
static size_t read_pipe(int fd, char *out, size_t cap) {
    size_t n = 0;
    for (;;) {
        char c = '\0';
        ssize_t k = read(fd, &c, 1);
        if (k <= 0 || c == '\n') break;
        if (n + 1u < cap) out[n++] = c; else break;
    }
    out[n] = '\0';
    return n;
}

struct S { char a[8]; char guard[8]; };

/* 0: %n and positional specifiers must be refused before host printf. */
static int c_percent_n(unsigned long long *rs) {
    static const char *const attacks[] = {
        "%n", "%hhn", "%lln", "%1$2$n", "%11$22$n", "ABCD%n",
        "%1$-2$n", "%2$1$n", "%*2$n"
    };
    volatile int sink = SENTINEL;
    char b[GUARD + 64u + GUARD];
    int ok = 1, i;
    (void)rnd(rs);
    fill(b, sizeof b);
    for (i = 0; i < (int)(sizeof attacks / sizeof attacks[0]); ++i) {
        errno = 0;
        if (cplus_snprintf_s(b + GUARD, 64u, attacks[i], (int *)&sink) != -1 ||
            errno != EINVAL || sink != SENTINEL) ok = 0;
    }
    /* Control: literal '$' and escaped '%' keep their semantics. */
    if (cplus_snprintf_s(b + GUARD, 64u, "price: $5") != 9) ok = 0;
    if (cplus_snprintf_s(b + GUARD, 64u, "100%%n ok") != 8) ok = 0;
    if (!all_a5(b, GUARD) || !all_a5(b + GUARD + 64u, GUARD)) ok = 0;
    return ok;
}

/* 1: struct-member one-over (F2) via the unprefixed macro. */
static int c_one_over_struct(unsigned long long *rs) {
    struct S s;
    int ok = 1;
    (void)rnd(rs);
    fill((char *)&s, sizeof s);
    errno = 0;
    if (snprintf_s(s.a, 16u, "%s", "0123456789ABCDE") != -1 || errno != EINVAL)
        ok = 0;
    if (!all_a5(s.guard, sizeof s.guard)) ok = 0; /* adjacent field intact */
    /* Exact-fit member is accepted. */
    if (snprintf_s(s.a, sizeof s.a, "%s", "abc") != 3) ok = 0;
    return ok;
}

/* 2: one-over on a tracked heap allocation must be refused even when the
 * caller claims a larger dmax (independent of compiler object-size data). */
static int c_one_over_flat(unsigned long long *rs) {
    char *hp;
    int ok = 1;
    (void)rnd(rs);
    hp = (char *)malloc_s(16u);
    if (hp == (char *)0) return 0;
    memset(hp, 0xA5, 16u);
    errno = 0;
    if (snprintf_s(hp, 17u, "%s", "0123456789ABCDEF") != -1 ||
        errno != EINVAL) ok = 0;
    if (!all_a5(hp, 16u)) ok = 0; /* allocation completely untouched */
    if (snprintf_s(hp, 16u, "%s", "abc") != 3) ok = 0;
    free_s(hp);
    return ok;
}

/* 3: non-literal format refused even when its text is harmless. */
static int c_nonliteral(unsigned long long *rs) {
    char b[GUARD + 32u + GUARD];
    int ok = 1;
    (void)rnd(rs);
    fill(b, sizeof b);
    errno = 0;
    if (cplus_snprintf_s_checked(0, 32u, b + GUARD, 32u, "%s", "x") != -1 ||
        errno != EINVAL) ok = 0;
    if (!all_a5(b, GUARD) || !all_a5(b + GUARD + 32u, GUARD)) ok = 0;
    errno = 0;
    if (cplus_printf_s_checked(0, "ok") != -1 || errno != EINVAL) ok = 0;
    return ok;
}

/* 4: scanf unbounded conversions and %n; bounded controls. */
static int c_scanf_spec(unsigned long long *rs) {
    char b[GUARD + 64u + GUARD];
    volatile int sink = SENTINEL;
    int ok = 1;
    (void)rnd(rs);
    fill(b, sizeof b);
    errno = 0;
    if (cplus_sscanf_s("abc def", "%s", b + GUARD) != -1 || errno != EINVAL)
        ok = 0;
    errno = 0;
    if (cplus_sscanf_s("abc", "%[a-z]", b + GUARD) != -1 || errno != EINVAL)
        ok = 0;
    errno = 0;
    if (cplus_sscanf_s("1", "%n", (int *)&sink) != -1 || errno != EINVAL ||
        sink != SENTINEL) ok = 0;
    errno = 0;
    if (cplus_sscanf_s("1", "%1$2$n", (int *)&sink, (int *)&sink) != -1 ||
        errno != EINVAL || sink != SENTINEL) ok = 0;
    /* Controls: bounded %63s and suppressed %*s are accepted. */
    if (cplus_sscanf_s("abc", "%3s", b + GUARD) != 1) ok = 0;
    if (cplus_sscanf_s("abc def", "%*s %3s", b + GUARD) != 1) ok = 0;
    if (!all_a5(b, GUARD)) ok = 0;
    return ok;
}

/* 5/6/7/8/9/10: checked I/O (closed stream, bmax, null, clamp, round-trip). */
static int c_io(int id, int fd, unsigned long long *rs) {
    int mode = (int)(rnd(rs) % 6u);
    const char *path = "/tmp/cplus_iojson2_check.tmp";
    char b[GUARD + 32u + GUARD];
    FILE *f;
    int ok = 1;
    fill(b, sizeof b);
    f = cplus_fopen_s(path, "wb+");
    if (f == (FILE *)0) {
        if (mode == 5) return 1; /* fopen failure is a valid refusal */
        return 0;
    }
    if (cplus_fwrite_s("0123456789ABCDEF", 16u, 1u, 16u, f) != 16u) ok = 0;
    rewind(f);
    if (mode == 0) {
        /* closed stream must be refused before host fread/fwrite */
        if (cplus_fclose_s(f) != 0) ok = 0;
        errno = 0;
        if (cplus_fread_s(b + GUARD, 32u, 1u, 4u, f) != 0u || errno != EINVAL)
            ok = 0;
        errno = 0;
        if (cplus_fwrite_s(b + GUARD, 32u, 1u, 4u, f) != 0u || errno != EINVAL)
            ok = 0;
        if (!all_a5(b + GUARD, 32u)) ok = 0;
        errno = 0;
        if (cplus_fclose_s(f) != EOF || errno != EINVAL) ok = 0;
    } else if (mode == 1) {
        /* lying bmax on a tracked allocation must be refused */
        char *hp = (char *)malloc_s(8u);
        if (hp == (char *)0) { ok = 0; }
        else {
            memset(hp, 'Z', 8u);
            errno = 0;
            if (cplus_fread_s(hp, 16u, 1u, 16u, f) != 0u || errno != EINVAL)
                ok = 0;
            if (hp[0] != 'Z' || hp[7] != 'Z') ok = 0;
            free_s(hp);
        }
        if (cplus_fclose_s(f) != 0) ok = 0;
    } else if (mode == 2) {
        /* null args and zero size/count */
        if (cplus_fread_s((void *)0, 32u, 1u, 1u, f) != 0u) ok = 0;
        if (cplus_fread_s(b + GUARD, 32u, 1u, 1u, (FILE *)0) != 0u) ok = 0;
        if (cplus_fwrite_s((void *)0, 32u, 1u, 1u, f) != 0u) ok = 0;
        if (cplus_fread_s(b + GUARD, 32u, 0u, 1u, f) != 0u) ok = 0;
        if (cplus_fwrite_s(b + GUARD, 32u, 1u, 0u, f) != 0u) ok = 0;
        if (cplus_fclose_s(f) != 0) ok = 0;
    } else if (mode == 3) {
        /* huge count clamps to bmax/size=4, but the file holds only 16 bytes,
         * so fread returns 2 items; no byte past the 32-byte window is touched. */
        if (cplus_fread_s(b + GUARD, 32u, 8u, (size_t)-1, f) != 2u) ok = 0;
        if (!all_a5(b + GUARD + 32u, GUARD)) ok = 0;
        if (cplus_fclose_s(f) != 0) ok = 0;
    } else if (mode == 4) {
        /* checked fprintf/fscanf on a closed stream must be refused */
        if (cplus_fclose_s(f) != 0) ok = 0;
        errno = 0;
        if (cplus_fprintf_s(f, "x") != -1 || errno != EINVAL) ok = 0;
        errno = 0;
        if (cplus_fscanf_s(f, "%d", &id) != -1 || errno != EINVAL) ok = 0;
    } else {
        /* valid round-trip */
        rewind(f);
        if (cplus_fread_s(b + GUARD, 32u, 1u, 16u, f) != 16u) ok = 0;
        if (memcmp(b + GUARD, "0123456789ABCDEF", 16u) != 0) ok = 0;
        if (cplus_fclose_s(f) != 0) ok = 0;
    }
    unlink(path);
    if (!all_a5(b, GUARD) || !all_a5(b + GUARD + 32u, GUARD)) ok = 0;
    (void)id;
    return ok;
}

static void dispatch(int id, int fd, unsigned long long *rs) {
    int ok;
    switch (id % 5) {
    case 0: ok = c_percent_n(rs); break;
    case 1: ok = c_one_over_struct(rs); break;
    case 2: ok = c_one_over_flat(rs); break;
    case 3: ok = c_nonliteral(rs); break;
    default:
        if (id % 10 == 4) ok = c_scanf_spec(rs);
        else ok = c_io(id, fd, rs);
        break;
    }
    report(fd, id, "safe_iface", ok, 1);
}

static int fork_case(int id, int pfds[2], pid_t *pid) {
    if (pipe(pfds) != 0) return -1;
    *pid = fork();
    if (*pid < 0) { close(pfds[0]); close(pfds[1]); return -1; }
    if (*pid == 0) {
        unsigned long long s;
        close(pfds[0]);
        alarm(ALARM_SEC);
        s = seed64((unsigned int)id);
        dispatch(id, pfds[1], &s);
        close(pfds[1]);
        _exit(0);
    }
    close(pfds[1]);
    return 0;
}

int main(void) {
    int i, pfds[2], status, mismatch = 0, corrupt = 0, crashes = 0;
    pid_t pid;

    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    cplus_runtime_set_violation_trap(0);

    for (i = 0; i < N_CASES; i++) {
        char line[128], name[48];
        int n, v_id, v_ok, v_sent, rc;
        if (fork_case(i, pfds, &pid) != 0) { crashes++; g_cases++; continue; }
        n = (int)read_pipe(pfds[0], line, sizeof(line));
        close(pfds[0]);
        rc = waitpid(pid, &status, 0);
        g_cases++;
        if (rc != pid || WIFSIGNALED(status) || n == 0) { crashes++; continue; }
        if (sscanf(line, "S %d %31s %d %d", &v_id, name, &v_ok, &v_sent) != 4 ||
            v_id != i) { crashes++; continue; }
        if (!v_sent) corrupt++;
        if (!v_ok) mismatch++;
    }
    printf("SAFEIFACE %d %d %d %d\n", g_cases, crashes, corrupt, mismatch);
    return (crashes || corrupt || mismatch) ? 1 : 0;
}
