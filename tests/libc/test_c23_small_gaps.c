/*
 * test_c23_small_gaps.c -- standalone C99 test for the small C23 gaps in
 * libc/src/c23_small_gaps.c (memset_explicit, timespec_getres, FE_TIESAWAY).
 *
 * The capability detector below MUST match the one in the implementation.
 * When a host exposes an API, normal/boundary/NULL cases and logging parity
 * (identical results with logging off vs on, plus the entry marker in the
 * captured stderr) are checked. When it does not, the case is SKIPped with
 * the exact probe blocker printed -- nothing is faked. The logger plumbing
 * itself is always exercised so "logging parity" is verified even on a host
 * where all three C23 entries are unavailable.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <assert.h>
#include <errno.h>
#include <fenv.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <unistd.h>
#endif

/* ---- capability detection (mirrors libc/src/c23_small_gaps.c) ---------- */
#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#  if __GLIBC_PREREQ(2, 38)
#    define CPLUS_C23_HAVE_MEMSET_EXPLICIT 1
#    define CPLUS_C23_HAVE_TIMESPEC_GETRES 1
#  endif
#endif
#if defined(CPLUS_C23_HOST_HAS_MEMSET_EXPLICIT)
#  define CPLUS_C23_HAVE_MEMSET_EXPLICIT 1
#endif
#if defined(CPLUS_C23_HOST_HAS_TIMESPEC_GETRES)
#  define CPLUS_C23_HAVE_TIMESPEC_GETRES 1
#endif
#if !defined(CPLUS_C23_HAVE_MEMSET_EXPLICIT)
#  define CPLUS_C23_HAVE_MEMSET_EXPLICIT 0
#endif
#if !defined(CPLUS_C23_HAVE_TIMESPEC_GETRES)
#  define CPLUS_C23_HAVE_TIMESPEC_GETRES 0
#endif
#if defined(FE_TIESAWAY)
#  define CPLUS_C23_HAVE_FE_TIESAWAY 1
#  define CPLUS_FE_TIESAWAY FE_TIESAWAY
#else
#  define CPLUS_C23_HAVE_FE_TIESAWAY 0
#endif

extern void cplus_libc_log_set_enabled(int enabled);
extern void cplus_libc_log(const char *api, const char *msg);
#if CPLUS_C23_HAVE_MEMSET_EXPLICIT
extern void *cplus_memset_explicit(void *s, int c, size_t n);
#endif
#if CPLUS_C23_HAVE_TIMESPEC_GETRES
extern int cplus_timespec_getres(struct timespec *ts, int base);
#endif

#define LOG_PREFIX "[CPLUS-LIBC] "

#if !(CPLUS_C23_HAVE_MEMSET_EXPLICIT && CPLUS_C23_HAVE_TIMESPEC_GETRES)
static const char blocker_functions[] =
    "host /usr/bin/clang -std=c17: no declaration in SDK headers and no "
    "libSystem symbol; pinned vendored musl 7813e9119eb5fe681b6750b8a033a049"
    "0bca0941 has no corresponding source; no substitute fabricated";
#endif
#if !CPLUS_C23_HAVE_FE_TIESAWAY
static const char blocker_fe_ties_away[] =
    "host /usr/bin/clang -std=c17: FE_TIESAWAY is not defined by <fenv.h> "
    "(only FE_TONEAREST, FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO); no "
    "vendored <fenv.h> to copy; no constant fabricated";
#endif

#ifndef _WIN32
/* Minimal, bounded stderr capture: the logger writes a handful of bytes with
 * write(2), so a blocking drain after closing the write end cannot hang. */
typedef struct {
    int saved;
    int rd;
    int wr;
    int active;
} Cap;

static int cap_begin(Cap *c) {
    int fds[2];
    c->saved = -1;
    c->rd = -1;
    c->wr = -1;
    c->active = 0;
    if (pipe(fds) != 0) return -1;
    c->rd = fds[0];
    c->wr = fds[1];
    c->saved = dup(STDERR_FILENO);
    if (c->saved < 0) {
        close(c->rd);
        close(c->wr);
        c->rd = c->wr = -1;
        return -1;
    }
    if (dup2(c->wr, STDERR_FILENO) < 0) {
        (void)dup2(c->saved, STDERR_FILENO);
        close(c->saved);
        close(c->rd);
        close(c->wr);
        c->saved = c->rd = c->wr = -1;
        return -1;
    }
    c->active = 1;
    return 0;
}

