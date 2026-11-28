/* fuzz10k/alloc: deterministic, forked fuzzing of the checked allocation APIs.
 *
 * Scope (SafeCRT archive, build/safe/libcplus_safecrt.a):
 *   malloc_s, calloc_s, realloc_s, free_s, free_checked_s, aligned_alloc_s,
 *   cplus_safe_ptr_is_live, cplus_safe_ptr_size, cplus_safe_ptr_was_freed,
 *   cplus_safe_alloc_get_meta, cplus_safe_ptr_check_generation,
 *   cplus_safe_alloc_live_count/bytes, cplus_safe_alloc_seq,
 *   cplus_safe_alloc_fail_after/reset.
 * Scope (hardened archive, build/libc/libcplus_hardened.a):
 *   cplus_malloc, cplus_free_checked, cplus_alloc_is_live, cplus_alloc_size,
 *   cplus_alloc_live_count, cplus_alloc_double_free_count,
 *   cplus_alloc_fail_after/reset.
 *
 * Every scenario runs in a forked child (alarm(3) + a 5s parent watchdog) so a
 * fatal memory error or a hang cannot take down the harness. The parent records
 * one deterministic line per scenario and a summary. No fabricated passes:
 * only real child exit statuses are counted.
 *
 * Build/run: see run.sh in this directory.
 */
#define _POSIX_C_SOURCE 200809L
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1 /* exposes MAP_ANON on macOS */
#endif

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "cplus_libc.h"
#include "stdlib/safe/safe_alloc.h"
#include "stdlib/safe/safe_error.h"

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

#define RSIZE ((size_t)(SIZE_MAX >> 1))
#define NO_INJECT UINT64_C(0xFF)
#define BASE_SEED UINT64_C(0x46555A5A31304B30) /* "FUZZ10K0" */
#define TARGET_DEFAULT 10000L
#define HT_CAP 32768u
#define LINE_CAP 768u

/* ------------------------------- PRNG ------------------------------------ */
/* splitmix64: fixed, platform-independent, deterministic. */

static uint64_t splitmix64(uint64_t *x) {
    uint64_t z = (*x += UINT64_C(0x9E3779B97F4A7C15));
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}

/* ----------------------------- Scenario model ---------------------------- */

enum {
    OP_MALLOC,
    OP_CALLOC,
    OP_RGROW,
    OP_RSHRINK,
    OP_ALIGNED,
    OP_FREE_DOUBLE,
    OP_FREE_INVALID,
    OP_ZERO,
    OP_INJ_SAFE,
    OP_INJ_HARD,
    OP_OVERFLOW,
    OP_RINVALID,
    OP_COUNT
};

static const char *const OP_NAMES[OP_COUNT] = {
    "MALLOC", "CALLOC", "RGROW", "RSHRINK", "ALIGNED", "FREE_DOUBLE",
    "FREE_INVALID", "ZERO", "INJ_SAFECRT", "INJ_HARDENED", "OVERFLOW",
    "REALLOC_INVALID"
};

typedef struct {
    uint8_t op;
    uint8_t variant; /* 0 = uniform random, 1 = overflow boundary, 2 = special */
    uint64_t size;
    uint64_t count;
    uint64_t size2;
    uint64_t align;
    uint64_t inject; /* 0..8 = N successes then latch-fail; NO_INJECT = off */
    uint64_t tag;
} Params;

static size_t pick_size(uint64_t r, uint8_t variant) {
    static const size_t specials[] = {0, 1, 2, 7, 8, 15, 16, 31, 32, 63, 64,
                                      255, 256, 4095, 4096, 32767, 32768,
                                      65535, 65536};
    static const size_t ovs[] = {RSIZE + 1, SIZE_MAX - 1, SIZE_MAX,
                                 SIZE_MAX / 2 + 1};
    if (variant == 1) return ovs[r % 4];
    if (variant == 2) return specials[r % (sizeof specials / sizeof specials[0])];
    return (size_t)(r % 65537u);
}

static size_t pick_count(uint64_t r, uint8_t variant) {
    static const size_t ovs[] = {SIZE_MAX, SIZE_MAX - 1, RSIZE, RSIZE + 1,
                                 SIZE_MAX / 2 + 1, 4097};
    if (variant == 1) return ovs[r % 6];
    return (size_t)(r % 4097u);
}

static size_t pick_align(uint64_t r, uint8_t variant) {
    static const size_t specials[] = {0, 1, 2, 3, 4, 5, 7, 8, 16, 32, 64,
                                      128, 256};
    if (variant == 2)
        return specials[r % (sizeof specials / sizeof specials[0])];
    return (size_t)(1u + (r % 256u));
}

