/* Deterministic harness shared by the F-021..F-040 allocation/lifecycle
 * fuzz configurations.  Header-only: every config is a standalone C program
 * linked only against the existing SafeCRT / hardened-libc archives.
 *
 * Design rules enforced here:
 *   - fixed-seed LCG (no time, no randomness, no stdin);
 *   - >= F100_OPS operations per configuration;
 *   - side-table queries only for freed/stale pointers (never dereference);
 *   - canary pages are created with mmap/mprotect exclusively inside forked
 *     children; the parent only waits and interprets status.
 */
#ifndef F100_ALLOC_HARNESS_H
#define F100_ALLOC_HARNESS_H

#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

#define F100_OPS 1200u
#define F100_MAX_ALLOC 4096u
#define F100_SEED 0xC0FFEE123456789ULL
#define F100_RSIZE_MAX ((size_t)(SIZE_MAX >> 1))

/* Numerical Recipes LCG: deterministic across all supported hosts. */
typedef struct {
    uint64_t state;
} f100_lcg;

static void f100_seed(f100_lcg *r, uint64_t seed) {
    r->state = seed ? seed : 0x9E3779B97F4A7C15ULL;
}

static uint32_t f100_next(f100_lcg *r) {
    r->state = r->state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (uint32_t)(r->state >> 32);
}

/* Every size lies in the closed range 0..4096. */
static size_t f100_size(f100_lcg *r) {
    return (size_t)(f100_next(r) % (uint32_t)(F100_MAX_ALLOC + 1u));
}

typedef struct {
    const char *tag;
    unsigned long checks;
    unsigned long fails;
} f100_ctx;

static void f100_begin(f100_ctx *c, const char *tag) {
    c->tag = tag;
    c->checks = 0u;
    c->fails = 0u;
}

static void f100_check(f100_ctx *c, const char *name, int cond) {
    c->checks++;
    if (!cond) {
        c->fails++;
        printf("FAIL %s %s\n", c->tag, name);
    }
}

static int f100_end(const f100_ctx *c) {
    printf("F100 %s CHECKS=%lu FAILS=%lu\n", c->tag, c->checks, c->fails);
    return c->fails ? 1 : 0;
}

/* Values used to probe count*size products at and beyond the overflow edge.
 * All expressions are representable; the multiplication under test is done by
 * the API being fuzzed, never by this harness. */
static const size_t f100_overflow_products[] = {
    (size_t)0,
    (size_t)1,
    (size_t)4096,
    SIZE_MAX,
    SIZE_MAX - 1u,
    SIZE_MAX / 2u,
    (SIZE_MAX / 2u) + 1u,
    F100_RSIZE_MAX,
    F100_RSIZE_MAX + 1u
};
#define F100_OVERFLOW_N \
    (sizeof(f100_overflow_products) / sizeof(f100_overflow_products[0]))

/* Canary-page helper.  The callback allocates `n` bytes through the API under
 * test, fills them with 0xA5 and copies back exactly the *reported* tracked
 * size into dst.  dst is the first of two mmap pages whose second page is
 * PROT_NONE, so a size larger than n is detected as SIGSEGV in the child. */
typedef int (*f100_copyback_fn)(unsigned char *dst, size_t n);

static int f100_canary_check(const char *tag, f100_copyback_fn fill) {
    long psl = sysconf(_SC_PAGESIZE);
    size_t ps = psl > 0 ? (size_t)psl : 4096u;
    pid_t integrity, guard;
    int si = 0, sg = 0, integrity_ok, guard_ok, ok;

    integrity = fork();
    if (integrity == 0) {
        unsigned char *r = (unsigned char *)mmap(
            NULL, ps * 2u, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        size_t i;
        int rc;
        if (r == MAP_FAILED) _exit(90);
        if (mprotect(r + ps, ps, PROT_NONE) != 0) _exit(91);
        memset(r, 0x00, ps);
        rc = fill(r, ps);
        for (i = 0; i < ps; ++i) {
            if (r[i] != 0xA5) { rc = 1; break; }
        }
        (void)munmap(r, ps * 2u);
        _exit(rc == 0 ? 0 : 92);
    }
    if (integrity < 0) return 1;

    guard = fork();
    if (guard == 0) {
        unsigned char *r = (unsigned char *)mmap(
            NULL, ps * 2u, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        volatile unsigned char *gp;
        if (r == MAP_FAILED) _exit(90);
        if (mprotect(r + ps, ps, PROT_NONE) != 0) _exit(91);
        /* UBSan installs fatal-signal handlers; restore the defaults so the
         * guard-page fault is observed as a real signal by the parent. */
        (void)signal(SIGSEGV, SIG_DFL);
        (void)signal(SIGBUS, SIG_DFL);
        gp = (volatile unsigned char *)(r + ps);
        *gp = 0x5A; /* deliberate one-byte overflow into the PROT_NONE page */
        _exit(93);  /* reached only if the guard page is ineffective */
    }
    if (guard < 0) { (void)waitpid(integrity, &si, 0); return 1; }

    (void)waitpid(integrity, &si, 0);
    (void)waitpid(guard, &sg, 0);
    integrity_ok = WIFEXITED(si) && WEXITSTATUS(si) == 0;
    /* macOS/arm64 reports a write into a PROT_NONE page as SIGBUS, x86_64
     * commonly as SIGSEGV; both prove the guard page is effective. */
    guard_ok = WIFSIGNALED(sg) &&
               (WTERMSIG(sg) == SIGSEGV || WTERMSIG(sg) == SIGBUS);
    ok = integrity_ok && guard_ok;
    printf("F100 %s CANARY_INTEGRITY=%d GUARD_DETECTED=%d OK=%d\n",
           tag, integrity_ok, guard_ok, ok);
    return ok ? 0 : 1;
}

#endif /* F100_ALLOC_HARNESS_H */