static size_t cap_end(Cap *c, char *buf, size_t bufsz) {
    size_t total = 0;
    if (bufsz == 0) return 0;
    if (c->active) {
        (void)fflush(stderr);
        (void)dup2(c->saved, STDERR_FILENO); /* restore before any assert */
        close(c->saved);
        close(c->wr);
        c->saved = -1;
        c->wr = -1;
        c->active = 0;
    }
    if (c->rd >= 0) {
        for (;;) {
            ssize_t n = read(c->rd, buf + total, bufsz - 1u - total);
            if (n > 0) {
                total += (size_t)n;
                if (total >= bufsz - 1u) break;
                continue;
            }
            break; /* 0 == EOF, < 0 == error: both end the drain */
        }
        close(c->rd);
        c->rd = -1;
    }
    buf[total] = '\0';
    return total;
}
#endif /* !_WIN32 */

/* Logging parity of the shared logger: an entry marker appears only when
 * logging is enabled, and toggling it never changes a returned value. */
static void test_logging_parity(void) {
    cplus_libc_log_set_enabled(0);
#ifndef _WIN32
    {
        Cap cap;
        char logbuf[512];
        size_t n;
        cplus_libc_log_set_enabled(1);
        if (cap_begin(&cap) == 0) {
            cplus_libc_log("c23_small_gaps", "parity-probe");
            n = cap_end(&cap, logbuf, sizeof logbuf);
            assert(n < sizeof logbuf);
            assert(strstr(logbuf, LOG_PREFIX "c23_small_gaps:") != NULL);
        }
        cplus_libc_log_set_enabled(0);
        if (cap_begin(&cap) == 0) {
            cplus_libc_log("c23_small_gaps", "parity-probe");
            n = cap_end(&cap, logbuf, sizeof logbuf);
            assert(n < sizeof logbuf);
            assert(strstr(logbuf, LOG_PREFIX) == NULL);
        }
    }
#else
    printf("note: stderr capture unavailable on Windows; logger marker "
           "parity checked by source contract only\n");
#endif
    cplus_libc_log_set_enabled(0);
}

#if CPLUS_C23_HAVE_MEMSET_EXPLICIT
static void test_memset_explicit(void) {
    unsigned char a[40], b[40];
    size_t i;
    void *r0, *r1;
    int e0, e1;

    for (i = 0u; i < sizeof a; ++i) {
        a[i] = (unsigned char)(i ^ 0xA5u);
        b[i] = a[i];
    }
    cplus_libc_log_set_enabled(0);
    errno = 0;
    r0 = cplus_memset_explicit(a, 0x5Cu, sizeof a);
    e0 = errno;
    cplus_libc_log_set_enabled(1);
    errno = 0;
    r1 = cplus_memset_explicit(b, 0x5Cu, sizeof b);
    e1 = errno;
    cplus_libc_log_set_enabled(0);
    assert(r0 == a && r1 == b);
    assert(e0 == e1 && e0 == 0);
    assert(memcmp(a, b, sizeof a) == 0);
    for (i = 0u; i < sizeof a; ++i) assert(a[i] == 0x5Cu);

    /* Boundary: n == 0 must leave the object untouched; n == 1 writes one
     * byte. Both are run under the two logging states and compared. */
    for (i = 0u; i < sizeof a; ++i) {
        a[i] = (unsigned char)(i ^ 0x3Cu);
        b[i] = a[i];
    }
    cplus_libc_log_set_enabled(0);
    assert(cplus_memset_explicit(a, 0xFF, 0u) == a);
    cplus_libc_log_set_enabled(1);
    assert(cplus_memset_explicit(b, 0xFF, 0u) == b);
    cplus_libc_log_set_enabled(0);
    assert(memcmp(a, b, sizeof a) == 0);
    assert(a[0] == 0x3Cu && a[sizeof a - 1u] == (unsigned char)((sizeof a - 1u) ^ 0x3Cu));

    cplus_libc_log_set_enabled(0);
    assert(cplus_memset_explicit(a, 0x11, 1u) == a);
    cplus_libc_log_set_enabled(1);
    assert(cplus_memset_explicit(b, 0x11, 1u) == b);
    cplus_libc_log_set_enabled(0);
    assert(memcmp(a, b, sizeof a) == 0);
    assert(a[0] == 0x11u && a[1] == (unsigned char)(1u ^ 0x3Cu));

    /* NULL is UB for the ISO C function; the wrapper gives it a defined
     * EINVAL result rather than faulting. */
    errno = 0;
    assert(cplus_memset_explicit(NULL, 0u, 4u) == NULL);
    assert(errno == EINVAL);

#ifndef _WIN32
    {
        Cap cap;
        char logbuf[512];
        size_t n;
        cplus_libc_log_set_enabled(1);
        if (cap_begin(&cap) == 0) {
            (void)cplus_memset_explicit(a, 0x22, 2u);
            n = cap_end(&cap, logbuf, sizeof logbuf);
            assert(n < sizeof logbuf);
            assert(strstr(logbuf, LOG_PREFIX "memset_explicit:") != NULL);
        }
        cplus_libc_log_set_enabled(0);
    }
#endif
}
#else
static void skip_memset_explicit(void) {
    printf("SKIP memset_explicit: %s\n", blocker_functions);
}
#endif /* CPLUS_C23_HAVE_MEMSET_EXPLICIT */