static Params gen_params(uint64_t index, uint64_t attempt) {
    uint64_t s = BASE_SEED ^ (index * UINT64_C(0x9E3779B97F4A7C15)) ^
                 (attempt * UINT64_C(0xD1B54A32D192ED03));
    uint64_t r1 = splitmix64(&s), r2 = splitmix64(&s), r3 = splitmix64(&s);
    uint64_t r4 = splitmix64(&s), r5 = splitmix64(&s);
    Params p;

    memset(&p, 0, sizeof p);
    p.op = (uint8_t)(index % OP_COUNT);
    p.variant = (uint8_t)(((r1 >> 7) % 16u == 0u) ? 1u
                          : (((r1 >> 7) % 16u == 1u) ? 2u : 0u));
    p.size = (uint64_t)pick_size(r2, p.variant);
    p.count = (uint64_t)pick_count(r3, p.variant);
    p.align = (uint64_t)pick_align(r4, p.variant);
    p.inject = ((r5 >> 3) % 4u == 0u) ? (r5 % 9u) : NO_INJECT;
    p.size2 = splitmix64(&s) % 65537u;
    p.tag = splitmix64(&s);

    if (p.op == OP_RGROW || p.op == OP_RSHRINK) {
        size_t eff0;
        p.size = r2 % 65537u; /* keep the base allocation small and safe */
        eff0 = p.size ? (size_t)p.size : 1u;
        if (p.variant == 1) {
            p.size2 = SIZE_MAX; /* overflow-boundary realloc target */
        } else if (p.op == OP_RGROW) {
            p.size2 = eff0 >= 65536u ? 65536u
                                    : (uint64_t)eff0 + 1u +
                                          (p.size2 % (65537u - (uint64_t)eff0));
        } else {
            p.size2 = eff0 > 1u ? p.size2 % (uint64_t)eff0 : 0u;
        }
    }
    return p;
}

static uint64_t hash_params(const Params *p) {
    const unsigned char *b = (const unsigned char *)p;
    uint64_t h = UINT64_C(1469598103934665603);
    size_t i;
    for (i = 0; i < sizeof *p; ++i) {
        h ^= b[i];
        h *= UINT64_C(1099511628211);
    }
    return h & (HT_CAP - 1u);
}

/* 1 = newly inserted (distinct), 0 = duplicate, -1 = table exhausted. */
static int insert_unique(Params *tab, unsigned char *used, const Params *p) {
    uint64_t start = hash_params(p), k;
    for (k = 0; k < HT_CAP; ++k) {
        uint64_t j = (start + k) & (HT_CAP - 1u);
        if (!used[j]) {
            used[j] = 1;
            tab[j] = *p;
            return 1;
        }
        if (memcmp(&tab[j], p, sizeof *p) == 0) return 0;
    }
    return -1;
}

/* --------------------------- Child-side helpers -------------------------- */

enum { ST_PASS = 0, ST_CORRUPT = 1, ST_REFUSED = 2, ST_HANG_EXIT = 124 };

typedef struct {
    volatile int status; /* written last by the child */
    int refusals;
    char line[LINE_CAP];
} Report;

typedef struct {
    size_t safe_live, safe_bytes, hard_live;
} Base;

static uint64_t g_tag;

static unsigned char pat(size_t i) {
    return (unsigned char)((g_tag >> ((i & 7u) * 8u)) ^
                           (uint64_t)(i * 131u + 7u) ^ UINT64_C(0x5A));
}

static void fillpat(unsigned char *q, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) q[i] = pat(i);
}

static int checkpat(const unsigned char *q, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i)
        if (q[i] != pat(i)) return 0;
    return 1;
}

static int checkzero(const unsigned char *q, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i)
        if (q[i] != 0) return 0;
    return 1;
}

static size_t effsz(size_t n) { return n ? n : 1u; }

static int freed_clean(const void *q) {
    return cplus_safe_ptr_is_live(q) == 0 && cplus_safe_ptr_size(q) == 0 &&
           cplus_safe_ptr_was_freed(q) == 1;
}

static int restored(const Base *b) {
    return cplus_safe_alloc_live_count() == b->safe_live &&
           cplus_safe_alloc_live_bytes() == b->safe_bytes &&
           cplus_alloc_live_count() == b->hard_live;
}

#define CK(cond, msg)                                                          \
    do {                                                                       \
        if (!(cond)) {                                                         \
            snprintf(r->line, sizeof r->line, "%s|CORRUPT:%s", pre, (msg));    \
            r->status = ST_CORRUPT;                                            \
            return;                                                            \
        }                                                                      \
    } while (0)

static void set_ok(Report *r, const char *pre, const char *fmt, ...) {
    char detail[320];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(detail, sizeof detail, fmt, ap);
    va_end(ap);
    snprintf(r->line, sizeof r->line, "%s|OK:%s", pre, detail);
}

/* ------------------------------- Scenarios ------------------------------- */

