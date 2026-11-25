/* Consolidated regression suite for the HIGH findings of
 * docs/libc/WRAPPER_SAFETY_AUDIT.md that are exercised through the existing
 * hardened (libc) and checked (SafeCRT) APIs.
 *
 * Coverage (see docs/libc/AUDIT_HIGH_REGRESSION.md for the contract sources):
 *   * double-fclose refusal with colliding FILE* addresses -- both the
 *     checked cplus_fclose_s layer (tombstoned probe chains) and the plain
 *     cplus_fclose layer;
 *   * detached-thread start-context lifetime (F1): the worker's func/arg are
 *     read from a heap cell, not from the creating frame's cplus_thrd_t;
 *   * uchar surrogate state (F2): a real mbstate_t is handed to the host and
 *     the musl pending-surrogate word lives in separate storage (canaries);
 *   * huge-n guards for strncpy / strncpy_bounded / memchr / memrchr /
 *     stpncpy, including NULL guards;
 *   * the checked wide-format sibling cplus_swprintf_checked rejects %n
 *     variants without touching va_list or the caller's sink;
 *   * signal/fenv required-pointer NULL guards;
 *   * cplus_libc_log preserves errno even when its write(2) fails.
 *
 * Only public/hardened entry points are called; no raw UB is used to "prove"
 * a bug. Every assertion goes through CHECK so the suite prints one summary
 * line:  audit_high: <N>/<N> checks ok
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fenv.h>
#include <locale.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#include "cplus_libc.h"
#include "cplus_threads.h"
#include "cplus_uchar.h"
#include "safe_io.h"

/* Internal diagnostics (deliberately not part of the installed <uchar.h>). */
extern int cplus_uchar_internal_canary_ok(void);
extern void cplus_libc_log(const char *api, const char *msg);

