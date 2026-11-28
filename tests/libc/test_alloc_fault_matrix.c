/* Deterministic allocation-failure matrix for the hardened allocation wrappers.
 *
 * WHY THESE HOOKS
 * ---------------
 * The `stdlib_alloc_fail_at/after/reset` and telemetry helpers named in the
 * task live in `stdlib/error.hp`, which is a C+ (C+) header: it declares
 * `class error`, relies on C+ method sugar and defines a weak C+ state object.
 * It is NOT includable from a plain C99 translation unit.  Per the task, this
 * matrix therefore uses the C-callable hooks of the two allocation APIs that
 * are linked here:
 *
 *   hardened libc : cplus_alloc_fail_after() / cplus_alloc_fail_reset()
 *                   (declared in libc/install-include/cplus_libc.h)
 *   SafeCRT       : cplus_safe_alloc_fail_after() / _reset() / _seq()
 *                   (declared in stdlib/safe/safe_alloc.h)
 *
 * Hook semantics differ from stdlib_alloc_fail_at(n) ("n-th call fails once"):
 * both C hooks are *latching* ("N successes, then every later call fails until
 * reset").  Mapping used below, with exact assertions in each section:
 *
 *   fail_at=0   -> fail_reset()                       (injection disabled)
 *   fail_at=1   -> fail_after(0) -> failing call -> fail_reset() -> next call
 *                  succeeds: one-shot failure + explicit recovery
 *   fail_after=0-> fail_after(0) -> two consecutive failures -> fail_reset()
 *                  -> recovery (verifies the latching behaviour itself)
 *
 * Coverage: cplus_malloc, cplus_calloc, cplus_realloc, cplus_free,
 * cplus_free_checked, malloc_s, calloc_s, realloc_s, free_s, free_checked_s
 * and aligned_alloc_s.  cplus_aligned_alloc is deliberately NOT in the matrix:
 * it is a plain host-backed wrapper with no owned metadata and no injection
 * hook (see docs/libc/ALLOC_FAULT_MATRIX.md).
 *
 * The test never exhausts memory: SIZE_MAX requests are refused by the
 * wrappers' own arithmetic/host allocator before any allocation is attempted.
 *
 * Build (from the repository root, no library rebuild):
 *   /usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
 *     -Ilibc/install-include -I. \
 *     tests/libc/test_alloc_fault_matrix.c \
 *     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
 *     -lm -pthread -o /tmp/alloc_fault_matrix
 *
 * UBSan variant: add -fsanitize=undefined -fno-sanitize-recover=undefined.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cplus_libc.h"             /* hardened wrappers + cplus_alloc_* hooks */
#include "stdlib/safe/safe_alloc.h" /* _s wrappers + cplus_safe_alloc_* hooks  */
#include "stdlib/safe/safe_log.h"   /* logging level control                  */

/* ---- harness ------------------------------------------------------------ */

static int g_checks = 0;
static int g_fail = 0;
static const char *g_op = "?";

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) {                                                         \
            ++g_fail;                                                          \
            printf("FINDING: %s: %s\n", g_op, (msg));                          \
        }                                                                      \
    } while (0)

#define CHECKF(cond, fmt, ...)                                                 \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) {                                                         \
            ++g_fail;                                                          \
            printf("FINDING: %s: ", g_op);                                     \
            printf((fmt), __VA_ARGS__);                                        \
            putchar('\n');                                                     \
        }                                                                      \
    } while (0)

static int g_saved_stderr = -1;

static void mute_stderr(void) {
    int fd = open("/dev/null", O_WRONLY);
    if (fd < 0) return;
    g_saved_stderr = dup(STDERR_FILENO);
    if (g_saved_stderr < 0 || dup2(fd, STDERR_FILENO) < 0) {
        if (g_saved_stderr >= 0) (void)close(g_saved_stderr);
        g_saved_stderr = -1;
    }
    (void)close(fd);
}

static void unmute_stderr(void) {
    if (g_saved_stderr >= 0) {
        (void)dup2(g_saved_stderr, STDERR_FILENO);
        (void)close(g_saved_stderr);
        g_saved_stderr = -1;
    }
}

static void set_logging(int on) {
    cplus_libc_log_set_enabled(on);
    cplus_safe_log_set_level(on ? CPLUS_SAFE_LOG_TRACE : CPLUS_SAFE_LOG_OFF);
}

/* ---- one row of the matrix: an allocation family ------------------------ */