static void op_malloc(const Params *p, const Base *b, const char *pre,
                      Report *r) {
    size_t n = (size_t)p->size;
    int inj = p->inject == NO_INJECT ? -1 : (int)p->inject;
    unsigned char *q;
    size_t want;
    int e;

    cplus_safe_alloc_fail_reset();
    if (inj >= 0) cplus_safe_alloc_fail_after(inj);
    errno = 0;
    q = (unsigned char *)malloc_s(n);
    e = errno;
    cplus_safe_alloc_fail_reset();

    if (n > RSIZE) {
        CK(q == NULL && e == EINVAL, "malloc_s oversize not refused EINVAL");
        r->refusals++;
        CK(restored(b), "malloc_s oversize metadata");
        set_ok(r, pre, "malloc_s(%" PRIuPTR ") refused EINVAL", n);
        return;
    }
    if (inj == 0) {
        CK(q == NULL && e == ENOMEM, "malloc_s injected failure not refused");
        r->refusals++;
        CK(restored(b), "malloc_s injection metadata");
        set_ok(r, pre, "malloc_s injected ENOMEM, metadata stable");
        return;
    }
    CK(q != NULL, "malloc_s returned NULL without cause");
    want = effsz(n);
    CK(cplus_safe_ptr_is_live(q) == 1, "malloc_s pointer not live");
    CK(cplus_safe_ptr_size(q) == want, "malloc_s metadata size wrong");
    fillpat(q, want);
    CK(checkpat(q, want), "malloc_s canary corrupted");
    free_s(q);
    CK(freed_clean(q), "malloc_s free metadata inconsistent");
    CK(restored(b), "malloc_s aggregate metadata not restored");
    set_ok(r, pre, "malloc_s tracked %" PRIuPTR " bytes, canary/free ok", want);
}

static void op_calloc(const Params *p, const Base *b, const char *pre,
                      Report *r) {
    size_t n = (size_t)p->count, sz = (size_t)p->size;
    int inj = p->inject == NO_INJECT ? -1 : (int)p->inject;
    unsigned char *q;
    int e, overflow;

    overflow = n != 0 && sz != 0 &&
               (n > SIZE_MAX / sz || n * sz > RSIZE);

    cplus_safe_alloc_fail_reset();
    if (inj >= 0) cplus_safe_alloc_fail_after(inj);
    errno = 0;
    q = (unsigned char *)calloc_s(n, sz);
    e = errno;
    cplus_safe_alloc_fail_reset();

    if (overflow) {
        CK(q == NULL && e == EOVERFLOW, "calloc_s overflow not refused");
        r->refusals++;
        CK(restored(b), "calloc_s overflow metadata");
        set_ok(r, pre, "calloc_s(%" PRIuPTR ",%" PRIuPTR ") refused EOVERFLOW",
               n, sz);
        return;
    }
    if (inj == 0) {
        CK(q == NULL && e == ENOMEM, "calloc_s injected failure not refused");
        r->refusals++;
        CK(restored(b), "calloc_s injection metadata");
        set_ok(r, pre, "calloc_s injected ENOMEM, metadata stable");
        return;
    }
    CK(q != NULL, "calloc_s returned NULL without cause");
    {
        /* Contract: if EITHER factor is zero, calloc_s allocates the defined
         * 1-byte (count=1, elem=1) block; it does not preserve the other
         * factor. See safe_alloc.h / safe_alloc.c: calloc_s. */
        size_t wn = (n == 0 || sz == 0) ? 1u : n;
        size_t ws = (n == 0 || sz == 0) ? 1u : sz;
        size_t total = wn * ws;
        CK(cplus_safe_ptr_is_live(q) == 1, "calloc_s pointer not live");
        CK(cplus_safe_ptr_size(q) == total, "calloc_s byte size wrong");
        CK(cplus_safe_ptr_elem_size(q) == ws, "calloc_s elem size wrong");
        CK(cplus_safe_ptr_count(q) == wn, "calloc_s element count wrong");
        CK(checkzero(q, total), "calloc_s bytes not zeroed");
        fillpat(q, total);
        CK(checkpat(q, total), "calloc_s canary corrupted");
        free_s(q);
        CK(freed_clean(q), "calloc_s free metadata inconsistent");
        CK(restored(b), "calloc_s aggregate metadata not restored");
        set_ok(r, pre, "calloc_s %" PRIuPTR "x%" PRIuPTR " tracked+z ok",
               wn, ws);
    }
}