#if CPLUS_C23_HAVE_TIMESPEC_GETRES
static void test_timespec_getres(void) {
    struct timespec a, b;
    int r0, e0;

    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    cplus_libc_log_set_enabled(0);
    errno = 0;
    r0 = cplus_timespec_getres(&a, TIME_UTC);
    e0 = errno;
    cplus_libc_log_set_enabled(1);
    errno = 0;
    {
        int r1 = cplus_timespec_getres(&b, TIME_UTC);
        int e1 = errno;
        cplus_libc_log_set_enabled(0);
        assert(r0 == r1);
        assert(e0 == e1);
    }
    if (r0 != 0) {
        /* C23: returns the base value on success. */
        assert(r0 == TIME_UTC);
        assert(a.tv_sec >= 0 && a.tv_nsec >= 0 && a.tv_nsec < 1000000000L);
        assert(memcmp(&a, &b, sizeof a) == 0);
    } else {
        printf("note: host reports TIME_UTC resolution as unsupported\n");
    }

    /* Boundary: a base the host does not recognise returns 0 without
     * crashing; only off/on parity is asserted, not a specific host value. */
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    cplus_libc_log_set_enabled(0);
    {
        int bad0 = cplus_timespec_getres(&a, 12345);
        cplus_libc_log_set_enabled(1);
        int bad1 = cplus_timespec_getres(&b, 12345);
        cplus_libc_log_set_enabled(0);
        assert(bad0 == bad1);
        assert(memcmp(&a, &b, sizeof a) == 0);
    }

    errno = 0;
    assert(cplus_timespec_getres(NULL, TIME_UTC) == 0);
    assert(errno == EINVAL);

#ifndef _WIN32
    {
        Cap cap;
        char logbuf[512];
        size_t n;
        cplus_libc_log_set_enabled(1);
        if (cap_begin(&cap) == 0) {
            (void)cplus_timespec_getres(&a, TIME_UTC);
            n = cap_end(&cap, logbuf, sizeof logbuf);
            assert(n < sizeof logbuf);
            assert(strstr(logbuf, LOG_PREFIX "timespec_getres:") != NULL);
        }
        cplus_libc_log_set_enabled(0);
    }
#endif
}
#else
static void skip_timespec_getres(void) {
    printf("SKIP timespec_getres: %s\n", blocker_functions);
}
#endif /* CPLUS_C23_HAVE_TIMESPEC_GETRES */

#if CPLUS_C23_HAVE_FE_TIESAWAY
static void test_fe_ties_away(void) {
    assert(CPLUS_FE_TIESAWAY == FE_TIESAWAY);
#if defined(FE_TONEAREST) && defined(FE_TOWARDZERO)
    assert(CPLUS_FE_TIESAWAY != FE_TONEAREST);
    assert(CPLUS_FE_TIESAWAY != FE_TOWARDZERO);
#endif
}
#else
static void skip_fe_ties_away(void) {
    printf("SKIP FE_TIESAWAY: %s\n", blocker_fe_ties_away);
}
#endif /* CPLUS_C23_HAVE_FE_TIESAWAY */

int main(void) {
    test_logging_parity();
#if CPLUS_C23_HAVE_MEMSET_EXPLICIT
    test_memset_explicit();
#else
    skip_memset_explicit();
#endif
#if CPLUS_C23_HAVE_TIMESPEC_GETRES
    test_timespec_getres();
#else
    skip_timespec_getres();
#endif
#if CPLUS_C23_HAVE_FE_TIESAWAY
    test_fe_ties_away();
#else
    skip_fe_ties_away();
#endif
    cplus_libc_log_set_enabled(0);
    puts("c23_small: ok");
    return 0;
}