typedef struct alloc_family {
    void *(*malloc1)(size_t);
    void *(*calloc2)(size_t, size_t);
    void *(*realloc2)(void *, size_t);
    void (*free1)(void *);
    void (*fail_after)(long);
    void (*fail_reset)(void);
    unsigned long (*seq)(void);          /* NULL: hardened lib has no seq API */
    size_t (*qsize)(const void *);
    size_t (*live_count)(void);
    unsigned long (*double_free_count)(void); /* hardened only */
    int (*was_freed)(const void *);            /* SafeCRT only */
    int malloc_overflow_errno;
    int calloc_overflow_errno;
    int realloc_overflow_errno;
} alloc_family;

static const alloc_family g_hardened = {
    cplus_malloc, cplus_calloc, cplus_realloc, cplus_free,
    cplus_alloc_fail_after, cplus_alloc_fail_reset, NULL,
    cplus_alloc_size, cplus_alloc_live_count,
    cplus_alloc_double_free_count, NULL,
    ENOMEM, ENOMEM, ENOMEM
};

static const alloc_family g_safecrt = {
    malloc_s, calloc_s, realloc_s, free_s,
    cplus_safe_alloc_fail_after, cplus_safe_alloc_fail_reset,
    cplus_safe_alloc_seq, cplus_safe_ptr_size, cplus_safe_alloc_live_count,
    NULL, cplus_safe_ptr_was_freed,
    EINVAL, EOVERFLOW, EINVAL
};

/* n < 0 means "injection off".  Always go through reset first: it clears the
 * latching counter and (on SafeCRT) zeroes the observable attempt sequence. */
static void inject(const alloc_family *f, long n) {
    f->fail_reset();
    if (n >= 0) f->fail_after(n);
}

static unsigned long seq_now(const alloc_family *f) {
    return f->seq ? f->seq() : 0UL;
}

/* ---- malloc-like wrappers ----------------------------------------------- */

static void parity_malloc(const alloc_family *f, size_t n, int injected) {
    void *p0, *p1;
    int e0, e1;
    unsigned long s0, s1;

    mute_stderr();
    inject(f, injected ? 0 : -1);
    set_logging(0);
    errno = 0; p0 = f->malloc1(n); e0 = errno; s0 = seq_now(f);

    inject(f, injected ? 0 : -1);
    set_logging(1);
    errno = 0; p1 = f->malloc1(n); e1 = errno; s1 = seq_now(f);

    set_logging(0);
    unmute_stderr();

    CHECK((p0 == NULL) == (p1 == NULL), "log-parity: NULL return");
    CHECK(e0 == e1, "log-parity: errno differs with logging on/off");
    CHECK(e0 == (injected ? ENOMEM : 0), "log-parity: expected errno");
    CHECK(s0 == s1, "log-parity: attempt sequence");
    if (p0 != NULL && p1 != NULL)
        CHECK(f->qsize(p0) == f->qsize(p1), "log-parity: metadata size");
    f->free1(p0);
    f->free1(p1);
}