static void op_realloc(const Params *p, const Base *b, const char *pre,
                       Report *r, int grow) {
    size_t n = (size_t)p->size, target = (size_t)p->size2;
    int inj = p->inject == NO_INJECT ? -1 : (int)p->inject;
    unsigned char *base, *q;
    size_t old;
    cplus_safe_alloc_meta m0, m1, m2;
    int e, refuse = 0;
    const char *why = "";

    cplus_safe_alloc_fail_reset();
    base = (unsigned char *)malloc_s(n);
    CK(base != NULL, "realloc base allocation failed");
    old = effsz(n);
    fillpat(base, old);
    CK(cplus_safe_alloc_get_meta(base, &m0) == CPLUS_SAFE_OK,
       "realloc base metadata missing");
    CK(m0.byte_size == old && m0.generation != 0, "realloc base metadata bad");

    if (inj >= 0) cplus_safe_alloc_fail_after(inj);
    errno = 0;
    q = (unsigned char *)realloc_s(base, target);
    e = errno;
    cplus_safe_alloc_fail_reset();

    if (target > RSIZE) {
        CK(q == NULL && e == EINVAL, "realloc oversize not refused EINVAL");
        refuse = 1;
        why = "size>RSIZE(EINVAL)";
    } else if (inj == 0) {
        CK(q == NULL && e == ENOMEM, "realloc injected failure not refused");
        refuse = 1;
        why = "injected(ENOMEM)";
    }

    if (refuse) {
        CK(cplus_safe_ptr_is_live(base) == 1, "owner not live after refusal");
        CK(cplus_safe_ptr_size(base) == old, "owner size changed on refusal");
        CK(cplus_safe_ptr_was_freed(base) == 0, "owner freed on refusal");
        CK(cplus_safe_alloc_get_meta(base, &m1) == CPLUS_SAFE_OK,
           "owner metadata missing after refusal");
        CK(m1.byte_size == m0.byte_size && m1.elem_size == m0.elem_size &&
               m1.generation == m0.generation,
           "owner metadata mutated on refusal");
        CK(checkpat(base, old), "owner canary corrupted on refusal");
        r->refusals++;
        free_s(base);
        CK(freed_clean(base), "owner free metadata after refusal");
        CK(restored(b), "realloc refusal aggregate metadata");
        set_ok(r, pre, "realloc refused %s; ptr+bytes+meta preserved", why);
        return;
    }

    CK(q != NULL, "realloc returned NULL without cause");
    {
        size_t want = effsz(target), keep = old < want ? old : want;
        CK(cplus_safe_ptr_is_live(q) == 1, "realloc result not live");
        CK(cplus_safe_ptr_size(q) == want, "realloc result size wrong");
        CK(checkpat(q, keep), "realloc content not preserved");
        CK(cplus_safe_alloc_get_meta(q, &m2) == CPLUS_SAFE_OK,
           "realloc result metadata missing");
        CK(m2.byte_size == want && m2.generation != m0.generation,
           "realloc result metadata/generation wrong");
        CK(cplus_safe_ptr_check_generation(q, m0.generation) == 0,
           "old generation still matches");
        CK(cplus_safe_ptr_check_generation(q, m2.generation) == 1,
           "new generation does not match");
        free_s(q);
        CK(freed_clean(q), "realloc free metadata inconsistent");
        CK(restored(b), "realloc aggregate metadata not restored");
        set_ok(r, pre, "realloc %s %" PRIuPTR "->%" PRIuPTR
                       " content+generation ok",
               grow ? "grow" : "shrink", old, want);
    }
}

static void op_aligned(const Params *p, const Base *b, const char *pre,
                       Report *r) {
    size_t a = (size_t)p->align, n = (size_t)p->size;
    int inj = p->inject == NO_INJECT ? -1 : (int)p->inject;
    unsigned char *q;
    int e, bad_align;

    bad_align = a < sizeof(void *) || (a & (a - 1u)) != 0;
    cplus_safe_alloc_fail_reset();
    if (inj >= 0) cplus_safe_alloc_fail_after(inj);
    errno = 0;
    q = (unsigned char *)aligned_alloc_s(a, n);
    e = errno;
    cplus_safe_alloc_fail_reset();

    if (bad_align || n > RSIZE) {
        CK(q == NULL && e == EINVAL, "aligned_alloc_s invalid args not refused");
        r->refusals++;
        CK(restored(b), "aligned invalid metadata");
        set_ok(r, pre, "aligned_alloc_s(a=%" PRIuPTR ",n=%" PRIuPTR
                       ") refused EINVAL", a, n);
        return;
    }
    if (inj == 0) {
        CK(q == NULL && e == ENOMEM, "aligned_alloc_s injection not refused");
        r->refusals++;
        CK(restored(b), "aligned injection metadata");
        set_ok(r, pre, "aligned_alloc_s injected ENOMEM, metadata stable");
        return;
    }
    CK(q != NULL, "aligned_alloc_s returned NULL without cause");
    CK(((uintptr_t)q % (uintptr_t)a) == 0u, "alignment not honoured");
    {
        size_t want = effsz(n);
        CK(cplus_safe_ptr_is_live(q) == 1, "aligned pointer not live");
        CK(cplus_safe_ptr_size(q) == want, "aligned metadata size wrong");
        fillpat(q, want);
        CK(checkpat(q, want), "aligned canary corrupted");
        free_s(q);
        CK(freed_clean(q), "aligned free metadata inconsistent");
        CK(restored(b), "aligned aggregate metadata not restored");
        set_ok(r, pre, "aligned_alloc_s a=%" PRIuPTR " size=%" PRIuPTR
                       " ok", a, want);
    }
}

