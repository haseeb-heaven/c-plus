/*
 * Deterministic blue-team regression tests for the checked memory APIs.
 *
 * Ownership: tests/defense/memory/ only.  No build scripts, compiler, runtime
 * or third_party files are modified here.
 *
 * The tests below reproduce inputs that the pre-hardening library accepted
 * (a provable heap source over-read in memcpy_s_auto / memmove_s_auto) and
 * assert REFUSED + CANARY INTACT afterwards.  They also pin the valid-input
 * semantics that must NOT change (successful copies, overlap policy, NULL and
 * zero-length behaviour, the memcmp -1/errno disambiguation convention).
 *
 * All violation paths are made observable by disabling the default fail-stop
 * trap (cplus_runtime_set_violation_trap(0)).
 */
#define _POSIX_C_SOURCE 200112L
#define CPLUS_SAFE_NO_UNPREFIXED_MACROS
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "safe_alloc.h"
#include "safe_error.h"
#include "safe_log.h"
#include "safe_memory.h"
#include "safe_runtime.h"

static int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            g_failures++;                                                      \
            fprintf(stderr, "FAIL %s:%d: %s (errno=%d)\n", __FILE__, __LINE__, \
                    #cond, errno);                                            \
        }                                                                      \
    } while (0)

#define CANARY 0xA5u

static void fill_canary(unsigned char *p, size_t n) { memset(p, CANARY, n); }