static void test_malloc_like(const alloc_family *f, const char *label) {
    void *p, *q;
    int e;
    size_t live0;

    g_op = label;
    live0 = f->live_count();

    /* fail_at=0: normal success, exact metadata, no leak. */
    inject(f, -1);
    errno = 0;
    p = f->malloc1(64);
    e = errno;
    CHECK(p != NULL, "off: expected a block");
    CHECK(e == 0, "off: errno must be 0");
    if (p != NULL) {
        CHECK(f->qsize(p) == 64, "off: metadata size");
        memset(p, 0x5a, 64);              /* exact-fit payload is writable */
    }
    CHECK(f->live_count() == live0 + 1, "off: live count +1");
    f->free1(p);
    CHECK(f->live_count() == live0, "off: live count restored");

    /* fail_at=1 (one-shot): first failure, explicit reset, then recovery. */
    inject(f, 0);
    errno = 0;
    p = f->malloc1(32);
    e = errno;
    CHECK(p == NULL, "fail_at=1: expected NULL");
    CHECK(e == ENOMEM, "fail_at=1: expected ENOMEM");
    CHECK(f->live_count() == live0, "fail_at=1: no block recorded");
    CHECK(seq_now(f) == (f->seq ? 1UL : 0UL), "fail_at=1: attempt sequence");
    f->fail_reset();
    errno = 0;
    p = f->malloc1(32);
    CHECK(p != NULL, "fail_at=1: recovery after reset");
    CHECK(errno == 0, "fail_at=1: recovery errno");
    f->free1(p);

    /* fail_after=0 (latching): repeated failure until reset. */
    inject(f, 0);
    errno = 0; p = f->malloc1(8);
    CHECK(p == NULL, "fail_after=0: first call NULL");
    CHECK(errno == ENOMEM, "fail_after=0: first call ENOMEM");
    errno = 0; q = f->malloc1(8);
    CHECK(q == NULL, "fail_after=0: second call still NULL");
    CHECK(errno == ENOMEM, "fail_after=0: second call still ENOMEM");
    CHECK(seq_now(f) == (f->seq ? 2UL : 0UL), "fail_after=0: both calls counted");
    f->fail_reset();
    p = f->malloc1(8);
    CHECK(p != NULL, "fail_after=0: recovery after reset");
    f->free1(p);

    /* zero-size: defined, tracked 1-byte block. */
    inject(f, -1);
    errno = 0;
    p = f->malloc1(0);
    e = errno;
    CHECK(p != NULL, "zero-size: defined 1-byte block");
    CHECK(e == 0, "zero-size: errno");
    if (p != NULL) CHECK(f->qsize(p) == 1, "zero-size: metadata size is 1");
    f->free1(p);

    /* exact-fit. */
    errno = 0;
    p = f->malloc1(37);
    CHECK(p != NULL, "exact-fit: return");
    CHECK(errno == 0, "exact-fit: errno");
    if (p != NULL) {
        memset(p, 0x3c, 37);
        CHECK(f->qsize(p) == 37, "exact-fit: metadata size");
    }
    f->free1(p);

    /* overflow size (SIZE_MAX): refused, no block, no memory exhaustion. */
    errno = 0;
    p = f->malloc1(SIZE_MAX);
    e = errno;
    CHECK(p == NULL, "SIZE_MAX: NULL");
    CHECK(e == f->malloc_overflow_errno, "SIZE_MAX: expected errno");
    CHECK(f->live_count() == live0, "SIZE_MAX: no allocation recorded");

    /* logging ON/OFF parity: success and injected failure. */
    parity_malloc(f, 24, 0);
    parity_malloc(f, 24, 1);
}

/* ---- calloc-like wrappers ------------------------------------------------ */

static void parity_calloc(const alloc_family *f, size_t n, size_t sz,
                          int injected) {
    void *p0, *p1;
    int e0, e1;
    unsigned long s0, s1;

    mute_stderr();
    inject(f, injected ? 0 : -1);
    set_logging(0);
    errno = 0; p0 = f->calloc2(n, sz); e0 = errno; s0 = seq_now(f);

    inject(f, injected ? 0 : -1);
    set_logging(1);
    errno = 0; p1 = f->calloc2(n, sz); e1 = errno; s1 = seq_now(f);

    set_logging(0);
    unmute_stderr();

    CHECK((p0 == NULL) == (p1 == NULL), "log-parity: NULL return");
    CHECK(e0 == e1, "log-parity: errno differs with logging on/off");
    CHECK(e0 == (injected ? ENOMEM : 0), "log-parity: expected errno");
    CHECK(s0 == s1, "log-parity: attempt sequence");
    if (p0 != NULL && p1 != NULL)
        CHECK(f->qsize(p0) == f->qsize(p1), "log-parity: metadata size");
    f->free1(p0);
    f->free1(p1);
}

static int all_zero(const void *p, size_t n) {
    const unsigned char *b = (const unsigned char *)p;
    size_t i;
    for (i = 0; i < n; ++i)
        if (b[i] != 0) return 0;
    return 1;
}

