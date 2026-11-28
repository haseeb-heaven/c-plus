/* Adversarial allocator-provenance / ownership attack test.
 *
 * Three allocators coexist and MUST NOT be mixed:
 *   (a) cplus_malloc/calloc/realloc/free + cplus_free_checked  -- live metadata
 *       table in libc/src/stdlib/alloc.c
 *   (b) host CRT malloc/free, also used by cplus_aligned_alloc -- no cplus meta
 *   (c) checked SafeCRT malloc_s/free_s -- its OWN metadata table in
 *       stdlib/safe/src/safe_alloc.c
 *
 * This test NEVER cross-frees. For every pointer it uses the allocator that
 * produced it. Cross-table statements are READ-ONLY provenance queries (or go
 * through a documented refusal guard) used to PROVE separation. Any false
 * "live"/"valid" answer, wrong return value, or crash is printed as FINDING:.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "install-include/cplus_libc.h"
#include "safe_alloc.h"

static int findings = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FINDING: %s\n", (msg));                                    \
            ++findings;                                                        \
        }                                                                      \
    } while (0)

/* ------------------------------------------------------------------------ */
/* Failure paths exercised with logging OFF and ON; return an observation    */
/* bitmask so we can prove toggling cplus_libc_log_set_enabled changes nothing*/
/* ------------------------------------------------------------------------ */
static int failure_paths(int logging) {
    int obs = 0;
    void *h, *d, *n = NULL, *g;
    cplus_alloc_meta m;

    cplus_libc_log_set_enabled(logging);

    /* checked free of a foreign (host CRT) pointer must be refused */
    h = malloc(16);
    CHECK(h != NULL, "failure_paths: host malloc failed");
    if (h == NULL)
        return -1;
    d = h;
    if (cplus_free_checked(&d) != 0)
        obs |= 1;
    if (d == NULL)
        obs |= 2;
    free(h); /* correct allocator for h */

    /* checked free of NULL is defined success */
    if (cplus_free_checked(&n) == 0)
        obs |= 4;

    /* generation forgery must be rejected */
    g = cplus_malloc(16);
    CHECK(g != NULL, "failure_paths: cplus_malloc failed");
    if (g == NULL)
        return -1;
    if (cplus_alloc_get_meta(g, &m) == 0) {
        if (cplus_alloc_check_generation(g, m.generation + 1u) == 0)
            obs |= 8;
        if (cplus_alloc_check_generation(g, 0u) == 0)
            obs |= 16;
    }
    cplus_free(g);

    /* transfer with NULL dst must be refused */
    if (cplus_transfer_ownership(NULL, &n) != 0)
        obs |= 32;

    cplus_libc_log_set_enabled(1);
    return obs;
}

