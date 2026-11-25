/*
 * test_uchar_c23.c -- C23 UTF-8 conversions (cplus_mbrtoc8 / cplus_c8rtomb).
 *
 * Policy (mirrors tests/libc/test_c23_small_gaps.c): when a backend is present,
 * check ASCII conversion both ways, zero-width reset / length-only queries,
 * invalid-byte classification, and logging parity (identical results with
 * logging off vs on plus the entry marker in captured stderr). When it is
 * absent, print the EXACT upstream blocker and SKIP -- nothing is faked. The
 * logger plumbing itself is always exercised, so "logging parity" is verified
 * even on a host where these C23 entries are unavailable.
 *
 * Upstream status (see docs/libc/UCHARC23_UPSTREAM.md): no pinned musl revision
 * -- pinned commit 7813e9119eb5fe681b6750b8a033a0490bca0941, tag v1.2.6, or
 * master (2026-11-22) -- ships mbrtoc8.c / c8rtomb.c. They exist only as an
 * UNMERGED 2023 musl-ml patch that depends on musl-internal state encoding, so
 * no cplus_* body is compiled and this case is SKIPped.
 *
 * Define CPLUS_UCHAR_C23_HAVE_CONVERSIONS=1 only once a real backend exists.
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
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>   /* mbstate_t (also provided by cplus_uchar.h) */
#ifndef _WIN32
#include <unistd.h>
#endif

#ifndef CPLUS_UCHAR_C23_HAVE_CONVERSIONS
#define CPLUS_UCHAR_C23_HAVE_CONVERSIONS 0
#endif

extern void cplus_libc_log_set_enabled(int enabled);
extern void cplus_libc_log(const char *api, const char *msg);

#if CPLUS_UCHAR_C23_HAVE_CONVERSIONS
/* Local externs: the orchestrator adds the char8_t-typed declarations to
 * cplus_uchar.h; char8_t is defined there as unsigned char, so these match. */
extern size_t cplus_mbrtoc8(unsigned char *restrict pc8,
                            const char *restrict s, size_t n,
                            mbstate_t *restrict ps);
extern size_t cplus_c8rtomb(char *restrict s, unsigned char c8,
                            mbstate_t *restrict ps);
#else
static const char blocker_uchar_c23[] =
    "no mbrtoc8/c8rtomb source at pinned musl 7813e9119eb5fe681b6750b8a033a049"
    "0bca0941, tag v1.2.6, or master (2026-11-22); only an unmerged 2023 "
    "musl-ml patch exists (msg-id <a4faa3c9cef8b48d685f059ab8425ec3fd5087db."
    "1684932960.git.Jens.Gustedt@inria.fr>) that depends on musl-internal "
    "state; no substitute fabricated";
#endif

#define LOG_PREFIX "[CPLUS-LIBC] "

#ifndef _WIN32
/* Minimal bounded stderr capture: the logger writes a handful of bytes with
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
 * logging is enabled, and toggling it never changes a returned/llogged value.
 * Always run, including in the SKIP case. */
static void test_logging_parity(void) {
    cplus_libc_log_set_enabled(0);
#ifndef _WIN32
    {
        Cap cap;
        char logbuf[512];
        size_t n;
        cplus_libc_log_set_enabled(1);
        if (cap_begin(&cap) == 0) {
            cplus_libc_log("uchar_c23", "parity-probe");
            n = cap_end(&cap, logbuf, sizeof logbuf);
            assert(n < sizeof logbuf);
            assert(strstr(logbuf, LOG_PREFIX "uchar_c23:") != NULL);
        }
        cplus_libc_log_set_enabled(0);
        if (cap_begin(&cap) == 0) {
            cplus_libc_log("uchar_c23", "parity-probe");
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

#if CPLUS_UCHAR_C23_HAVE_CONVERSIONS
static void test_ascii_conversion(void) {
    mbstate_t st;
    unsigned char c8 = 0;
    char out[16];

    /* mbrtoc8: ASCII byte -> one UTF-8 code unit. */
    memset(&st, 0, sizeof st);
    errno = 0;
    assert(cplus_mbrtoc8(&c8, "A", 1, &st) == 1);
    assert(c8 == (unsigned char)'A');
    assert(errno == 0);

    /* c8rtomb: UTF-8 code unit -> multibyte byte, initial state. */
    memset(&st, 0, sizeof st);
    memset(out, 0, sizeof out);
    errno = 0;
    assert(cplus_c8rtomb(out, (unsigned char)'A', &st) == 1);
    assert(out[0] == 'A');
    assert(errno == 0);
}

static void test_zero_width_reset(void) {
    mbstate_t st;
    unsigned char c8 = 0xFFu;

    /* Empty input denotes the null wide character. */
    memset(&st, 0, sizeof st);
    errno = 0;
    assert(cplus_mbrtoc8(&c8, "", 1, &st) == 0);
    assert(c8 == 0);

    /* NULL destination is a length-only query; zeroed state stays usable. */
    memset(&st, 0, sizeof st);
    errno = 0;
    assert(cplus_mbrtoc8(NULL, "A", 1, &st) == 1);

    /* c8rtomb(s=NULL) is a pure reset query and must not fault. */
    memset(&st, 0, sizeof st);
    errno = 0;
    assert(cplus_c8rtomb(NULL, 0, &st) == 1);
}

static void test_invalid_byte(void) {
    mbstate_t st;
    unsigned char c8 = 0;

    /* A lone 0xFF is not a valid UTF-8 start byte. */
    memset(&st, 0, sizeof st);
    errno = 0;
    assert(cplus_mbrtoc8(&c8, "\xFF", 1, &st) == (size_t)-1);
    assert(errno == EILSEQ);
}

static void test_entry_logging(void) {
    mbstate_t st;
    unsigned char c8 = 0;
    char out[16];
#ifndef _WIN32
    Cap cap;
    char logbuf[512];
    size_t n;
#endif
    memset(&st, 0, sizeof st);
    memset(out, 0, sizeof out);

    cplus_libc_log_set_enabled(1);
#ifndef _WIN32
    if (cap_begin(&cap) == 0) {
        (void)cplus_mbrtoc8(&c8, "A", 1, &st);
        (void)cplus_c8rtomb(out, (unsigned char)'A', &st);
        n = cap_end(&cap, logbuf, sizeof logbuf);
        assert(n < sizeof logbuf);
        assert(strstr(logbuf, LOG_PREFIX "mbrtoc8:") != NULL);
        assert(strstr(logbuf, LOG_PREFIX "c8rtomb:") != NULL);
    }
#else
    (void)cplus_mbrtoc8(&c8, "A", 1, &st);
    (void)cplus_c8rtomb(out, (unsigned char)'A', &st);
#endif
    cplus_libc_log_set_enabled(0);
    assert(c8 == (unsigned char)'A' && out[0] == 'A'); /* logging is neutral */
}
#else
static void skip_uchar_c23(void) {
    printf("SKIP mbrtoc8/c8rtomb: %s\n", blocker_uchar_c23);
}
#endif /* CPLUS_UCHAR_C23_HAVE_CONVERSIONS */

int main(void) {
    test_logging_parity();
#if CPLUS_UCHAR_C23_HAVE_CONVERSIONS
    test_ascii_conversion();
    test_zero_width_reset();
    test_invalid_byte();
    test_entry_logging();
#else
    skip_uchar_c23();
#endif
    cplus_libc_log_set_enabled(0);
    puts("uchar_c23: ok");
    return 0;
}