static void test_calloc_like(const alloc_family *f, const char *label) {
    void *p, *q;
    int e;
    size_t live0;

    g_op = label;
    live0 = f->live_count();

    /* fail_at=0: success, zeroed payload, byte size = count * element size. */
    inject(f, -1);
    errno = 0;
    p = f->calloc2(4, 12);
    e = errno;
    CHECK(p != NULL, "off: expected a block");
    CHECK(e == 0, "off: errno must be 0");
    if (p != NULL) {
        CHECK(f->qsize(p) == 48, "off: metadata byte size");
        CHECK(all_zero(p, 48), "off: payload is zeroed");
    }
    CHECK(f->live_count() == live0 + 1, "off: live count +1");
    f->free1(p);
    CHECK(f->live_count() == live0, "off: live count restored");

    /* fail_at=1 (one-shot). */
    inject(f, 0);
    errno = 0;
    p = f->calloc2(4, 12);
    e = errno;
    CHECK(p == NULL, "fail_at=1: expected NULL");
    CHECK(e == ENOMEM, "fail_at=1: expected ENOMEM");
    CHECK(seq_now(f) == (f->seq ? 1UL : 0UL), "fail_at=1: attempt sequence");
    f->fail_reset();
    p = f->calloc2(4, 12);
    CHECK(p != NULL, "fail_at=1: recovery after reset");
    f->free1(p);

    /* fail_after=0 (latching). */
    inject(f, 0);
    errno = 0; p = f->calloc2(1, 8);
    CHECK(p == NULL, "fail_after=0: first call NULL");
    CHECK(errno == ENOMEM, "fail_after=0: first call ENOMEM");
    errno = 0; q = f->calloc2(1, 8);
    CHECK(q == NULL, "fail_after=0: second call still NULL");
    CHECK(errno == ENOMEM, "fail_after=0: second call still ENOMEM");
    CHECK(seq_now(f) == (f->seq ? 2UL : 0UL), "fail_after=0: both calls counted");
    f->fail_reset();
    p = f->calloc2(1, 8);
    CHECK(p != NULL, "fail_after=0: recovery after reset");
    f->free1(p);

    /* zero-size: both (0, n) and (n, 0) give a tracked, zeroed 1-byte block. */
    inject(f, -1);
    p = f->calloc2(0, 8);
    CHECK(p != NULL, "zero-size count: block");
    if (p != NULL) {
        CHECK(f->qsize(p) == 1, "zero-size count: metadata size is 1");
        CHECK(all_zero(p, 1), "zero-size count: zeroed");
    }
    f->free1(p);
    p = f->calloc2(8, 0);
    CHECK(p != NULL, "zero-size element: block");
    if (p != NULL) {
        CHECK(f->qsize(p) == 1, "zero-size element: metadata size is 1");
        CHECK(all_zero(p, 1), "zero-size element: zeroed");
    }
    f->free1(p);

    /* exact-fit. */
    p = f->calloc2(7, 9);
    CHECK(p != NULL, "exact-fit: return");
    if (p != NULL) {
        CHECK(f->qsize(p) == 63, "exact-fit: metadata byte size");
        CHECK(all_zero(p, 63), "exact-fit: zeroed");
    }
    f->free1(p);

    /* overflow size: count*size beyond SIZE_MAX and beyond the safe limit. */
    errno = 0;
    p = f->calloc2(2, SIZE_MAX);
    e = errno;
    CHECK(p == NULL, "SIZE_MAX product: NULL");
    CHECK(e == f->calloc_overflow_errno, "SIZE_MAX product: expected errno");
    errno = 0;
    p = f->calloc2(SIZE_MAX, 2);
    e = errno;
    CHECK(p == NULL, "SIZE_MAX count: NULL");
    CHECK(e == f->calloc_overflow_errno, "SIZE_MAX count: expected errno");
    CHECK(f->live_count() == live0, "overflow: no allocation recorded");

    /* logging parity. */
    parity_calloc(f, 5, 16, 0);
    parity_calloc(f, 5, 16, 1);
}

/* ---- realloc-like wrappers ---------------------------------------------- */

static void parity_realloc(const alloc_family *f, int injected) {
    void *base0, *base1, *r0, *r1;
    int e0, e1;
    unsigned long s0, s1;
    size_t z0, z1;

    mute_stderr();
    inject(f, -1);
    set_logging(0);
    base0 = f->malloc1(20);
    inject(f, injected ? 0 : -1);
    errno = 0; r0 = f->realloc2(base0, 48); e0 = errno; s0 = seq_now(f);
    z0 = f->qsize(r0 != NULL ? r0 : base0);

    inject(f, -1);
    set_logging(1);
    base1 = f->malloc1(20);
    inject(f, injected ? 0 : -1);
    errno = 0; r1 = f->realloc2(base1, 48); e1 = errno; s1 = seq_now(f);
    z1 = f->qsize(r1 != NULL ? r1 : base1);

    set_logging(0);
    unmute_stderr();

    CHECK((r0 == NULL) == (r1 == NULL), "log-parity: NULL return");
    CHECK(e0 == e1, "log-parity: errno differs with logging on/off");
    CHECK(e0 == (injected ? ENOMEM : 0), "log-parity: expected errno");
    CHECK(s0 == s1, "log-parity: attempt sequence");
    CHECK(z0 == z1, "log-parity: owner metadata size");
    f->free1(r0 != NULL ? r0 : base0);
    f->free1(r1 != NULL ? r1 : base1);
}

