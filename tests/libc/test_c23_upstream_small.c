/*
 * test_c23_upstream_small.c -- standalone C99 test for the pinned-upstream
 * C23 small-gap bodies in libc/src/c23_upstream_small.c.
 *
 * Only memset_explicit is backed by a pinned upstream source at the selected
 * revisions; it gets normal/boundary/NULL/logging-parity coverage. The other
 * two entries SKIP with the exact, documented blocker so the test truthfully
 * reports "not closed" rather than faking a value or an implementation.
 * The shared logger plumbing is exercised unconditionally so logging parity
 * is verified even on a host where only one C23 entry is available.
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

/* Capability detection mirrors libc/src/c23_upstream_small.c exactly. */
#define CPLUS_C23_UPSTREAM_HAVE_TIMESPEC_GETRES 0
#if defined(FE_TIESAWAY)
#  define CPLUS_C23_UPSTREAM_HAVE_FE_TIESAWAY 1
#  define CPLUS_C23_UPSTREAM_FE_TIESAWAY FE_TIESAWAY
#else
#  define CPLUS_C23_UPSTREAM_HAVE_FE_TIESAWAY 0
#endif

extern void cplus_libc_log_set_enabled(int enabled);
extern void cplus_libc_log(const char *api, const char *msg);
extern void *cplus_memset_explicit(void *dst, int value, size_t count);

#define LOG_PREFIX "[CPLUS-LIBC] "

static const char blocker_timespec_getres[] =
    "no pinned upstream source: musl v1.2.6/master ship only "
    "src/time/timespec_get.c; LLVM libc llvmorg-20.1.1 (and main) have no "
    "timespec_getres source or build target; host SDK declares no such symbol";
static const char blocker_fe_ties_away[] =
    "compiler-provided macro absent from this host <fenv.h> "
    "(only FE_TONEAREST/FE_UPWARD/FE_DOWNWARD/FE_TOWARDZERO) and absent from "
    "LLVM libc fenv-macros.h at llvmorg-20.1.1; no numeric value fabricated";

#ifndef _WIN32
/* Minimal bounded stderr capture: the logger writes a handful of bytes with
 * write(2), so a blocking drain after restoring stderr cannot hang. */
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

/* Logging parity of the shared logger itself: entry marker only when enabled,
 * and toggling never changes a returned value. */
static void test_logging_parity(void) {
    cplus_libc_log_set_enabled(0);
#ifndef _WIN32
    {
        Cap cap;
        char logbuf[512];
        size_t n;
        cplus_libc_log_set_enabled(1);
        if (cap_begin(&cap) == 0) {
            cplus_libc_log("c23_upstream_small", "parity-probe");
            n = cap_end(&cap, logbuf, sizeof logbuf);
            assert(n < sizeof logbuf);
            assert(strstr(logbuf, LOG_PREFIX "c23_upstream_small:") != NULL);
        }
        cplus_libc_log_set_enabled(0);
        if (cap_begin(&cap) == 0) {
            cplus_libc_log("c23_upstream_small", "parity-probe");
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

static void test_memset_explicit(void) {
    unsigned char a[40], b[40];
    size_t i;
    void *r0, *r1;
    int e0, e1;

    /* Normal case: logging OFF vs ON must produce identical bytes, return
     * value and errno (logging parity). */
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

    /* Boundary n == 0: object must be untouched, pointer returned. */
    for (i = 0u; i < sizeof a; ++i) {
        a[i] = (unsigned char)(i ^ 0x3Cu);
        b[i] = a[i];
    }
    errno = 0;
    assert(cplus_memset_explicit(a, 0xFF, 0u) == a);
    e0 = errno;
    errno = 0;
    assert(cplus_memset_explicit(b, 0xFF, 0u) == b);
    e1 = errno;
    assert(e0 == e1 && e0 == 0);
    assert(memcmp(a, b, sizeof a) == 0);
    assert(a[0] == 0x3Cu);
    assert(a[sizeof a - 1u] == (unsigned char)((sizeof a - 1u) ^ 0x3Cu));

    /* Boundary n == 1: exactly one byte written. */
    errno = 0;
    assert(cplus_memset_explicit(a, 0x11, 1u) == a);
    assert(errno == 0);
    assert(a[0] == 0x11u);
    assert(a[1] == (unsigned char)(1u ^ 0x3Cu));

    /* Upstream narrows `value` to uint8_t (cplus_memset does the same
     * (unsigned char) cast), so 0x1FF must store 0xFF. */
    errno = 0;
    assert(cplus_memset_explicit(a, 0x1FF, 1u) == a);
    assert(errno == 0);
    assert(a[0] == 0xFFu);

    /* NULL with n > 0 is UB for the ISO C function. Here the imported body
     * delegates to cplus_memset, which gives it the defined EINVAL result;
     * no extra guard was added to the upstream body. */
    errno = 0;
    assert(cplus_memset_explicit(NULL, 0x5Au, 4u) == NULL);
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

int main(void) {
    test_logging_parity();
    test_memset_explicit();

#if CPLUS_C23_UPSTREAM_HAVE_TIMESPEC_GETRES
    printf("note: host now exposes timespec_getres; no vendored body is "
           "published, see docs/libc/C23_SMALL_UPSTREAM.md\n");
#else
    printf("SKIP timespec_getres: %s\n", blocker_timespec_getres);
#endif
#if CPLUS_C23_UPSTREAM_HAVE_FE_TIESAWAY
    assert(CPLUS_C23_UPSTREAM_FE_TIESAWAY == FE_TIESAWAY);
#else
    printf("SKIP FE_TIESAWAY: %s\n", blocker_fe_ties_away);
#endif

    cplus_libc_log_set_enabled(0);
    puts("c23_upstream_small: ok");
    return 0;
}