int main(void) {
    cplus_alloc_meta m, m2;
    size_t base_live = cplus_alloc_live_count();
    size_t base_bytes = cplus_alloc_live_bytes();
    size_t cnt;
    int rc, obs_off, obs_on;

    /* ------------------------------------------------------------------ */
    /* 1. Cross-allocator metadata isolation                               */
    /* ------------------------------------------------------------------ */
    {
        void *cp = cplus_malloc(64);
        void *hp = malloc(64);                  /* host CRT */
        void *ap = cplus_aligned_alloc(16, 64); /* host-aligned */
        CHECK(cp != NULL, "scenario1: cplus_malloc returned NULL");
        CHECK(hp != NULL, "scenario1: host malloc returned NULL");
        CHECK(ap != NULL, "scenario1: cplus_aligned_alloc returned NULL");
        if (!cp || !hp || !ap)
            return 0;

        if (cplus_alloc_is_live(cp) != 1)
            CHECK(0, "scenario1: cplus_malloc pointer NOT reported live");
        else
            CHECK(1, "scenario1: ok");

        if (cplus_alloc_is_live(hp) != 0)
            CHECK(0, "scenario1: host malloc pointer falsely reported LIVE");
        else
            CHECK(1, "scenario1: ok");

        if (cplus_alloc_is_live(ap) != 0)
            CHECK(0, "scenario1: aligned_alloc pointer falsely reported LIVE");
        else
            CHECK(1, "scenario1: ok");

        if (cplus_alloc_size(cp) != 64)
            CHECK(0, "scenario1: cplus_alloc_size(cp) wrong");
        else
            CHECK(1, "scenario1: ok");
        if (cplus_alloc_size(hp) != 0)
            CHECK(0, "scenario1: host pointer falsely has non-zero cplus size");
        else
            CHECK(1, "scenario1: ok");
        if (cplus_alloc_size(ap) != 0)
            CHECK(0, "scenario1: aligned pointer falsely has non-zero cplus size");
        else
            CHECK(1, "scenario1: ok");

        if (cplus_alloc_get_meta(hp, &m) == 0 || m.is_live)
            CHECK(0, "scenario1: get_meta falsely validated host pointer");
        else
            CHECK(1, "scenario1: ok");

        /* Free each with its CORRECT allocator. */
        cplus_free(cp); /* (a) */
        free(hp);       /* (b) */
        free(ap);       /* (b): macOS aligned_alloc() must be freed by free() */
    }

    /* ------------------------------------------------------------------ */
    /* 2. Wrong-table double-free detection (no crash, defined returns)    */
    /* ------------------------------------------------------------------ */
    {
        void *p = cplus_malloc(32);
        void *alias;
        void *h = malloc(32);
        void *hd, *nz = NULL;
        unsigned long df0 = cplus_alloc_double_free_count();
        unsigned long inv0 = cplus_alloc_invalid_free_count();

        CHECK(p != NULL && h != NULL, "scenario2: allocation failed");
        if (!p || !h)
            return 0;
        alias = p;

        /* (a) pointer already freed by cplus_free (same allocator family) */
        cplus_free(p);
        hd = alias;
        rc = cplus_free_checked(&hd);
        if (rc == 0 || hd != NULL)
            CHECK(0, "scenario2: double-free not refused/cleared");
        else
            CHECK(1, "scenario2: ok");
        if (cplus_alloc_double_free_count() <= df0)
            CHECK(0, "scenario2: double-free counter did not increase");
        else
            CHECK(1, "scenario2: ok");

        /* (b) host malloc pointer handed to the cplus checked guard */
        hd = h;
        rc = cplus_free_checked(&hd);
        if (rc == 0 || hd != NULL)
            CHECK(0, "scenario2: host-pointer free not refused/cleared");
        else
            CHECK(1, "scenario2: ok");
        /* h must still be intact: prove by writing and reading it. */
        memset(h, 0x5A, 32);
        CHECK(((unsigned char *)h)[0] == 0x5A && ((unsigned char *)h)[31] == 0x5A,
              "scenario2: host pointer was corrupted/freed by cplus_free_checked");
        free(h);
        /* Whichever diagnostic fires is a bounded-recent-cache detail: a
         * foreign pointer whose address matches a recently freed cplus block
         * is classified as a double-free, otherwise as an invalid-free. In
         * both cases the free must be REFUSED, so at least one counter moves. */
        if ((cplus_alloc_double_free_count() + cplus_alloc_invalid_free_count()) <=
            (df0 + inv0))
            CHECK(0, "scenario2: neither invalid-free nor double-free counter increased");
        else
            CHECK(1, "scenario2: ok");

        /* (c) NULL is defined success */
        rc = cplus_free_checked(&nz);
        if (rc != 0 || nz != NULL)
            CHECK(0, "scenario2: checked-free(NULL) not defined success");
        else
            CHECK(1, "scenario2: ok");
    }

    /* ------------------------------------------------------------------ */
    /* 3. Generation forgery                                               */
    /* ------------------------------------------------------------------ */
    {
        void *g = cplus_malloc(48);
        unsigned long long gen;
        CHECK(g != NULL, "scenario3: cplus_malloc failed");
        if (!g)
            return 0;
        CHECK(cplus_alloc_get_meta(g, &m) == 0, "scenario3: get_meta failed");
        gen = m.generation;
        CHECK(gen > 0, "scenario3: generation is zero");
        if (cplus_alloc_check_generation(g, gen) != 1)
            CHECK(0, "scenario3: valid generation rejected");
        else
            CHECK(1, "scenario3: ok");
        if (cplus_alloc_check_generation(g, gen + 1u) != 0)
            CHECK(0, "scenario3: forged generation+1 ACCEPTED");
        else
            CHECK(1, "scenario3: ok");
        if (cplus_alloc_check_generation(g, 0u) != 0)
            CHECK(0, "scenario3: forged generation 0 ACCEPTED");
        else
            CHECK(1, "scenario3: ok");
        cplus_free(g);
        if (cplus_alloc_check_generation(g, gen) != 0)
            CHECK(0, "scenario3: stale (freed) pointer with old generation ACCEPTED");
        else
            CHECK(1, "scenario3: ok");
    }

    /* ------------------------------------------------------------------ */
    /* 4. Ownership transfer                                               */
    /* ------------------------------------------------------------------ */
    {
        void *src = cplus_malloc(40);
        void *original;
        void *dst = NULL, *dst2 = NULL, *dst3 = NULL;
        void *tmp, *holder;
        unsigned long long gen;

        CHECK(src != NULL, "scenario4: cplus_malloc failed");
        if (!src)
            return 0;
        original = src;
        CHECK(cplus_alloc_get_meta(src, &m) == 0, "scenario4: get_meta failed");
        gen = m.generation;

        CHECK(cplus_transfer_ownership(&dst, &src) == 0, "scenario4: transfer failed");
        if (src != NULL || dst != original || cplus_alloc_is_live(dst) != 1)
            CHECK(0, "scenario4: transfer did not null src / move ptr / keep live");
        else
            CHECK(1, "scenario4: ok");
        if (cplus_alloc_check_generation(dst, gen) != 0)
            CHECK(0, "scenario4: pre-transfer generation still valid after transfer");
        else
            CHECK(1, "scenario4: ok");
        CHECK(cplus_alloc_get_meta(dst, &m2) == 0, "scenario4: get_meta(dst) failed");
        if (m2.generation <= gen)
            CHECK(0, "scenario4: transfer did not bump generation");
        else
            CHECK(1, "scenario4: ok");

        /* dst == src */
        if (cplus_transfer_ownership(&dst, &dst) == 0)
            CHECK(0, "scenario4: transfer with dst==src ACCEPTED");
        else
            CHECK(1, "scenario4: ok");

        /* NULL dst */
        tmp = cplus_malloc(8);
        CHECK(tmp != NULL, "scenario4: tmp malloc failed");
        if (cplus_transfer_ownership(NULL, &tmp) == 0 || tmp == NULL)
            CHECK(0, "scenario4: NULL dst ACCEPTED or consumed source");
        else
            CHECK(1, "scenario4: ok");

        /* NULL src */
        if (cplus_transfer_ownership(&dst2, NULL) == 0 || dst2 != NULL)
            CHECK(0, "scenario4: NULL src ACCEPTED");
        else
            CHECK(1, "scenario4: ok");

        /* dst already holds a pointer */
        holder = dst;
        if (cplus_transfer_ownership(&holder, &tmp) == 0 || tmp == NULL)
            CHECK(0, "scenario4: non-empty dst ACCEPTED or consumed source");
        else
            CHECK(1, "scenario4: ok");

        /* already-transferred pointer: original src is now NULL */
        if (cplus_transfer_ownership(&dst3, &src) == 0 || dst3 != NULL)
            CHECK(0, "scenario4: transfer of already-transferred (NULL) src ACCEPTED");
        else
            CHECK(1, "scenario4: ok");

        /* Exactly one owner remains; free each block once. */
        cplus_free(dst);
        cplus_free(tmp);
    }

    /* ------------------------------------------------------------------ */
    /* 5. Size-query forgeries                                             */
    /* ------------------------------------------------------------------ */
    {
        void *q = cplus_malloc(80);
        void *hq = malloc(80);
        void *fq = cplus_malloc(80);
        cplus_alloc_meta fm;

        CHECK(q != NULL && hq != NULL && fq != NULL, "scenario5: allocation failed");
        if (!q || !hq || !fq)
            return 0;

        /* interior pointer */
        if (cplus_alloc_size((char *)q + 1) != 0)
            CHECK(0, "scenario5: interior pointer falsely has non-zero size");
        else
            CHECK(1, "scenario5: ok");
        cnt = 999;
        if (cplus_alloc_query_length((char *)q + 1, 1, &cnt) == 0 || cnt != 0)
            CHECK(0, "scenario5: interior pointer falsely validated by query_length");
        else
            CHECK(1, "scenario5: ok");

        /* host pointer */
        if (cplus_alloc_size(hq) != 0)
            CHECK(0, "scenario5: host pointer falsely has non-zero cplus size");
        else
            CHECK(1, "scenario5: ok");
        cnt = 999;
        if (cplus_alloc_query_length(hq, 1, &cnt) == 0 || cnt != 0)
            CHECK(0, "scenario5: host pointer falsely validated by query_length");
        else
            CHECK(1, "scenario5: ok");

        /* freed pointer */
        cplus_free(fq);
        if (cplus_alloc_size(fq) != 0)
            CHECK(0, "scenario5: freed pointer falsely has non-zero cplus size");
        else
            CHECK(1, "scenario5: ok");
        cnt = 999;
        if (cplus_alloc_query_length(fq, 1, &cnt) == 0 || cnt != 0)
            CHECK(0, "scenario5: freed pointer falsely validated by query_length");
        else
            CHECK(1, "scenario5: ok");
        if (cplus_alloc_get_meta(fq, &fm) == 0 || fm.is_live || fm.byte_size != 0)
            CHECK(0, "scenario5: freed pointer falsely validated by get_meta");
        else
            CHECK(1, "scenario5: ok");

        cplus_free(q);
        free(hq);
    }

    /* ------------------------------------------------------------------ */
    /* 6. Checked (SafeCRT) allocator isolation                            */
    /* ------------------------------------------------------------------ */
    {
        void *s = malloc_s(64);
        void *cp2 = cplus_malloc(64);
        void *cp3 = cplus_malloc(24);
        void *foreign = cp3;

        CHECK(s != NULL, "scenario6: malloc_s failed");
        CHECK(cp2 != NULL && cp3 != NULL, "scenario6: cplus_malloc failed");
        if (!s || !cp2 || !cp3)
            return 0;

        if (cplus_alloc_is_live(s) != 0)
            CHECK(0, "scenario6: malloc_s pointer falsely LIVE in cplus table");
        else
            CHECK(1, "scenario6: ok");
        if (cplus_safe_ptr_is_live(s) != 1)
            CHECK(0, "scenario6: malloc_s pointer not live in SafeCRT table");
        else
            CHECK(1, "scenario6: ok");
        if (cplus_alloc_size(s) != 0)
            CHECK(0, "scenario6: malloc_s pointer falsely has cplus size");
        else
            CHECK(1, "scenario6: ok");

        if (cplus_safe_ptr_is_live(cp2) != 0)
            CHECK(0, "scenario6: cplus pointer falsely LIVE in SafeCRT table");
        else
            CHECK(1, "scenario6: ok");

        /* Documented refusal guard: SafeCRT must refuse a foreign pointer and
         * leave it untouched (no cross-free). */
        errno = 0;
        if (free_checked_s(foreign) == CPLUS_SAFE_OK)
            CHECK(0, "scenario6: SafeCRT accepted foreign cplus pointer");
        else
            CHECK(1, "scenario6: ok");
        if (cplus_alloc_is_live(cp3) != 1)
            CHECK(0, "scenario6: foreign cplus pointer was freed by SafeCRT guard");
        else
            CHECK(1, "scenario6: ok");

        free_s(s);      /* correct allocator */
        cplus_free(cp2); /* correct allocator */
        cplus_free(cp3); /* correct allocator */
        if (cplus_safe_ptr_is_live(s) != 0)
            CHECK(0, "scenario6: SafeCRT pointer still live after free_s");
        else
            CHECK(1, "scenario6: ok");
    }

    /* ------------------------------------------------------------------ */
    /* 8. Extra stress: force metadata table growth/compaction and mass    */
    /*    address reuse; hunt for false-live / false-size / false-valid.   */
    /* ------------------------------------------------------------------ */
    {
        enum { N = 256 };
        void *v[N];
        int i;
        for (i = 0; i < N; ++i) {
            v[i] = cplus_calloc(1, (size_t)(i + 1));
            CHECK(v[i] != NULL, "scenario8: calloc failed");
            if (!v[i])
                break;
        }
        for (i = 0; i < N; ++i) {
            if (!v[i])
                continue;
            if (cplus_alloc_is_live(v[i]) != 1)
                CHECK(0, "scenario8: live block NOT reported live during table growth");
            if (cplus_alloc_size(v[i]) != (size_t)(i + 1))
                CHECK(0, "scenario8: size corrupted during table growth");
        }
        for (i = 0; i < N; ++i) {
            if (v[i])
                cplus_free(v[i]);
        }
        for (i = 0; i < N; ++i) {
            if (v[i] && cplus_alloc_is_live(v[i]) != 0)
                CHECK(0, "scenario8: freed pointer falsely LIVE after mass free");
        }
        /* Reuse addresses; stale references must not masquerade as the new
         * owner with the OLD generation. */
        for (i = 0; i < N; ++i) {
            unsigned long long old_gen;
            v[i] = cplus_malloc((size_t)(i + 1));
            if (!v[i])
                continue;
            CHECK(cplus_alloc_get_meta(v[i], &m) == 0, "scenario8: get_meta failed");
            old_gen = m.generation;
            if (cplus_alloc_check_generation(v[i], 0u) != 0 ||
                cplus_alloc_check_generation(v[i], old_gen + 1u) != 0)
                CHECK(0, "scenario8: forged generation accepted on reused address");
            cplus_free(v[i]);
        }
    }

    /* ------------------------------------------------------------------ */
    /* 7. Logging on/off must not change failure-path return values        */
    /* ------------------------------------------------------------------ */
    obs_off = failure_paths(0);
    obs_on = failure_paths(1);
    CHECK(obs_off == obs_on, "scenario7: logging changed failure-path return values");
    CHECK(obs_off == 0x3F, "scenario7: expected failure paths not all exercised");
    cplus_libc_log_set_enabled(1);

    /* Final ownership/leak sanity: every cplus block was freed exactly once. */
    CHECK(cplus_alloc_live_count() == base_live, "final: cplus live count leaked");
    CHECK(cplus_alloc_live_bytes() == base_bytes, "final: cplus live bytes leaked");

    printf("provenance_attack: ok\n");
    if (findings)
        printf("provenance_attack: %d FINDING(S)\n", findings);
    return 0;
}