static void test_realloc_like(const alloc_family *f, const char *label) {
    void *p, *q;
    int e;
    size_t live0;

    g_op = label;
    live0 = f->live_count();

    /* NULL realloc acts like malloc. */
    inject(f, -1);
    errno = 0;
    p = f->realloc2(NULL, 32);
    e = errno;
    CHECK(p != NULL, "NULL realloc: returns a block");
    CHECK(e == 0, "NULL realloc: errno");
    if (p != NULL) {
        CHECK(f->qsize(p) == 32, "NULL realloc: metadata size");
        memset(p, 0x11, 32);
    }
    CHECK(f->live_count() == live0 + 1, "NULL realloc: live count +1");

    /* exact-fit grow then shrink, preserving the payload. */
    q = f->realloc2(p, 96);
    CHECK(q != NULL, "grow: return");
    if (q != NULL) {
        p = q;
        CHECK(f->qsize(p) == 96, "grow: metadata size");
        CHECK(((unsigned char *)p)[0] == 0x11, "grow: payload preserved");
    }
    q = f->realloc2(p, 40);
    CHECK(q != NULL, "shrink: return");
    if (q != NULL) {
        p = q;
        CHECK(f->qsize(p) == 40, "shrink: metadata size");
    }
    f->free1(p);
    CHECK(f->live_count() == live0, "grow/shrink: live count restored");

    /* fail_at=1 (one-shot) on a live owner: failure preserves the block. */
    p = f->malloc1(16);
    CHECK(p != NULL, "inject: setup block");
    inject(f, 0);
    errno = 0;
    q = f->realloc2(p, 64);
    e = errno;
    CHECK(q == NULL, "realloc injected: NULL");
    CHECK(e == ENOMEM, "realloc injected: ENOMEM");
    CHECK(f->live_count() == live0 + 1, "realloc injected: owner still live");
    CHECK(f->qsize(p) == 16, "realloc injected: owner size preserved");
    CHECK(seq_now(f) == (f->seq ? 1UL : 0UL), "realloc injected: sequence");
    f->fail_reset();
    errno = 0;
    q = f->realloc2(p, 64);
    CHECK(q != NULL, "realloc injected: recovery after reset");
    if (q != NULL) {
        p = q;
        CHECK(f->qsize(p) == 64, "realloc recovery: metadata size");
    }
    f->free1(p);

    /* fail_after=0 (latching) through the NULL-owner path. */
    inject(f, 0);
    errno = 0; q = f->realloc2(NULL, 16);
    CHECK(q == NULL, "realloc fail_after=0: first call NULL");
    CHECK(errno == ENOMEM, "realloc fail_after=0: ENOMEM");
    q = f->realloc2(NULL, 16);
    CHECK(q == NULL, "realloc fail_after=0: second call still NULL");
    CHECK(seq_now(f) == (f->seq ? 2UL : 0UL), "realloc fail_after=0: sequence");
    f->fail_reset();

    /* zero-size realloc: defined 1-byte block. */
    p = f->malloc1(24);
    errno = 0;
    q = f->realloc2(p, 0);
    CHECK(q != NULL, "zero-size realloc: block");
    CHECK(errno == 0, "zero-size realloc: errno");
    if (q != NULL) {
        p = q;
        CHECK(f->qsize(p) == 1, "zero-size realloc: metadata size is 1");
    }
    f->free1(p);

    /* overflow SIZE_MAX with a live owner: refusal must preserve the owner. */
    p = f->malloc1(24);
    errno = 0;
    q = f->realloc2(p, SIZE_MAX);
    e = errno;
    CHECK(q == NULL, "realloc SIZE_MAX: NULL");
    CHECK(e == f->realloc_overflow_errno, "realloc SIZE_MAX: expected errno");
    CHECK(f->qsize(p) == 24, "realloc SIZE_MAX: owner size preserved");
    CHECK(f->live_count() == live0 + 1, "realloc SIZE_MAX: owner still live");
    f->free1(p);

    /* overflow SIZE_MAX with NULL owner: routes through the malloc-like guard. */
    errno = 0;
    q = f->realloc2(NULL, SIZE_MAX);
    e = errno;
    CHECK(q == NULL, "realloc(NULL,SIZE_MAX): NULL");
    CHECK(e == f->malloc_overflow_errno, "realloc(NULL,SIZE_MAX): expected errno");

    /* logging parity: success and injected failure, with owner preserved. */
    parity_realloc(f, 0);
    parity_realloc(f, 1);
}

/* ---- free wrappers ------------------------------------------------------- */

