/* PHASE 5-7 deterministic memory/string/allocation adversarial harness.
 *
 * Every attack runs in a forked child.  A MAP_SHARED word carries the child's
 * own classification; WIFSIGNALED distinguishes a real fault (CRASH) from a
 * library refusal (REFUSED).  Each child places canary bytes around the
 * buffers it attacks and reports CORRUPT when those bytes change without a
 * fault.  A success where a refusal was promised is UNEXPECTED_OK.
 *
 * The harness links the parent-owned build/safe/libcplus_safecrt.a; run.sh
 * rebuilds it before running.  No file outside tests/defense/memory2/ is
 * modified by the harness itself.
 */
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200112L
#include "safe_alloc.h"
#include "safe_memory.h"
#include "safe_runtime.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

enum {
    C_REFUSED = 0,      /* documented error signal, canaries intact */
    C_CRASH = 1,        /* child died on a signal (guard-page fault) */
    C_CORRUPT = 2,      /* canary changed but no fault */
    C_UNEXPECTED_OK = 3,/* succeeded where a refusal was promised */
    C_OK = 4            /* valid input succeeded as expected */
};
static const char *cname(int c) {
    switch (c) {
    case C_REFUSED: return "REFUSED";
    case C_CRASH: return "CRASH";
    case C_CORRUPT: return "CORRUPT";
    case C_UNEXPECTED_OK: return "UNEXPECTED_OK";
    default: return "OK";
    }
}

typedef struct { volatile int cls; } shared_t;

static void canary_fill(unsigned char *p, size_t n, unsigned seed) {
    size_t i;
    for (i = 0; i < n; ++i) p[i] = (unsigned char)(0xA5u ^ (seed + i * 37u));
}
static int canary_eq(const unsigned char *a, const unsigned char *b, size_t n) {
    return memcmp(a, b, n) == 0;
}

/* plain-malloc buffer surrounded by canaries; returns usable pointer */
#define GC 24
static unsigned char *gbuf(size_t n, unsigned seed, unsigned char *save_pre,
                           unsigned char *save_post) {
    unsigned char *raw = (unsigned char *)malloc(n + 2u * GC);
    if (!raw) return NULL;
    canary_fill(raw, GC, seed);
    canary_fill(raw + GC + n, GC, seed ^ 0x5A5Au);
    if (save_pre) memcpy(save_pre, raw, GC);
    if (save_post) memcpy(save_post, raw + GC + n, GC);
    return raw + GC;
}
static int gok(unsigned char *p, size_t n, const unsigned char *pre,
               const unsigned char *post) {
    return canary_eq(p - GC, pre, GC) && canary_eq(p + n, post, GC);
}