static void op_free_double(const Params *p, const Base *b, const char *pre,
                           Report *r) {
    size_t n = 16u + (size_t)(p->tag % 1024u);
    unsigned char *q;
    int rc;

    (void)p;
    cplus_safe_alloc_fail_reset();
    q = (unsigned char *)malloc_s(n);
    CK(q != NULL, "double-free base allocation failed");
    fillpat(q, n);
    rc = free_checked_s(q);
    CK(rc == CPLUS_SAFE_OK, "first free_checked_s failed");
    CK(freed_clean(q), "first free metadata inconsistent");
    errno = 0;
    free_s(q);
    CK(errno == EINVAL, "free_s double free not refused");
    r->refusals++;
    errno = 0;
    rc = free_checked_s(q);
    CK(rc == CPLUS_SAFE_EINVAL && errno == EINVAL,
       "free_checked_s double free not refused");
    r->refusals++;
    CK(cplus_safe_ptr_is_live(q) == 0, "refused double free resurrected block");
    CK(restored(b), "double-free aggregate metadata");
    set_ok(r, pre, "double free refused by free_s and free_checked_s");
}

static void op_free_invalid(const Params *p, const Base *b, const char *pre,
                            Report *r) {
    unsigned char stack_obj[48];
    unsigned char *q;
    int rc;

    (void)p;
    fillpat(stack_obj, sizeof stack_obj);
    errno = 0;
    rc = free_checked_s(stack_obj);
    CK(rc == CPLUS_SAFE_EINVAL && errno == EINVAL,
       "stack pointer free not refused");
    r->refusals++;
    CK(checkpat(stack_obj, sizeof stack_obj), "stack object damaged by free");
    CK(cplus_safe_ptr_is_live(stack_obj) == 0, "stack pointer falsely live");

    errno = 0;
    q = (unsigned char *)realloc_s(stack_obj, 32);
    CK(q == NULL && errno == EINVAL, "stack pointer realloc not refused");
    r->refusals++;
    CK(checkpat(stack_obj, sizeof stack_obj), "stack object damaged by realloc");

    q = (unsigned char *)malloc_s(64);
    CK(q != NULL, "interior-free base allocation failed");
    fillpat(q, 64);
    errno = 0;
    rc = free_checked_s(q + 8);
    CK(rc == CPLUS_SAFE_EINVAL && errno == EINVAL,
       "interior pointer free not refused");
    r->refusals++;
    CK(cplus_safe_ptr_is_live(q) == 1, "interior free killed owner");
    CK(checkpat(q, 64), "interior free damaged owner bytes");
    free_s(q);
    CK(freed_clean(q), "interior-free owner free metadata");
    CK(restored(b), "invalid-free aggregate metadata");
    set_ok(r, pre, "stack/interior invalid free+realloc refused safely");
}

static void op_zero(const Params *p, const Base *b, const char *pre,
                    Report *r) {
    uint64_t a = p->align;
    unsigned char *q, *z;

    if (a < sizeof(void *) || (a & (a - 1u)) != 0) a = 16u;

    cplus_safe_alloc_fail_reset();
    q = (unsigned char *)malloc_s(0);
    CK(q != NULL && cplus_safe_ptr_size(q) == 1, "malloc_s(0) not 1-byte");
    free_s(q);
    CK(freed_clean(q), "malloc_s(0) free metadata");

    q = (unsigned char *)calloc_s(0, 8);
    CK(q != NULL && cplus_safe_ptr_size(q) == 1 &&
           cplus_safe_ptr_elem_size(q) == 1 && checkzero(q, 1),
       "calloc_s(0,8) wrong");
    free_s(q);

    q = (unsigned char *)calloc_s(8, 0);
    CK(q != NULL && cplus_safe_ptr_size(q) == 1 &&
           cplus_safe_ptr_elem_size(q) == 1 && checkzero(q, 1),
       "calloc_s(8,0) wrong");
    free_s(q);

    q = (unsigned char *)malloc_s(32);
    CK(q != NULL, "zero-realloc base failed");
    fillpat(q, 32);
    z = (unsigned char *)realloc_s(q, 0);
    CK(z != NULL && cplus_safe_ptr_size(z) == 1, "realloc_s(p,0) wrong");
    CK(checkpat(z, 1), "realloc_s(p,0) lost first byte");
    free_s(z);

    q = (unsigned char *)aligned_alloc_s((size_t)a, 0);
    CK(q != NULL && cplus_safe_ptr_size(q) == 1, "aligned_alloc_s(a,0) wrong");
    CK(((uintptr_t)q % (uintptr_t)a) == 0u, "aligned_alloc_s(a,0) alignment");
    free_s(q);

    CK(restored(b), "zero-size aggregate metadata");
    set_ok(r, pre, "zero-size malloc/calloc/realloc/aligned all 1-byte tracked");
}