static int g_checks;
static int g_total;

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++g_total;                                                             \
        if (cond) {                                                            \
            ++g_checks;                                                        \
        } else {                                                               \
            printf("FINDING: %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                      \
    } while (0)

/* --------------------------------------------------------------- stdio --- */

/* Mirrors hash_stream()/closed_hash(): 512 open-addressed slots. */
static size_t stream_bucket(const FILE *f) {
    uintptr_t x = (uintptr_t)f;
    x ^= x >> 33;
    x *= (uintptr_t)0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    return (size_t)(x & 511u);
}

/* Force two FILE* into the same probe chain, close them through the wrapper,
 * reopen one of those exact addresses (clearing only that marker), then
 * require that a second close of the OTHER address is still refused.  A
 * deletion-by-NULL cache would break the chain and reach host fclose() on an
 * already freed FILE*; tombstones keep the surviving marker reachable.
 *
 * Which of a/b the allocator recycles first is host-dependent, so we pick the
 * "other" stream from whichever address actually came back. */
static void check_collision_double_close(FILE *(*openf)(const char *, const char *),
                                         int (*closef)(FILE *), int want_errno) {
    enum { SEARCH = 1200, TRIES = 256 };
    FILE *raw[SEARCH];
    FILE *a = NULL, *b = NULL, *reopened = NULL, *other;
    int n = 0, i, j;

    for (i = 0; i < SEARCH; ++i) {
        raw[i] = fopen("/dev/null", "r");
        if (!raw[i]) break;
        ++n;
    }
    CHECK(n == SEARCH); /* all probe streams opened */
    for (i = 0; i < n && !b; ++i) {
        for (j = i + 1; j < n; ++j) {
            if (raw[i] != raw[j] &&
                stream_bucket(raw[i]) == stream_bucket(raw[j])) {
                a = raw[i];
                b = raw[j];
                break;
            }
        }
    }
    CHECK(a != NULL && b != NULL);

    if (a && b) {
        /* Leave the other probes OPEN.  Releasing them first makes the
         * allocator hand back an unrelated recycled FILE*, so a/b would no
         * longer be at the top of the free list when we reopen below.  They
         * are raw fopen()s, so they never occupy a checked cache slot. */
        /* A takes the home slot; B is inserted just past it. */
        CHECK(closef(a) == 0);
        CHECK(closef(b) == 0);

        for (i = 0; i < TRIES && !reopened; ++i) {
            FILE *f = openf("/dev/null", "r");
            CHECK(f != NULL);
            if (!f) break;
            if (f == a || f == b) reopened = f;
            else CHECK(fclose(f) == 0); /* host close: adds no wrapper marker */
        }
        CHECK(reopened != NULL);
        if (reopened) {
            other = (reopened == a) ? b : a;
            errno = 0;
            CHECK(closef(other) == EOF && errno == want_errno);
            CHECK(closef(reopened) == 0); /* now live again: clean close */
        }
    }

    /* Cleanup (not part of the assertions): a and b were closed by closef. */
    for (i = 0; i < n; ++i)
        if (raw[i] != a && raw[i] != b) (void)fclose(raw[i]);
}

static void check_basic_double_close(void) {
    FILE *f = cplus_fopen_s("/tmp/cplus_audit_high_s.tmp", "wb");
    CHECK(f != NULL);
    if (f) {
        CHECK(cplus_fclose_s(f) == 0);
        errno = 0;
        CHECK(cplus_fclose_s(f) == EOF && errno == EINVAL);
    }
    unlink("/tmp/cplus_audit_high_s.tmp");

    f = cplus_fopen("/tmp/cplus_audit_high_plain.tmp", "wb");
    CHECK(f != NULL);
    if (f) {
        CHECK(cplus_fclose(f) == 0);
        errno = 0;
        CHECK(cplus_fclose(f) == EOF && errno == EBADF);
    }
    unlink("/tmp/cplus_audit_high_plain.tmp");
}

/* ------------------------------------------------------- detached thread -- */

#define F1_MAGIC 0xC0FFEEu
typedef struct { unsigned magic; int idx; } f1_payload;
static f1_payload g_f1_payload = { F1_MAGIC, 7 };
static volatile int g_f1_done;
static volatile int g_f1_saw;

static int f1_worker(void *arg) {
    struct timespec nap;
    f1_payload *p = (f1_payload *)arg;
    nap.tv_sec = 0;
    nap.tv_nsec = 60L * 1000L * 1000L; /* outlive the creating stack frame */
    (void)cplus_thrd_sleep(&nap, NULL);
    if (p == &g_f1_payload && p->magic == F1_MAGIC && p->idx == 7) g_f1_saw = 1;
    g_f1_done = 1;
    return 0;
}

/* The public cplus_thrd_t lives in THIS frame and is poisoned on return. */
static void f1_spawn_and_detach(void) {
    cplus_thrd_t t;
    volatile unsigned char poison[1024];
    size_t k;
    g_f1_done = 0;
    g_f1_saw = 0;
    if (cplus_thrd_create(&t, f1_worker, &g_f1_payload) != CPLUS_THRD_SUCCESS) {
        g_f1_done = -1;
        return;
    }
    if (cplus_thrd_detach(t) != CPLUS_THRD_SUCCESS) {
        g_f1_done = -1;
        return;
    }
    for (k = 0; k < sizeof poison; ++k) poison[k] = (unsigned char)0xA5u;
    (void)poison[sizeof poison - 1u]; /* keep the writes observable */
}

static void check_f1(void) {
    struct timespec nap;
    int i;
    f1_spawn_and_detach();
    nap.tv_sec = 0;
    nap.tv_nsec = 10L * 1000L * 1000L;
    for (i = 0; i < 400 && !g_f1_done; ++i) (void)nanosleep(&nap, NULL);
    CHECK(g_f1_done == 1);
    CHECK(g_f1_saw == 1);
}

/* ------------------------------------------------------------ uchar / F2 -- */

static void check_f2(void) {
    typedef char state_size_ok[(sizeof(mbstate_t) >= sizeof(unsigned)) ? 1 : -1];
    state_size_ok size_guard;
    char16_t c16 = 0;
    char32_t c32 = 0;
    const char *loc;
    int utf8;

    (void)size_guard;
    CHECK(sizeof(mbstate_t) >= sizeof(unsigned));
    CHECK(cplus_uchar_internal_canary_ok() == 1);

    /* ps == NULL selects the file-static real mbstate_t. */
    CHECK(cplus_mbrtoc16(&c16, "A", 1, NULL) == 1 && c16 == (char16_t)'A');
    CHECK(cplus_mbrtoc32(&c32, "Z", 1, NULL) == 1 && c32 == (char32_t)'Z');
    CHECK(cplus_uchar_internal_canary_ok() == 1);

    loc = setlocale(LC_CTYPE, "");
    utf8 = (loc != NULL && MB_CUR_MAX >= 4);
    if (utf8) {
        char out[16];
        char16_t hi = 0, lo = 0;
        size_t r, k;
        int canary_ok;
        struct {
            unsigned char head[16];
            mbstate_t st;
            unsigned char tail[16];
        } pad;
        unsigned char guard[16];

        /* Encode then decode a surrogate pair through the NULL internal state. */
        memset(out, 0, sizeof out);
        CHECK(cplus_c16rtomb(out, (char16_t)0xD83D, NULL) == 0);
        CHECK(cplus_c16rtomb(out, (char16_t)0xDE00, NULL) == 4);
        CHECK((unsigned char)out[0] == 0xF0u && (unsigned char)out[3] == 0x80u);
        CHECK(cplus_mbrtoc16(&hi, out, 4, NULL) == 4);
        CHECK(hi == (char16_t)0xD83D);
        CHECK(cplus_mbrtoc16(&lo, "", 1, NULL) == (size_t)-3);
        CHECK(lo == (char16_t)0xDE00);
        CHECK(cplus_uchar_internal_canary_ok() == 1);

        /* Caller mbstate_t guarded by canaries; split UTF-8 forces the host
         * to write real conversion state into it. */
        memset(guard, 0xA5u, sizeof guard);
        memset(&pad, 0x5Au, sizeof pad);
        memcpy(pad.head, guard, sizeof guard);
        memcpy(pad.tail, guard, sizeof guard);
        memset(&pad.st, 0, sizeof pad.st);
        CHECK(cplus_mbrtoc16(&c16, "\xF0\x9F", 2, &pad.st) == (size_t)-2);
        r = cplus_mbrtoc16(&c16, "\x98\x80", 2, &pad.st);
        CHECK(r > 0u && r <= 4u);
        CHECK(c16 == (char16_t)0xD83D);
        CHECK(cplus_mbrtoc16(&c16, "", 1, &pad.st) == (size_t)-3);
        CHECK(c16 == (char16_t)0xDE00);
        canary_ok = 1;
        for (k = 0; k < sizeof guard; ++k)
            if (pad.head[k] != guard[k] || pad.tail[k] != guard[k]) canary_ok = 0;
        CHECK(canary_ok);

        /* Same split sequence through the internal NULL state: this is the
         * path that previously handed a 4-byte object to a 128-byte host state. */
        CHECK(cplus_mbrtoc16(&c16, "\xF0\x9F", 2, NULL) == (size_t)-2);
        CHECK(cplus_uchar_internal_canary_ok() == 1);
        r = cplus_mbrtoc16(&c16, "\x98\x80", 2, NULL);
        CHECK(r > 0u && r <= 4u);
        CHECK(c16 == (char16_t)0xD83D);
        CHECK(cplus_mbrtoc16(&c16, "", 1, NULL) == (size_t)-3);
        CHECK(c16 == (char16_t)0xDE00);
        CHECK(cplus_uchar_internal_canary_ok() == 1);
    } else {
        printf("note: audit_high UTF-8 locale unavailable; surrogate checks skipped\n");
    }
}

/* ------------------------------------------------------- huge-n / NULL ---- */

static void check_huge_n(void) {
    const size_t huge = (size_t)-1; /* SIZE_MAX > RSIZE_MAX_MEM (SIZE_MAX >> 1) */
    char d[16];
    size_t i;
    int all;

    /* strncpy raw: refuse before the n-byte zero-fill loop. */
    memset(d, 'A', sizeof d);
    errno = 0;
    CHECK(cplus_strncpy(d, "abc", huge) == d);
    CHECK(errno == EOVERFLOW);
    for (all = 1, i = 0; i < sizeof d; ++i) if (d[i] != 'A') all = 0;
    CHECK(all);

    /* Valid small n keeps the documented zero-fill semantics. */
    memset(d, 'A', sizeof d);
    CHECK(cplus_strncpy(d, "ab", 4) == d);
    CHECK(d[0] == 'a' && d[1] == 'b' && d[2] == '\0' && d[3] == '\0' &&
          d[4] == 'A');

    /* strncpy_bounded with a known destination capacity: refuse, no writes. */
    memset(d, 'A', sizeof d);
    errno = 0;
    CHECK(cplus_strncpy_bounded(d, "abc", huge, sizeof d) == NULL);
    CHECK(errno == ERANGE);
    for (all = 1, i = 0; i < sizeof d; ++i) if (d[i] != 'A') all = 0;
    CHECK(all);

    /* memrchr / memchr: NULL and huge-n guards plus small-n controls. */
    memset(d, 'x', sizeof d);
    errno = 0;
    CHECK(cplus_memchr(d, 'x', huge) == NULL);
    CHECK(errno == EOVERFLOW);
    errno = 0;
    CHECK(cplus_memrchr(d, 'x', huge) == NULL);
    CHECK(errno == EOVERFLOW);
    errno = 0;
    CHECK(cplus_memchr(NULL, 'x', 4) == NULL);
    CHECK(errno == EINVAL);
    errno = 0;
    CHECK(cplus_memrchr(NULL, 'x', 4) == NULL);
    CHECK(errno == EINVAL);
    CHECK(cplus_memchr(d, 'x', sizeof d) == d);
    CHECK(cplus_memrchr(d, 'x', sizeof d) == d + sizeof d - 1);

    /* stpncpy: huge tail memset and NULL are refused before any write. */
    memset(d, 'A', sizeof d);
    errno = 0;
    CHECK(cplus_stpncpy(d, "abc", huge) == NULL);
    CHECK(errno == EOVERFLOW);
    for (all = 1, i = 0; i < sizeof d; ++i) if (d[i] != 'A') all = 0;
    CHECK(all);
    errno = 0;
    CHECK(cplus_stpncpy(NULL, "abc", 4) == NULL);
    CHECK(errno == EINVAL);
    CHECK(cplus_stpncpy(d, "abc", sizeof d) == d + 3);
    CHECK(d[3] == '\0' && d[15] == '\0');
}

/* ------------------------------------------------------- wide format %n --- */

static void check_wide_n(void) {
    wchar_t out[32];
    int sentinel = 12345;
    int r;

    errno = 0;
    CHECK(cplus_swprintf_checked(out, 32, L"%n", &sentinel) == -1);
    CHECK(errno == EINVAL);
    CHECK(sentinel == 12345);

    errno = 0;
    CHECK(cplus_swprintf_checked(out, 32, L"%1$n", &sentinel) == -1);
    CHECK(errno == EINVAL);
    CHECK(sentinel == 12345);

    errno = 0;
    CHECK(cplus_swprintf_checked(out, 32, L"val=%lln", &sentinel) == -1);
    CHECK(errno == EINVAL);
    CHECK(sentinel == 12345);

    /* "%%n" is a literal, not a %n conversion. */
    errno = 0;
    r = cplus_swprintf_checked(out, 32, L"%%n");
    CHECK(r == 2);
    CHECK(wcscmp(out, L"%n") == 0);

    errno = 0;
    CHECK(cplus_swprintf_checked(NULL, 32, L"x") == -1 && errno == EINVAL);
    errno = 0;
    CHECK(cplus_swprintf_checked(out, 32, NULL) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(cplus_swprintf(out, 32, NULL) == -1 && errno == EINVAL);
}

/* --------------------------------------------------- signal / fenv NULLs -- */

static void check_signal_null(void) {
    sigset_t set;
    int sig = 0;

    errno = 0;
    CHECK(cplus_sigemptyset(NULL) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(cplus_sigfillset(NULL) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(cplus_sigaddset(NULL, SIGUSR1) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(cplus_sigdelset(NULL, SIGUSR1) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(cplus_sigismember(NULL, SIGUSR1) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(cplus_sigsuspend(NULL) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(cplus_sigwait(NULL, &sig) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(cplus_sigwait(&set, NULL) == -1 && errno == EINVAL);

    /* Positive control: valid sets still work. */
    CHECK(cplus_sigemptyset(&set) == 0);
    CHECK(cplus_sigaddset(&set, SIGUSR1) == 0);
    CHECK(cplus_sigismember(&set, SIGUSR1) == 1);
}

static void check_fenv_null(void) {
    fenv_t env;

    errno = 0;
    CHECK(cplus_fegetenv(NULL) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(cplus_fesetenv(NULL) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(cplus_fegetexceptflag(NULL, FE_ALL_EXCEPT) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(cplus_fesetexceptflag(NULL, FE_ALL_EXCEPT) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(cplus_feholdexcept(NULL) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(cplus_feupdateenv(NULL) == -1 && errno == EINVAL);

    /* Positive control: save/restore a real environment. */
    CHECK(cplus_fegetenv(&env) == 0);
    CHECK(cplus_fesetenv(&env) == 0);
}

/* ------------------------------------------------------ logger errno ------ */

static void check_log_errno(void) {
    int saved = dup(STDERR_FILENO);
    CHECK(saved >= 0);
    if (saved < 0) return;

    /* Force write(2) to fail with EBADF so the logger's save/restore of errno
     * is observable rather than vacuous. */
    CHECK(close(STDERR_FILENO) == 0);
    cplus_libc_log_set_enabled(1);
    errno = 1234;
    cplus_libc_log("audit-high", "errno-preservation");
    CHECK(errno == 1234);
    cplus_libc_log_set_enabled(0);
    CHECK(dup2(saved, STDERR_FILENO) == STDERR_FILENO);
    CHECK(close(saved) == 0);
}

int main(void) {
    cplus_libc_log_set_enabled(0); /* keep the summary line readable */

    /* Collision tests run first, before any wrapper close leaves a marker at
     * a recyclable FILE* address. */
    check_collision_double_close(cplus_fopen_s, cplus_fclose_s, EINVAL);
    check_collision_double_close(cplus_fopen, cplus_fclose, EBADF);
    check_basic_double_close();

    check_f1();
    check_f2();
    check_huge_n();
    check_wide_n();
    check_signal_null();
    check_fenv_null();
    check_log_errno();

    printf("audit_high: %d/%d checks ok\n", g_checks, g_total);
    return (g_checks == g_total) ? 0 : 1;
}