static int all_canary(const unsigned char *p, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i)
        if (p[i] != CANARY) return 0;
    return 1;
}

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    cplus_runtime_set_violation_trap(0);

    /* Every capacity used below is proven (tracked malloc_s metadata or
     * compile-time __builtin_object_size), so strict unknown-capacity mode can
     * stay on for the whole suite. */
    cplus_safe_set_unknown_capacity_mode(1);
    CHECK(cplus_safe_unknown_capacity_strict() == 1);

    /* ---- T1: interior heap source, 8 bytes proven, ask for 32 (memcpy) ----
     * Before hardening: accepted; bytes [8..31] came from memory beyond the
     * source allocation (the probe printed allocator metadata 02 00 00 ...).
     * After hardening: REFUSED, errno=ERANGE, destination canary intact. */
    {
        unsigned char *sbase = (unsigned char *)malloc_s(64);
        unsigned char *dbase = (unsigned char *)malloc_s(64);
        unsigned char *src;
        size_t dcap;
        void *r;
        CHECK(sbase != NULL && dbase != NULL);
        if (!sbase || !dbase) return 2;
        memset(sbase, 'A', 64);
        fill_canary(dbase, 64);
        src = sbase + 56; /* remaining tracked source capacity == 8 */
        dcap = cplus_safe_remaining_capacity(dbase);
        CHECK(cplus_safe_remaining_capacity(src) == 8);
        CHECK(dcap == 64);
        errno = 0;
        r = cplus_memcpy_s_auto(dbase, src, 32, dcap);
        CHECK(r == NULL);
        CHECK(errno == ERANGE);
        CHECK(all_canary(dbase, 64));
        free_s(sbase);
        free_s(dbase);
    }

    /* ---- T2: same shape for memmove_s_auto ---- */
    {
        unsigned char *sbase = (unsigned char *)malloc_s(64);
        unsigned char *dbase = (unsigned char *)malloc_s(64);
        unsigned char *src;
        size_t dcap;
        void *r;
        CHECK(sbase != NULL && dbase != NULL);
        if (!sbase || !dbase) return 2;
        memset(sbase, 'A', 64);
        fill_canary(dbase, 64);
        src = sbase + 56;
        dcap = cplus_safe_remaining_capacity(dbase);
        CHECK(cplus_safe_remaining_capacity(src) == 8);
        errno = 0;
        r = cplus_memmove_s_auto(dbase, src, 32, dcap);
        CHECK(r == NULL);
        CHECK(errno == ERANGE);
        CHECK(all_canary(dbase, 64));
        free_s(sbase);
        free_s(dbase);
    }

    /* ---- T3: exact-size heap source, request one more than its capacity ---- */
    {
        unsigned char *src = (unsigned char *)malloc_s(8);
        unsigned char *dst = (unsigned char *)malloc_s(32);
        void *r;
        CHECK(src != NULL && dst != NULL);
        if (!src || !dst) return 2;
        memset(src, 'B', 8);
        fill_canary(dst, 32);
        errno = 0;
        r = cplus_memcpy_s_auto(dst, src, 9, cplus_safe_remaining_capacity(dst));
        CHECK(r == NULL);
        CHECK(errno == ERANGE);
        CHECK(all_canary(dst, 32));
        free_s(src);
        free_s(dst);
    }

    /* ---- T4: VALID input with a tracked source must still succeed ----
     * Guards against an over-eager fix that refuses all heap sources. */
    {
        unsigned char *src = (unsigned char *)malloc_s(32);
        unsigned char *dst = (unsigned char *)malloc_s(32);
        size_t i;
        void *r;
        CHECK(src != NULL && dst != NULL);
        if (!src || !dst) return 2;
        for (i = 0; i < 32; ++i) src[i] = (unsigned char)(i + 1);
        fill_canary(dst, 32);
        errno = 0;
        r = cplus_memcpy_s_auto(dst, src, 16, cplus_safe_remaining_capacity(dst));
        CHECK(r == dst);
        CHECK(errno == 0);
        CHECK(memcmp(dst, src, 16) == 0);
        CHECK(all_canary(dst + 16, 16));
        free_s(src);
        free_s(dst);
    }

    /* ---- T5: UNTRACKED (stack) source: no new refusal, even in strict mode ----
     * Source BOS is unavailable inside the fixed public signature; the new
     * check only refuses when proven capacity exists and is insufficient. */
    {
        unsigned char dst[64];
        const unsigned char src[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        void *r;
        fill_canary(dst, sizeof dst);
        errno = 0;
        r = cplus_memcpy_s_auto(dst, src, 8, CPLUS_SAFE_OBJ_SIZE(dst));
        CHECK(r == dst);
        CHECK(errno == 0);
        CHECK(memcmp(dst, src, 8) == 0);
        CHECK(all_canary(dst + 8, sizeof dst - 8));
    }

    /* ---- T6: destination overflow refusal + canary (pre-existing behaviour) ---- */
    {
        unsigned char *dst = (unsigned char *)malloc_s(16);
        unsigned char src[32];
        void *r;
        CHECK(dst != NULL);
        if (!dst) return 2;
        memset(src, 'C', sizeof src);
        fill_canary(dst, 16);
        errno = 0;
        r = cplus_memcpy_s_auto(dst, src, 32, cplus_safe_remaining_capacity(dst));
        CHECK(r == NULL);
        CHECK(errno == ERANGE);
        CHECK(all_canary(dst, 16));
        free_s(dst);
    }

    /* ---- T7: overlap policy unchanged (checked memcpy refuses, memmove ok) ---- */
    {
        char a[16] = "0123456789";
        char b[16] = "0123456789";
        int diff = 99;
        CHECK(cplus_memcpy_s(a + 2, sizeof a - 2, a, 5) != 0);
        CHECK(cplus_memmove_s(b + 2, sizeof b - 2, b, 5) == 0);
        CHECK(memcmp(b, "0101234789", 10) == 0);
        CHECK(cplus_memcmp_s("abc", 3, "abd", 3, &diff) == 0);
        CHECK(diff < 0);
    }

    /* ---- T8: memcmp -1 collision is disambiguated by errno, not by value ----
     * A genuine "less than" returns -1 with errno untouched; a refusal also
     * returns -1 but sets errno.  Pin both halves of that contract. */
    {
        char small[4] = "abc";
        int diff = 0;
        errno = 0;
        CHECK(cplus_memcmp_s("a", 1, "b", 1, &diff) == 0);
        CHECK(diff == -1);
        CHECK(errno == 0);
        errno = 0;
        CHECK(cplus_memcmp_s(small, sizeof small, "0123456789", 10, &diff) != 0);
        CHECK(errno != 0);
    }

    /* ---- T9: NULL and zero-length semantics unchanged ---- */
    {
        char buf[8] = "xyz";
        errno = 0;
        CHECK(cplus_memcpy_s_auto(NULL, buf, 0, sizeof buf) == NULL);
        CHECK(errno == EINVAL);
        errno = 0;
        CHECK(cplus_memcpy_s_auto(buf, NULL, 0, sizeof buf) == NULL);
        CHECK(errno == EINVAL);
        errno = 0;
        CHECK(cplus_memcpy_s_auto(buf, buf, 0, sizeof buf) == buf);
        CHECK(errno == 0);
        CHECK(strcmp(buf, "xyz") == 0);
        CHECK(cplus_memzero_s(buf, sizeof buf) == 0);
    }

    /* Restore the default trap so this suite never leaves global state altered. */
    cplus_safe_set_unknown_capacity_mode(0);
    cplus_runtime_set_violation_trap(1);

    if (g_failures == 0) {
        puts("memory_hardening: PASS (0 failures)");
        return 0;
    }
    printf("memory_hardening: FAIL (%d failures)\n", g_failures);
    return 1;
}