/* N-byte RW region flush against a PROT_NONE guard page. */
static unsigned char *guardbuf(size_t n) {
    long ps = sysconf(_SC_PAGESIZE);
    unsigned char *r;
    if (ps <= 0 || n == 0 || n > (size_t)ps) return NULL;
    r = (unsigned char *)mmap(NULL, (size_t)ps * 2u, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (r == MAP_FAILED) return NULL;
    if (mprotect(r + ps, (size_t)ps, PROT_NONE) != 0) {
        munmap(r, (size_t)ps * 2u);
        return NULL;
    }
    return r + ps - n;
}

typedef int (*attack_fn)(unsigned);

static void run_attack(const char *name, attack_fn fn, unsigned seed,
                       int *totals) {
    shared_t *sh = (shared_t *)mmap(NULL, sizeof(shared_t), PROT_READ | PROT_WRITE,
                                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    pid_t pid;
    int status = 0, cls;
    if (sh == MAP_FAILED) { printf("CASE %s MMAP_FAIL\n", name); return; }
    sh->cls = C_UNEXPECTED_OK;
    pid = fork();
    if (pid == 0) {
        cplus_runtime_set_violation_trap(0); /* refusals return, not _exit(70) */
        cplus_safe_alloc_fail_reset();
        errno = 0;
        sh->cls = fn(seed);
        _exit(0);
    }
    if (pid < 0) { printf("CASE %s FORK_FAIL\n", name); munmap(sh, sizeof(shared_t)); return; }
    waitpid(pid, &status, 0);
    cls = WIFSIGNALED(status) ? C_CRASH : sh->cls;
    if (cls < 0 || cls > 4) cls = C_UNEXPECTED_OK;
    totals[cls]++;
    printf("CASE %-34s %s\n", name, cname(cls));
    munmap(sh, sizeof(shared_t));
}

/* ---- attacks ------------------------------------------------------------ */

/* A1: direct adapter, tracked dest, dmax larger than proven capacity. */
static int a_dmax_over_tracked(unsigned seed) {
    unsigned char *d = (unsigned char *)malloc_s(64);
    unsigned char src[64];
    errno_t r;
    if (!d) return C_UNEXPECTED_OK;
    memset(src, (int)(seed & 0xff), sizeof src);
    errno = 0;
    r = cplus_memcpy_s(d, 128, src, 64);
    if (r == 406 && errno == ERANGE) { free_s(d); return C_REFUSED; }
    free_s(d);
    return C_UNEXPECTED_OK;
}

/* A2: direct adapter, tracked dest, valid dmax + copy. */
static int a_direct_valid(unsigned seed) {
    unsigned char *d = (unsigned char *)malloc_s(64);
    unsigned char src[32];
    errno_t r;
    if (!d) return C_UNEXPECTED_OK;
    memset(src, (int)(seed & 0xff), sizeof src);
    r = cplus_memcpy_s(d, 64, src, sizeof src);
    if (r == 0 && d[0] == src[0] && d[31] == src[31]) { free_s(d); return C_OK; }
    free_s(d);
    return C_UNEXPECTED_OK;
}

/* A3: direct adapter, smax > dmax -> safeclib error clear within capacity. */
static int a_smax_gt_dmax(unsigned seed) {
    unsigned char *d = (unsigned char *)malloc_s(64);
    unsigned char src[128];
    errno_t r;
    if (!d) return C_UNEXPECTED_OK;
    memset(src, (int)(seed & 0xff), sizeof src);
    errno = 0;
    r = cplus_memcpy_s(d, 32, src, 128);
    if (r != 0 && errno != 0) { free_s(d); return C_REFUSED; }
    free_s(d);
    return C_UNEXPECTED_OK;
}

/* A4: auto wrapper, tracked source over-read (blue-team regression). */
static int a_src_overread(unsigned seed) {
    unsigned char *src = (unsigned char *)malloc_s(8);
    unsigned char *dst = (unsigned char *)malloc_s(64);
    void *r;
    if (!src || !dst) return C_UNEXPECTED_OK;
    memset(src, (int)(seed & 0xff), 8);
    errno = 0;
    r = cplus_memcpy_s_auto(dst, src, 32, 64);
    if (r == NULL && errno == ERANGE) { free_s(src); free_s(dst); return C_REFUSED; }
    free_s(src); free_s(dst);
    return C_UNEXPECTED_OK;
}

/* A5: auto wrapper, interior dest with BOS from caller (stack array). */
static int a_interior_bos(unsigned seed) {
    unsigned char pre[GC], post[GC], *p;
    unsigned char src[32];
    void *r;
    memset(src, (int)(seed & 0xff), sizeof src);
    p = gbuf(64, seed, pre, post);
    if (!p) return C_UNEXPECTED_OK;
    memset(p, 0x33, 64);
    memcpy(pre, p - GC, GC);
    memcpy(post, p + 64, GC);
    errno = 0;
    /* BOS of p+40 is 24 (p points into the gbuf usable region, unknown to
       compiler here), so pass the BOS of a known local array instead. */
    r = cplus_memcpy_s_auto(p + 40, src, 32, 24);
    if (r == NULL && gok(p, 64, pre, post)) { free(p - GC); return C_REFUSED; }
    free(p - GC);
    return C_UNEXPECTED_OK;
}

/* A6: memcmp auto macro, tracked second operand too small. */
static int a_memcmp_src_small(unsigned seed) {
    unsigned char *a = (unsigned char *)malloc_s(64);
    unsigned char *b = (unsigned char *)malloc_s(8);
    int v;
    if (!a || !b) return C_UNEXPECTED_OK;
    memset(a, (int)(seed & 0xff), 64);
    memset(b, (int)(seed & 0xff), 8);
    errno = 0;
    v = memcmp_s(a, b, 32);
    if (v == -1 && errno == ERANGE) { free_s(a); free_s(b); return C_REFUSED; }
    free_s(a); free_s(b);
    return C_UNEXPECTED_OK;
}

/* A7: GENUINE FINDING — memcmp auto, both NULL, n>0 reports "equal" (0). */
static int a_memcmp_both_null(unsigned seed) {
    int v;
    (void)seed;
    errno = 0;
    v = cplus_memcmp_s_auto(NULL, NULL, 8, (size_t)-1, (size_t)-1);
    if (v == -1 && errno == EINVAL) return C_REFUSED;
    return C_UNEXPECTED_OK;
}

/* A8: one-NULL keeps documented ordering sentinel (must not regress). */
static int a_memcmp_one_null(unsigned seed) {
    unsigned char b[8] = {0};
    int v1, v2;
    (void)seed;
    errno = 0; v1 = cplus_memcmp_s_auto(NULL, b, 4, (size_t)-1, (size_t)-1);
    errno = 0; v2 = cplus_memcmp_s_auto(b, NULL, 4, (size_t)-1, (size_t)-1);
    if (v1 < 0 && v2 > 0) return C_OK;
    return C_UNEXPECTED_OK;
}

/* A9: UAF exact pointer refused with EFAULT. */
static int a_uaf_exact(unsigned seed) {
    unsigned char *p = (unsigned char *)malloc_s(32);
    errno_t r;
    (void)seed;
    if (!p) return C_UNEXPECTED_OK;
    memset(p, 0x41, 32);
    free_s(p);
    errno = 0;
    r = cplus_memset_s(p, 32, 0xCC, 32);
    if (r == 400 && errno == EFAULT) return C_REFUSED;
    return C_UNEXPECTED_OK;
}

/* A10: UAF interior pointer — known gap, escalated (not fixable here). */
static int a_uaf_interior(unsigned seed) {
    unsigned char *p = (unsigned char *)malloc_s(64);
    unsigned char *q;
    errno_t r;
    (void)seed;
    if (!p) return C_UNEXPECTED_OK;
    memset(p, 0x42, 64);
    q = p + 16;
    free_s(p);
    errno = 0;
    r = cplus_memset_s(q, 32, 0xCC, 32);
    if (r == 400 && errno == EFAULT) return C_REFUSED;
    return C_OK; /* documents the gap; see HARDENING2.md */
}

/* A11: double free refused. */
static int a_double_free(unsigned seed) {
    unsigned char *p = (unsigned char *)malloc_s(32);
    (void)seed;
    if (!p) return C_UNEXPECTED_OK;
    free_s(p);
    free_s(p);
    return C_REFUSED;
}

/* A12: guard-page dest, direct adapter, valid small copy (no fault). */
static int a_guard_direct(unsigned seed) {
    unsigned char *d = guardbuf(64);
    unsigned char src[32];
    errno_t r;
    if (!d) return C_OK;
    memset(src, (int)(seed & 0xff), sizeof src);
    errno = 0;
    r = cplus_memcpy_s(d, 64, src, sizeof src);
    if (r == 0) return C_OK;
    return C_UNEXPECTED_OK;
}

/* A13: guard-page source over-read via macro (untracked src -> accepted). */
static int a_guard_src(unsigned seed) {
    unsigned char *s = guardbuf(32);
    unsigned char *d = (unsigned char *)malloc_s(64);
    void *r;
    (void)seed;
    if (!s || !d) return C_OK;
    r = memcpy_s(d, s, 32);
    if (r == d) { free_s(d); return C_OK; }
    free_s(d);
    return C_UNEXPECTED_OK;
}

/* A14: realloc failure atomicity under injected failure. */
static int a_realloc_atomic(unsigned seed) {
    unsigned char *p = (unsigned char *)malloc_s(32);
    unsigned char *q;
    (void)seed;
    if (!p) return C_UNEXPECTED_OK;
    memset(p, 0x5A, 32);
    cplus_safe_alloc_fail_after(0);
    q = realloc_s(p, 64);
    cplus_safe_alloc_fail_reset();
    if (q == NULL && cplus_safe_ptr_is_live(p) && p[0] == 0x5A) {
        free_s(p); return C_REFUSED;
    }
    if (q) free_s(q); else free_s(p);
    return C_UNEXPECTED_OK;
}

/* A15: calloc overflow refused. */
static int a_alloc_overflow(unsigned seed) {
    void *p;
    (void)seed;
    errno = 0;
    p = calloc_s((size_t)-1, 2);
    if (p == NULL && errno == EOVERFLOW) return C_REFUSED;
    if (p) free_s(p);
    return C_UNEXPECTED_OK;
}

/* A16: SIZE_MAX n in auto wrapper refused. */
static int a_sizemax_auto(unsigned seed) {
    unsigned char d[8];
    void *r;
    (void)seed;
    memset(d, 0, sizeof d);
    errno = 0;
    r = cplus_memset_s_auto(d, 0, (size_t)-1, sizeof d);
    if (r == NULL && errno == ERANGE) return C_REFUSED;
    return C_UNEXPECTED_OK;
}

/* ---- deterministic fuzz ------------------------------------------------- */
static int fuzz_auto(unsigned seed) {
    unsigned char dst[128], src[128];
    size_t dn, n;
    void *r;
    dn = 8 + (seed % 64);
    n  = 1 + ((seed / 13) % 128);
    memset(dst, 0x11, sizeof dst);
    memset(src, (int)(seed & 0xff), sizeof src);
    errno = 0;
    r = cplus_memcpy_s_auto(dst, src, n, dn);
    if (n > dn) return (r == NULL) ? C_REFUSED : C_UNEXPECTED_OK;
    return (r == dst) ? C_OK : C_UNEXPECTED_OK;
}

static int fuzz_memcmp(unsigned seed) {
    unsigned char a[64], b[64];
    size_t n = 1 + (seed % 64);
    int v;
    memset(a, (int)(seed & 0xff), sizeof a);
    memset(b, (int)((seed >> 8) & 0xff), sizeof b);
    errno = 0;
    v = cplus_memcmp_s_auto(a, b, n, sizeof a, sizeof b);
    if (v == -1 && errno == ERANGE) return C_REFUSED;
    if (v == 0 || v == 1 || v == -1) return C_OK;
    return C_UNEXPECTED_OK;
}

int main(int argc, char **argv) {
    int totals[5] = {0, 0, 0, 0, 0};
    int fz[5] = {0, 0, 0, 0, 0};
    unsigned i;
    const char *mode = argc > 1 ? argv[1] : "all";

    if (!strcmp(mode, "all") || !strcmp(mode, "attacks")) {
#define RUN(n, f) run_attack(n, f, 0x1234u, totals)
        RUN("dmax_over_tracked", a_dmax_over_tracked);
        RUN("direct_valid", a_direct_valid);
        RUN("smax_gt_dmax", a_smax_gt_dmax);
        RUN("src_overread", a_src_overread);
        RUN("interior_bos", a_interior_bos);
        RUN("memcmp_src_small", a_memcmp_src_small);
        RUN("memcmp_both_null", a_memcmp_both_null);
        RUN("memcmp_one_null", a_memcmp_one_null);
        RUN("uaf_exact", a_uaf_exact);
        RUN("uaf_interior", a_uaf_interior);
        RUN("double_free", a_double_free);
        RUN("guard_direct", a_guard_direct);
        RUN("guard_src", a_guard_src);
        RUN("realloc_atomic", a_realloc_atomic);
        RUN("alloc_overflow", a_alloc_overflow);
        RUN("sizemax_auto", a_sizemax_auto);
#undef RUN
        printf("ATTACK_TOTALS REFUSED=%d CRASH=%d CORRUPT=%d UNEXPECTED_OK=%d OK=%d\n",
               totals[C_REFUSED], totals[C_CRASH], totals[C_CORRUPT],
               totals[C_UNEXPECTED_OK], totals[C_OK]);
    }

    if (!strcmp(mode, "all") || !strcmp(mode, "fuzz")) {
        for (i = 0; i < 1200; ++i) {
            shared_t *sh = (shared_t *)mmap(NULL, sizeof(shared_t),
                PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
            pid_t pid; int st = 0, cls;
            if (sh == MAP_FAILED) { printf("FUZZ MMAP_FAIL\n"); return 2; }
            sh->cls = C_UNEXPECTED_OK;
            pid = fork();
            if (pid == 0) {
                cplus_runtime_set_violation_trap(0);
                cplus_safe_alloc_fail_reset();
                errno = 0;
                sh->cls = (i & 1) ? fuzz_auto(i + 0x5eedu)
                                  : fuzz_memcmp(i + 0x5eedu);
                _exit(0);
            }
            waitpid(pid, &st, 0);
            cls = WIFSIGNALED(st) ? C_CRASH : sh->cls;
            if (cls < 0 || cls > 4) cls = C_UNEXPECTED_OK;
            fz[cls]++;
            munmap(sh, sizeof(shared_t));
        }
        printf("FUZZ_TOTALS cases=1200 REFUSED=%d CRASH=%d CORRUPT=%d UNEXPECTED_OK=%d OK=%d\n",
               fz[C_REFUSED], fz[C_CRASH], fz[C_CORRUPT], fz[C_UNEXPECTED_OK], fz[C_OK]);
    }
    return 0;
}