static void op_inj_safe(const Params *p, const Base *b, const char *pre,
                        Report *r) {
    long n = (long)(p->inject == NO_INJECT ? (p->tag % 9u) : p->inject);
    void *arr[16];
    int nsucc = 0, i;
    void *q;

    cplus_safe_alloc_fail_reset();
    cplus_safe_alloc_fail_after(n);
    for (i = 0; i < 12; ++i) {
        int e;
        q = malloc_s(8);
        e = errno;
        if (i < n) {
            CK(q != NULL && cplus_safe_ptr_is_live(q) == 1,
               "safe latch: expected success before threshold");
            arr[nsucc++] = q;
        } else {
            CK(q == NULL && e == ENOMEM, "safe latch: expected ENOMEM");
            r->refusals++;
        }
    }
    CK(cplus_safe_alloc_seq() == 12UL, "safe latch: sequence counter wrong");
    cplus_safe_alloc_fail_reset();
    q = malloc_s(8);
    CK(q != NULL, "safe latch: no recovery after reset");
    arr[nsucc++] = q;
    for (i = 0; i < nsucc; ++i) free_s(arr[i]);
    CK(restored(b), "safe latch aggregate metadata");
    set_ok(r, pre, "safe inject latch n=%ld: %d ok, 12-n ENOMEM, reset recovers",
           n, nsucc - 1);
}

static void op_inj_hard(const Params *p, const Base *b, const char *pre,
                        Report *r) {
    long n = (long)(p->inject == NO_INJECT ? (p->tag % 9u) : p->inject);
    unsigned long df0 = cplus_alloc_double_free_count();
    void *arr[16];
    int nsucc = 0, i;
    void *q, *alias;

    cplus_alloc_fail_reset();
    cplus_alloc_fail_after(n);
    for (i = 0; i < 12; ++i) {
        q = cplus_malloc(8);
        if (i < n) {
            CK(q != NULL && cplus_alloc_is_live(q) == 1 &&
                   cplus_alloc_size(q) == 8,
               "hard latch: expected live 8-byte block");
            arr[nsucc++] = q;
        } else {
            CK(q == NULL, "hard latch: expected failure after threshold");
            r->refusals++;
        }
    }
    cplus_alloc_fail_reset();
    q = cplus_malloc(8);
    CK(q != NULL, "hard latch: no recovery after reset");
    alias = q;
    CK(cplus_free_checked(&q) == 0 && q == NULL, "hard checked free failed");
    CK(cplus_alloc_is_live(alias) == 0, "hard checked free left block live");
    errno = 0;
    CK(cplus_free_checked(&alias) == EINVAL && errno == EINVAL,
       "hard double free not refused");
    r->refusals++;
    CK(cplus_alloc_double_free_count() == df0 + 1,
       "hard double-free counter not incremented");
    for (i = 0; i < nsucc; ++i) cplus_free(arr[i]);
    CK(cplus_alloc_live_count() == b->hard_live,
       "hard latch live count not restored");
    CK(cplus_safe_alloc_live_count() == b->safe_live &&
           cplus_safe_alloc_live_bytes() == b->safe_bytes,
       "hard latch safecrt metadata changed");
    set_ok(r, pre, "hard inject latch n=%ld: %d ok, double-free counted+refused",
           n, nsucc);
}

static void op_overflow(const Params *p, const Base *b, const char *pre,
                        Report *r) {
    static const size_t bad[] = {RSIZE + 1, SIZE_MAX - 1, SIZE_MAX,
                                 SIZE_MAX / 2 + 1};
    size_t i;
    unsigned char *base, *q;

    (void)p;
    for (i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        size_t v = bad[i];
        errno = 0;
        q = (unsigned char *)malloc_s(v);
        CK(q == NULL && errno == EINVAL, "overflow malloc_s not refused");
        r->refusals++;
        errno = 0;
        q = (unsigned char *)realloc_s(NULL, v);
        CK(q == NULL && errno == EINVAL, "overflow realloc_s(NULL) not refused");
        r->refusals++;
        errno = 0;
        q = (unsigned char *)aligned_alloc_s(16, v);
        CK(q == NULL && errno == EINVAL, "overflow aligned_alloc_s not refused");
        r->refusals++;
    }

    {
        struct pair { size_t n, s; } cases[] = {
            {SIZE_MAX, 2}, {2, SIZE_MAX}, {RSIZE + 1, 1},
            {SIZE_MAX / 2 + 1, 2}, {SIZE_MAX, 0}, {0, SIZE_MAX}
        };
        for (i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
            errno = 0;
            q = (unsigned char *)calloc_s(cases[i].n, cases[i].s);
            if (cases[i].n == 0 || cases[i].s == 0) {
                CK(q != NULL && cplus_safe_ptr_size(q) == 1 && checkzero(q, 1),
                   "calloc_s zero-factor boundary wrong");
                free_s(q);
            } else {
                CK(q == NULL && errno == EOVERFLOW,
                   "calloc_s count*size overflow not refused");
                r->refusals++;
            }
        }
    }

    base = (unsigned char *)malloc_s(32);
    CK(base != NULL, "overflow base allocation failed");
    fillpat(base, 32);
    errno = 0;
    q = (unsigned char *)realloc_s(base, SIZE_MAX);
    CK(q == NULL && errno == EINVAL, "realloc_s SIZE_MAX not refused");
    r->refusals++;
    CK(cplus_safe_ptr_is_live(base) == 1 && cplus_safe_ptr_size(base) == 32 &&
           checkpat(base, 32),
       "realloc_s SIZE_MAX damaged owner");
    free_s(base);
    CK(freed_clean(base), "overflow base free metadata");
    CK(restored(b), "overflow aggregate metadata");
    set_ok(r, pre, "SIZE_MAX/count*size/aligned boundaries all refused atomically");
}