/* Ordinary free + double-free refusal.  The two families expose the refusal
 * through different diagnosis APIs: hardened has a double-free counter,
 * SafeCRT has cplus_safe_ptr_was_freed().  We never dereference a freed block. */
static void test_plain_free(const alloc_family *f, const char *label) {
    void *p;
    size_t live0 = f->live_count();
    unsigned long df0, df1;

    g_op = label;
    inject(f, -1);
    p = f->malloc1(24);
    CHECK(p != NULL, "free: setup block");
    f->free1(p);
    CHECK(f->live_count() == live0, "free: live count restored");

    df0 = f->double_free_count ? f->double_free_count() : 0UL;
    f->free1(p);                       /* double-free attempt: must be refused */
    df1 = f->double_free_count ? f->double_free_count() : 0UL;
    CHECK(f->live_count() == live0, "free: double free did not change live count");
    if (f->double_free_count)
        CHECK(df1 == df0 + 1, "free: double-free counter incremented once");
    if (f->was_freed)
        CHECK(f->was_freed(p) == 1, "free: pointer recorded as freed");
}

/* Hardened checked free takes void ** and nulls the handle on success. */
static void test_checked_free_hardened(void) {
    const alloc_family *f = &g_hardened;
    void *p, *alias, *a0, *a1, *h0, *h1, *s0, *s1;
    int rc, e, r_ok0, r_ok1, e_ok0, e_ok1, r_ref0, r_ref1, e_ref0, e_ref1;
    size_t live0 = f->live_count(), l0, l1;

    g_op = "cplus_free_checked";
    inject(f, -1);
    p = f->malloc1(16);
    CHECK(p != NULL, "checked free: setup block");
    alias = p;
    errno = 0;
    rc = cplus_free_checked(&alias);
    e = errno;
    CHECK(rc == 0, "checked free: success rc");
    CHECK(alias == NULL, "checked free: handle nulled");
    CHECK(e == 0, "checked free: errno on success");

    errno = 0;
    rc = cplus_free_checked(&p);       /* p is a stale alias of the freed block */
    e = errno;
    CHECK(rc != 0, "checked free: double free refused");
    CHECK(e == EINVAL, "checked free: EINVAL on refusal");
    CHECK(f->live_count() == live0, "checked free: live count restored");

    errno = 0;
    rc = cplus_free_checked(&alias);   /* alias is NULL now */
    e = errno;
    CHECK(rc == 0, "checked free: NULL handle is a no-op");
    CHECK(e == 0, "checked free: errno on NULL no-op");

    /* logging parity: keep success and refusal results separate. */
    mute_stderr();
    inject(f, -1);
    set_logging(0);
    a0 = f->malloc1(16); h0 = a0;
    errno = 0; r_ok0 = cplus_free_checked(&h0); e_ok0 = errno;
    l0 = f->live_count();
    s0 = a0;
    errno = 0; r_ref0 = cplus_free_checked(&s0); e_ref0 = errno;

    inject(f, -1);
    set_logging(1);
    a1 = f->malloc1(16); h1 = a1;
    errno = 0; r_ok1 = cplus_free_checked(&h1); e_ok1 = errno;
    l1 = f->live_count();
    s1 = a1;
    errno = 0; r_ref1 = cplus_free_checked(&s1); e_ref1 = errno;
    set_logging(0);
    unmute_stderr();

    CHECK(r_ok0 == r_ok1, "log-parity: checked-free success rc");
    CHECK(e_ok0 == e_ok1, "log-parity: checked-free success errno");
    CHECK(l0 == l1, "log-parity: checked-free live count");
    CHECK(r_ref0 == r_ref1, "log-parity: checked-free refusal rc");
    CHECK(e_ref0 == e_ref1, "log-parity: checked-free refusal errno");
    CHECK(e_ref0 == EINVAL, "log-parity: checked-free refusal expected errno");
    (void)a0; (void)a1;
}

/* SafeCRT checked free takes the pointer by value and never rewrites the
 * caller's alias; success is CPLUS_SAFE_OK, refusal CPLUS_SAFE_EINVAL+errno. */
static void test_checked_free_safecrt(void) {
    const alloc_family *f = &g_safecrt;
    void *p, *a0, *a1;
    int rc, e, r_ok0, r_ok1, e_ok0, e_ok1, r_ref0, r_ref1, e_ref0, e_ref1;
    size_t live0 = f->live_count(), l0, l1;

    g_op = "free_checked_s";
    inject(f, -1);
    p = malloc_s(16);
    CHECK(p != NULL, "checked free: setup block");
    errno = 0;
    rc = free_checked_s(p);
    e = errno;
    CHECK(rc == CPLUS_SAFE_OK, "checked free: success rc");
    CHECK(e == 0, "checked free: errno on success");
    CHECK(p != NULL, "checked free: caller alias unchanged (by-value API)");

    errno = 0;
    rc = free_checked_s(p);            /* stale alias */
    e = errno;
    CHECK(rc == CPLUS_SAFE_EINVAL, "checked free: double free refused");
    CHECK(e == EINVAL, "checked free: EINVAL on refusal");
    CHECK(f->live_count() == live0, "checked free: live count restored");

    errno = 0;
    rc = free_checked_s(NULL);
    e = errno;
    CHECK(rc == CPLUS_SAFE_OK, "checked free: NULL is a no-op");
    CHECK(e == 0, "checked free: errno on NULL no-op");

    mute_stderr();
    inject(f, -1);
    set_logging(0);
    a0 = malloc_s(16);
    errno = 0; r_ok0 = free_checked_s(a0); e_ok0 = errno;
    l0 = f->live_count();
    errno = 0; r_ref0 = free_checked_s(a0); e_ref0 = errno;

    inject(f, -1);
    set_logging(1);
    a1 = malloc_s(16);
    errno = 0; r_ok1 = free_checked_s(a1); e_ok1 = errno;
    l1 = f->live_count();
    errno = 0; r_ref1 = free_checked_s(a1); e_ref1 = errno;
    set_logging(0);
    unmute_stderr();

    CHECK(r_ok0 == r_ok1, "log-parity: checked-free success rc");
    CHECK(e_ok0 == e_ok1, "log-parity: checked-free success errno");
    CHECK(l0 == l1, "log-parity: checked-free live count");
    CHECK(r_ref0 == r_ref1, "log-parity: checked-free refusal rc");
    CHECK(e_ref0 == e_ref1, "log-parity: checked-free refusal errno");
    CHECK(e_ref0 == EINVAL, "log-parity: checked-free refusal expected errno");
}

/* ---- aligned_alloc_s (SafeCRT only) ------------------------------------- */

static void parity_aligned(void) {
    void *p0, *p1;
    int e0, e1;
    unsigned long s0, s1;

    mute_stderr();
    inject(&g_safecrt, 0);
    set_logging(0);
    errno = 0; p0 = aligned_alloc_s(16, 64); e0 = errno; s0 = seq_now(&g_safecrt);

    inject(&g_safecrt, 0);
    set_logging(1);
    errno = 0; p1 = aligned_alloc_s(16, 64); e1 = errno; s1 = seq_now(&g_safecrt);

    set_logging(0);
    unmute_stderr();

    CHECK(p0 == NULL && p1 == NULL, "log-parity: aligned injected NULL");
    CHECK(e0 == e1, "log-parity: aligned errno");
    CHECK(e0 == ENOMEM, "log-parity: aligned expected ENOMEM");
    CHECK(s0 == s1, "log-parity: aligned attempt sequence");
    free_s(p0);
    free_s(p1);
}

static void test_aligned_alloc_s(void) {
    void *p;
    int e;
    size_t live0 = cplus_safe_alloc_live_count();

    g_op = "aligned_alloc_s";
    inject(&g_safecrt, -1);

    errno = 0;
    p = aligned_alloc_s(16, 64);
    e = errno;
    CHECK(p != NULL, "off: aligned block");
    CHECK(e == 0, "off: errno");
    if (p != NULL) {
        CHECK(((uintptr_t)p % 16u) == 0u, "off: alignment honoured");
        CHECK(cplus_safe_ptr_size(p) == 64, "off: metadata size");
    }
    free_s(p);

    /* zero-size -> tracked 1-byte block. */
    p = aligned_alloc_s(16, 0);
    CHECK(p != NULL, "zero-size: aligned block");
    if (p != NULL) CHECK(cplus_safe_ptr_size(p) == 1, "zero-size: metadata size");
    free_s(p);

    /* exact-fit. */
    p = aligned_alloc_s(32, 96);
    CHECK(p != NULL, "exact-fit: aligned block");
    if (p != NULL) {
        CHECK(((uintptr_t)p % 32u) == 0u, "exact-fit: alignment honoured");
        CHECK(cplus_safe_ptr_size(p) == 96, "exact-fit: metadata size");
    }
    free_s(p);

    /* bad alignment and overflow size (SIZE_MAX) are refused with EINVAL. */
    errno = 0;
    p = aligned_alloc_s(3, 64);
    CHECK(p == NULL, "bad alignment: NULL");
    CHECK(errno == EINVAL, "bad alignment: EINVAL");
    errno = 0;
    p = aligned_alloc_s(16, SIZE_MAX);
    CHECK(p == NULL, "SIZE_MAX: NULL");
    CHECK(errno == EINVAL, "SIZE_MAX: EINVAL");

    /* fail_at=1 / fail_after=0 via the SafeCRT injection hook. */
    inject(&g_safecrt, 0);
    errno = 0;
    p = aligned_alloc_s(16, 64);
    CHECK(p == NULL, "injected: NULL");
    CHECK(errno == ENOMEM, "injected: ENOMEM");
    CHECK(seq_now(&g_safecrt) == 1UL, "injected: attempt sequence");
    p = aligned_alloc_s(16, 64);
    CHECK(p == NULL, "injected: latching failure");
    cplus_safe_alloc_fail_reset();
    p = aligned_alloc_s(16, 64);
    CHECK(p != NULL, "injected: recovery after reset");
    free_s(p);

    CHECK(cplus_safe_alloc_live_count() == live0, "aligned: no leaked blocks");
    parity_aligned();
}