static void op_rinvalid(const Params *p, const Base *b, const char *pre,
                        Report *r) {
    unsigned char stack_obj[64];
    unsigned char *base, *q;
    void *alias;

    (void)p;
    fillpat(stack_obj, sizeof stack_obj);
    errno = 0;
    q = (unsigned char *)realloc_s(stack_obj, 32);
    CK(q == NULL && errno == EINVAL, "stack realloc not refused");
    r->refusals++;
    CK(checkpat(stack_obj, sizeof stack_obj), "stack realloc damaged object");

    base = (unsigned char *)malloc_s(48);
    CK(base != NULL, "rinvalid base allocation failed");
    fillpat(base, 48);
    errno = 0;
    q = (unsigned char *)realloc_s(base + 8, 64);
    CK(q == NULL && errno == EINVAL, "interior realloc not refused");
    r->refusals++;
    CK(cplus_safe_ptr_is_live(base) == 1 && cplus_safe_ptr_size(base) == 48 &&
           checkpat(base, 48),
       "interior realloc damaged owner");
    free_s(base);

    alias = malloc_s(16);
    CK(alias != NULL, "freed-realloc base failed");
    free_s(alias);
    errno = 0;
    q = (unsigned char *)realloc_s(alias, 32);
    CK(q == NULL && errno == EINVAL, "freed realloc not refused");
    r->refusals++;
    CK(cplus_safe_ptr_was_freed(alias) == 1, "freed alias state wrong");
    CK(restored(b), "rinvalid aggregate metadata");
    set_ok(r, pre, "stack/interior/freed realloc refused with owner intact");
}

typedef void (*OpFn)(const Params *, const Base *, const char *, Report *);

static void op_rgrow_wrap(const Params *p, const Base *b, const char *pre,
                          Report *r) {
    op_realloc(p, b, pre, r, 1);
}

static void op_rshrink_wrap(const Params *p, const Base *b, const char *pre,
                            Report *r) {
    op_realloc(p, b, pre, r, 0);
}

static void build_prefix(char *pre, size_t cap, uint64_t index,
                         const Params *p) {
    char injstr[24];
    if (p->inject == NO_INJECT)
        snprintf(injstr, sizeof injstr, "-");
    else
        snprintf(injstr, sizeof injstr, "%llu",
                 (unsigned long long)p->inject);
    snprintf(pre, cap,
             "%06llu|%s|size=%llu|count=%llu|size2=%llu|align=%llu|inject=%s"
             "|variant=%u|tag=%016llx",
             (unsigned long long)index, OP_NAMES[p->op],
             (unsigned long long)p->size, (unsigned long long)p->count,
             (unsigned long long)p->size2, (unsigned long long)p->align,
             injstr, (unsigned)p->variant, (unsigned long long)p->tag);
}

static void run_scenario(uint64_t index, const Params *p, Report *rep) {
    Base b;
    char pre[320];
    static const OpFn fns[OP_COUNT] = {
        op_malloc,       op_calloc,      op_rgrow_wrap,  op_rshrink_wrap,
        op_aligned,      op_free_double, op_free_invalid, op_zero,
        op_inj_safe,     op_inj_hard,    op_overflow,    op_rinvalid
    };

    cplus_safe_alloc_fail_reset();
    cplus_alloc_fail_reset();
    b.safe_live = cplus_safe_alloc_live_count();
    b.safe_bytes = cplus_safe_alloc_live_bytes();
    b.hard_live = cplus_alloc_live_count();
    g_tag = p->tag ? p->tag : UINT64_C(1);

    build_prefix(pre, sizeof pre, index, p);
    snprintf(rep->line, sizeof rep->line, "%s|PENDING", pre);

    fns[p->op](p, &b, pre, rep);

    if (rep->status == ST_PASS && rep->refusals > 0) rep->status = ST_REFUSED;
}