/* Prove the log toggle used by the parity helpers is not vacuous: capture fd 2
 * into a temporary file and assert that ON emits both log prefixes while OFF
 * emits nothing for the very same calls. */
static void test_logging_emission(void) {
    char line[512];
    char buf[4096];
    size_t used = 0;
    FILE *tf;
    int saved, on_libc, on_safe, off_total;

    g_op = "logging toggle";
    tf = tmpfile();
    if (tf == NULL) {
        CHECK(0, "tmpfile() for log capture failed");
        return;
    }
    saved = dup(STDERR_FILENO);
    (void)dup2(fileno(tf), STDERR_FILENO);

    set_logging(1);
    errno = 0; (void)cplus_malloc(SIZE_MAX);
    errno = 0; (void)malloc_s(SIZE_MAX);
    fflush(stderr);
    set_logging(0);

    (void)dup2(saved, STDERR_FILENO);
    (void)close(saved);
    rewind(tf);
    buf[0] = '\0';
    while (used + 1u < sizeof(buf) && fgets(line, (int)sizeof(line), tf) != NULL) {
        size_t n = strlen(line);
        if (n > sizeof(buf) - 1u - used) n = sizeof(buf) - 1u - used;
        memcpy(buf + used, line, n);
        used += n;
        buf[used] = '\0';
    }
    fclose(tf);
    on_libc = strstr(buf, "[CPLUS-LIBC]") != NULL;
    on_safe = strstr(buf, "[SAFECRT]") != NULL;
    CHECK(on_libc, "logging ON: hardened libc emitted a log line");
    CHECK(on_safe, "logging ON: SafeCRT emitted a log line");

    /* Same calls with logging OFF must produce no output at all. */
    tf = tmpfile();
    if (tf == NULL) {
        CHECK(0, "tmpfile() for silent-log capture failed");
        return;
    }
    saved = dup(STDERR_FILENO);
    (void)dup2(fileno(tf), STDERR_FILENO);
    set_logging(0);
    errno = 0; (void)cplus_malloc(SIZE_MAX);
    errno = 0; (void)malloc_s(SIZE_MAX);
    fflush(stderr);
    (void)dup2(saved, STDERR_FILENO);
    (void)close(saved);
    rewind(tf);
    off_total = (fgets(line, (int)sizeof(line), tf) != NULL);
    fclose(tf);
    CHECK(off_total == 0, "logging OFF: no log line emitted");
}

/* ---- entry point --------------------------------------------------------- */

int main(void) {
    test_malloc_like(&g_hardened, "cplus_malloc");
    test_malloc_like(&g_safecrt, "malloc_s");
    test_calloc_like(&g_hardened, "cplus_calloc");
    test_calloc_like(&g_safecrt, "calloc_s");
    test_realloc_like(&g_hardened, "cplus_realloc");
    test_realloc_like(&g_safecrt, "realloc_s");
    test_plain_free(&g_hardened, "cplus_free");
    test_plain_free(&g_safecrt, "free_s");
    test_checked_free_hardened();
    test_checked_free_safecrt();
    test_aligned_alloc_s();
    test_logging_emission();

    set_logging(0);
    printf("alloc_fault_matrix: %d/%d checks ok\n", g_checks - g_fail, g_checks);
    return g_fail != 0;
}