/* ------------------------------- Harness -------------------------------- */

static void on_alarm(int sig) {
    (void)sig;
    _exit(ST_HANG_EXIT);
}

int main(int argc, char **argv) {
    const char *logpath = argc > 1 ? argv[1] : "results/scenarios.log";
    long target = argc > 2 ? strtol(argv[2], NULL, 10) : TARGET_DEFAULT;
    Params *list, *ht;
    unsigned char *used;
    Report *rep;
    FILE *log;
    long i, nscen = 0, crashes = 0, hangs = 0, corrupts = 0, refusals = 0;

    if (target < 1) target = TARGET_DEFAULT;
    log = fopen(logpath, "w");
    if (!log) {
        perror("fopen scenarios.log");
        return 2;
    }
    fprintf(log, "# fuzz10k alloc seed=0x%016llx target=%ld\n",
            (unsigned long long)BASE_SEED, target);

    list = (Params *)calloc((size_t)target, sizeof *list);
    ht = (Params *)calloc(HT_CAP, sizeof *ht);
    used = (unsigned char *)calloc(HT_CAP, sizeof *used);
    rep = (Report *)mmap(NULL, sizeof *rep, PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (!list || !ht || !used || rep == MAP_FAILED) {
        fprintf(stderr, "fuzz10k: setup OOM\n");
        return 2;
    }

    for (i = 0; i < target; ++i) {
        uint64_t attempt = 0, ok;
        for (;;) {
            Params cand = gen_params((uint64_t)i, attempt++);
            ok = (uint64_t)insert_unique(ht, used, &cand);
            if (ok == 1) {
                list[i] = cand;
                break;
            }
            if (ok == (uint64_t)-1 || attempt > 1000) {
                fprintf(stderr, "fuzz10k: dedup table exhausted\n");
                return 2;
            }
        }
        ++nscen;
    }
    fprintf(log, "# distinct_scenarios=%ld\n", nscen);

    for (i = 0; i < nscen; ++i) {
        pid_t pid, w;
        int status = 0, spins = 0, hung = 0;
        const char *child_status;
        struct timespec ts;

        memset((void *)rep, 0, sizeof *rep);
        fflush(NULL);
        pid = fork();
        if (pid == 0) {
            struct sigaction sa;
            memset(&sa, 0, sizeof sa);
            sa.sa_handler = on_alarm;
            sigemptyset(&sa.sa_mask);
            sigaction(SIGALRM, &sa, NULL);
            alarm(3);
            run_scenario((uint64_t)i, &list[i], rep);
            _exit(rep->status == ST_CORRUPT ? 1
                  : rep->status == ST_REFUSED ? 2
                                              : 0);
        }
        if (pid < 0) {
            perror("fork");
            crashes++;
            fprintf(log, "%06ld|CHILD=FORKFAIL\n", i);
            continue;
        }

        ts.tv_sec = 0;
        ts.tv_nsec = 200000; /* 0.2 ms */
        for (;;) {
            w = waitpid(pid, &status, WNOHANG);
            if (w == pid) break;
            if (w < 0 && errno == EINTR) continue;
            nanosleep(&ts, NULL);
            if (++spins > 25000) { /* 5 s watchdog */
                kill(pid, SIGKILL);
                (void)waitpid(pid, &status, 0);
                hung = 1;
                break;
            }
        }

        if (hung) {
            hangs++;
            child_status = "HANG";
        } else if (WIFSIGNALED(status)) {
            int sig = WTERMSIG(status);
            if (sig == SIGALRM) {
                hangs++;
                child_status = "HANG";
            } else {
                crashes++;
                child_status = "CRASH";
            }
        } else if (WIFEXITED(status) && WEXITSTATUS(status) == ST_HANG_EXIT) {
            hangs++;
            child_status = "HANG";
        } else if (WIFEXITED(status) && WEXITSTATUS(status) == 1) {
            corrupts++;
            child_status = "CORRUPT";
        } else if (WIFEXITED(status) && WEXITSTATUS(status) == 2) {
            refusals += rep->refusals;
            child_status = "REFUSED";
        } else {
            refusals += rep->refusals;
            child_status = "OK";
        }
        fprintf(log, "%s|CHILD=%s\n",
                rep->line[0] ? rep->line : "<no-report>", child_status);
        if (fflush(log) != 0) {
            perror("fflush log");
            return 2;
        }
    }

    fprintf(log, "# SUMMARY scenarios=%ld crashes=%ld hangs=%ld "
                 "corruptions=%ld refusals=%ld\n",
            nscen, crashes, hangs, corrupts, refusals);
    fclose(log);
    printf("FUZZ10K alloc %ld %ld %ld %ld %ld\n", nscen, crashes, hangs,
           corrupts, refusals);
    return (crashes || hangs || corrupts) ? 1 : 0;
}
