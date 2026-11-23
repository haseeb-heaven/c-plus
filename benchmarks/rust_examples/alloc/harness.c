/* Deterministic C99 harness for EVERY checked allocation/free API that the
 * SafeCRT and hardened-libc archives export.
 *
 * One line per case:  <api> <case> <token>
 * (the task calls this "<api> <case> <token>"; the repository's older
 *  per-method harness calls the first field "method" -- same meaning).
 *
 * Tokens
 *   OK          valid alloc/free (or free-checked) succeeded
 *   REFUSED     dangerous op refused: overflow / double / invalid / injection
 *   DEFINED     zero-size or NULL no-op has defined behavior
 *   PRESERVED   injected failure returned NULL and left the original block
 *               live and untouched (realloc atomicity)
 *   CANARY_OK   the 8-byte guard at offset 64 survived the whole case intact
 *   LIVE/NOT_LIVE/SIZED/ONE_BYTE/FREED/NOT_FREED/DETECTED
 *               metadata / counter observations ( Rust emits BLOCKED for all
 *               of these -- see NOTES.md )
 *   MISMATCH    observed behavior disagreed with the documented contract
 *
 * Only the checked allocation APIs are exercised. No game/app memory.
 * Link: build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SafeCRT checked API (malloc_s, ..., cplus_safe_*). */
#include "stdlib/safe/safe_alloc.h"
/* Hardened libc API (cplus_malloc, ..., cplus_alloc_*). */
#include "cplus_libc.h"

static void emit(const char *api, const char *testcase, const char *token) {
    printf("%s %s %s\n", api, testcase, token);
}

/* 64-byte payload followed by an 8-byte guard canary. */
#define PAYLOAD 64u
#define GUARD 8u
#define TOTAL (PAYLOAD + GUARD)
static const uint64_t CANARY_WORD = (uint64_t)0xDEADBEEFCAFEBABEULL;

static void write_payload_with_guard(unsigned char *buf) {
    size_t i;
    for (i = 0; i < PAYLOAD; ++i) buf[i] = (unsigned char)(i & 0xffu);
    memcpy(buf + PAYLOAD, &CANARY_WORD, GUARD);
}

static int guard_intact(const unsigned char *buf) {
    uint64_t w;
    memcpy(&w, buf + PAYLOAD, GUARD);
    return w == CANARY_WORD;
}

/* ==================================================================== */
/* SafeCRT checked allocation/free API                                   */
/* ==================================================================== */

static void test_malloc_s(void) {
    const char *a = "malloc_s";
    void *p;

    errno = 0;
    p = malloc_s(TOTAL);
    if (p && cplus_safe_ptr_is_live(p)) {
        write_payload_with_guard((unsigned char *)p);
        free_s(p);
        emit(a, "valid", "OK");
    } else {
        if (p) free_s(p);
        emit(a, "valid", "MISMATCH");
    }

    errno = 0;
    p = malloc_s(SIZE_MAX); /* > RSIZE_MAX_MEM -> EINVAL */
    emit(a, "overflow", (p == NULL && errno == EINVAL) ? "REFUSED" : "MISMATCH");
    if (p) free_s(p);

    errno = 0;
    p = malloc_s(0); /* defined one-byte tracked block */
    emit(a, "zero",
         (p != NULL && cplus_safe_ptr_is_live(p) && cplus_safe_ptr_size(p) == 1u)
             ? "DEFINED"
             : "MISMATCH");
    if (p) free_s(p);

    cplus_safe_alloc_fail_after(0);
    errno = 0;
    p = malloc_s(TOTAL);
    cplus_safe_alloc_fail_reset();
    emit(a, "inject", (p == NULL && errno == ENOMEM) ? "REFUSED" : "MISMATCH");
    if (p) free_s(p);

    errno = 0;
    p = malloc_s(TOTAL);
    if (p) {
        write_payload_with_guard((unsigned char *)p);
        emit(a, "canary", guard_intact((unsigned char *)p) ? "CANARY_OK" : "MISMATCH");
        free_s(p);
    } else {
        emit(a, "canary", "MISMATCH");
    }
}

static void test_calloc_s(void) {
    const char *a = "calloc_s";
    void *p;

    errno = 0;
    p = calloc_s(4u, 16u);
    if (p) {
        unsigned char *b = (unsigned char *)p;
        size_t i;
        int zeroed = 1;
        for (i = 0; i < 64u; ++i) {
            if (b[i] != 0u) { zeroed = 0; break; }
        }
        free_s(p);
        emit(a, "valid", zeroed ? "OK" : "MISMATCH");
    } else {
        emit(a, "valid", "MISMATCH");
    }

    errno = 0;
    p = calloc_s(SIZE_MAX, 4u); /* count*size overflow -> EOVERFLOW */
    emit(a, "overflow", (p == NULL && errno == EOVERFLOW) ? "REFUSED" : "MISMATCH");
    if (p) free_s(p);

    errno = 0;
    p = calloc_s(0u, 8u);
    emit(a, "zero",
         (p != NULL && cplus_safe_ptr_is_live(p) && cplus_safe_ptr_size(p) == 1u)
             ? "DEFINED"
             : "MISMATCH");
    if (p) free_s(p);

    cplus_safe_alloc_fail_after(0);
    errno = 0;
    p = calloc_s(4u, 16u);
    cplus_safe_alloc_fail_reset();
    emit(a, "inject", (p == NULL && errno == ENOMEM) ? "REFUSED" : "MISMATCH");
    if (p) free_s(p);

    errno = 0;
    p = calloc_s(TOTAL / 8u, 8u);
    if (p) {
        write_payload_with_guard((unsigned char *)p);
        emit(a, "canary", guard_intact((unsigned char *)p) ? "CANARY_OK" : "MISMATCH");
        free_s(p);
    } else {
        emit(a, "canary", "MISMATCH");
    }
}

static void test_realloc_s(void) {
    const char *a = "realloc_s";
    void *p, *q;

    errno = 0;
    p = malloc_s(32u);
    if (p) {
        q = realloc_s(p, 256u);
        if (q && cplus_safe_ptr_is_live(q) && cplus_safe_ptr_size(q) == 256u) {
            free_s(q);
            emit(a, "valid", "OK");
        } else {
            if (q) free_s(q); else free_s(p);
            emit(a, "valid", "MISMATCH");
        }
    } else {
        emit(a, "valid", "MISMATCH");
    }

    errno = 0;
    p = malloc_s(32u);
    if (p) {
        q = realloc_s(p, SIZE_MAX);
        emit(a, "overflow",
             (q == NULL && errno == EINVAL && cplus_safe_ptr_is_live(p) &&
              cplus_safe_ptr_size(p) == 32u) ? "REFUSED" : "MISMATCH");
        free_s(p);
    } else {
        emit(a, "overflow", "MISMATCH");
    }

    errno = 0;
    p = malloc_s(32u);
    if (p) {
        q = realloc_s(p, 0u);
        emit(a, "zero",
             (q != NULL && cplus_safe_ptr_is_live(q) && cplus_safe_ptr_size(q) == 1u)
                 ? "DEFINED" : "MISMATCH");
        if (q) free_s(q); else free_s(p);
    } else {
        emit(a, "zero", "MISMATCH");
    }

    errno = 0;
    p = malloc_s(32u);
    if (p) {
        cplus_safe_alloc_fail_after(0);
        q = realloc_s(p, 256u);
        cplus_safe_alloc_fail_reset();
        emit(a, "inject",
             (q == NULL && errno == ENOMEM && cplus_safe_ptr_is_live(p) &&
              cplus_safe_ptr_size(p) == 32u) ? "PRESERVED" : "MISMATCH");
        free_s(p);
    } else {
        emit(a, "inject", "MISMATCH");
    }

    {
        unsigned char stack_obj[16];
        errno = 0;
        q = realloc_s(stack_obj, 32u); /* not owned by this allocator */
        emit(a, "invalid", (q == NULL && errno == EINVAL) ? "REFUSED" : "MISMATCH");
        if (q) free_s(q);
    }

    errno = 0;
    p = malloc_s(TOTAL);
    if (p) {
        write_payload_with_guard((unsigned char *)p);
        q = realloc_s(p, TOTAL);
        emit(a, "canary",
             (q != NULL && guard_intact((unsigned char *)q)) ? "CANARY_OK" : "MISMATCH");
        if (q) free_s(q); else free_s(p);
    } else {
        emit(a, "canary", "MISMATCH");
    }
}

static void test_free_s(void) {
    const char *a = "free_s";
    void *p;

    errno = 0;
    p = malloc_s(32u);
    if (p) {
        free_s(p);
        emit(a, "valid", cplus_safe_ptr_is_live(p) ? "MISMATCH" : "OK");
    } else {
        emit(a, "valid", "MISMATCH");
    }

    errno = 0;
    free_s(NULL); /* documented no-op */
    emit(a, "null", (errno == 0) ? "DEFINED" : "MISMATCH");

    errno = 0;
    p = malloc_s(32u);
    if (p) {
        free_s(p);                 /* first release */
        errno = 0;
        free_s(p);                 /* stale alias: refused, sets errno=EINVAL */
        emit(a, "double", (errno == EINVAL && cplus_safe_ptr_was_freed(p))
                 ? "REFUSED" : "MISMATCH");
    } else {
        emit(a, "double", "MISMATCH");
    }

    {
        unsigned char stack_obj[16];
        errno = 0;
        free_s(stack_obj); /* not owned: refused */
        emit(a, "invalid", (errno == EINVAL) ? "REFUSED" : "MISMATCH");
    }

    errno = 0;
    p = malloc_s(TOTAL);
    if (p) {
        void *other = malloc_s(16u);
        write_payload_with_guard((unsigned char *)p);
        if (other) free_s(other);
        emit(a, "canary", guard_intact((unsigned char *)p) ? "CANARY_OK" : "MISMATCH");
        free_s(p);
    } else {
        emit(a, "canary", "MISMATCH");
    }
}

static void test_free_checked_s(void) {
    const char *a = "free_checked_s";
    void *p;

    errno = 0;
    p = malloc_s(32u);
    if (p) {
        int rc = free_checked_s(p);
        emit(a, "valid",
             (rc == CPLUS_SAFE_OK && !cplus_safe_ptr_is_live(p)) ? "OK" : "MISMATCH");
    } else {
        emit(a, "valid", "MISMATCH");
    }

    errno = 0;
    p = malloc_s(32u);
    if (p) {
        void *alias = p;           /* stale alias */
        int rc1 = free_checked_s(p);
        int rc2 = free_checked_s(alias);
        emit(a, "double",
             (rc1 == CPLUS_SAFE_OK && rc2 == CPLUS_SAFE_EINVAL && errno == EINVAL)
                 ? "REFUSED" : "MISMATCH");
    } else {
        emit(a, "double", "MISMATCH");
    }

    {
        unsigned char stack_obj[16];
        errno = 0;
        emit(a, "invalid",
             (free_checked_s(stack_obj) == CPLUS_SAFE_EINVAL && errno == EINVAL)
                 ? "REFUSED" : "MISMATCH");
    }

    errno = 0;
    emit(a, "null", (free_checked_s(NULL) == CPLUS_SAFE_OK) ? "DEFINED" : "MISMATCH");

    errno = 0;
    p = malloc_s(TOTAL);
    if (p) {
        void *other = malloc_s(16u);
        write_payload_with_guard((unsigned char *)p);
        if (other) (void)free_checked_s(other);
        emit(a, "canary", guard_intact((unsigned char *)p) ? "CANARY_OK" : "MISMATCH");
        free_s(p);
    } else {
        emit(a, "canary", "MISMATCH");
    }
}

static void test_aligned_alloc_s(void) {
    const char *a = "aligned_alloc_s";
    void *p;

    errno = 0;
    p = aligned_alloc_s(16u, 64u);
    if (p && (((uintptr_t)p & 15u) == 0u) && cplus_safe_ptr_is_live(p)) {
        free_s(p);
        emit(a, "valid", "OK");
    } else {
        if (p) free_s(p);
        emit(a, "valid", "MISMATCH");
    }

    errno = 0;
    p = aligned_alloc_s(16u, SIZE_MAX);
    emit(a, "overflow", (p == NULL && errno == EINVAL) ? "REFUSED" : "MISMATCH");
    if (p) free_s(p);

    errno = 0;
    p = aligned_alloc_s(16u, 0u);
    emit(a, "zero",
         (p != NULL && cplus_safe_ptr_is_live(p) && cplus_safe_ptr_size(p) == 1u)
             ? "DEFINED" : "MISMATCH");
    if (p) free_s(p);

    errno = 0;
    p = aligned_alloc_s(3u, 64u); /* not power-of-two / < sizeof(void*) */
    emit(a, "bad_align", (p == NULL && errno == EINVAL) ? "REFUSED" : "MISMATCH");
    if (p) free_s(p);

    cplus_safe_alloc_fail_after(0);
    errno = 0;
    p = aligned_alloc_s(16u, 64u);
    cplus_safe_alloc_fail_reset();
    emit(a, "inject", (p == NULL && errno == ENOMEM) ? "REFUSED" : "MISMATCH");
    if (p) free_s(p);

    errno = 0;
    p = aligned_alloc_s(16u, TOTAL);
    if (p) {
        write_payload_with_guard((unsigned char *)p);
        emit(a, "canary", guard_intact((unsigned char *)p) ? "CANARY_OK" : "MISMATCH");
        free_s(p);
    } else {
        emit(a, "canary", "MISMATCH");
    }
}

/* ---- SafeCRT metadata queries and failure-injection control ------------ */

static void test_safe_metadata_and_control(void) {
    void *p;
    unsigned char stack_obj[16];

    /* cplus_safe_ptr_is_live */
    p = malloc_s(32u);
    emit("cplus_safe_ptr_is_live", "live", cplus_safe_ptr_is_live(p) ? "LIVE" : "MISMATCH");
    free_s(p);
    emit("cplus_safe_ptr_is_live", "freed",
         !cplus_safe_ptr_is_live(p) ? "NOT_LIVE" : "MISMATCH");
    emit("cplus_safe_ptr_is_live", "foreign",
         !cplus_safe_ptr_is_live(stack_obj) ? "NOT_LIVE" : "MISMATCH");

    /* cplus_safe_ptr_size */
    p = malloc_s(64u);
    emit("cplus_safe_ptr_size", "live",
         (cplus_safe_ptr_size(p) == 64u) ? "SIZED" : "MISMATCH");
    free_s(p);
    p = malloc_s(0u);
    emit("cplus_safe_ptr_size", "zero",
         (cplus_safe_ptr_size(p) == 1u) ? "ONE_BYTE" : "MISMATCH");
    free_s(p);

    /* cplus_safe_ptr_was_freed (the task's "ptr_was_freed") */
    p = malloc_s(32u);
    emit("cplus_safe_ptr_was_freed", "live",
         (cplus_safe_ptr_was_freed(p) == 0) ? "NOT_FREED" : "MISMATCH");
    free_s(p);
    emit("cplus_safe_ptr_was_freed", "after_free",
         (cplus_safe_ptr_was_freed(p) == 1) ? "FREED" : "MISMATCH");

    /* cplus_safe_alloc_fail_after / reset */
    cplus_safe_alloc_fail_after(0);
    errno = 0;
    p = malloc_s(32u);
    cplus_safe_alloc_fail_reset();
    emit("cplus_safe_alloc_fail_after", "inject",
         (p == NULL && errno == ENOMEM) ? "REFUSED" : "MISMATCH");
    if (p) free_s(p);

    cplus_safe_alloc_fail_after(0);
    (void)malloc_s(8u);               /* consume the forced failure */
    cplus_safe_alloc_fail_reset();
    errno = 0;
    p = malloc_s(32u);
    emit("cplus_safe_alloc_fail_reset", "reset", (p != NULL) ? "OK" : "MISMATCH");
    if (p) free_s(p);
}

/* ==================================================================== */
/* Hardened libc allocation/free API (cplus_*)                           */
/* ==================================================================== */

static void test_cplus_malloc(void) {
    const char *a = "cplus_malloc";
    void *p;

    errno = 0;
    p = cplus_malloc(TOTAL);
    if (p && cplus_alloc_is_live(p)) {
        write_payload_with_guard((unsigned char *)p);
        cplus_free(p);
        emit(a, "valid", "OK");
    } else {
        if (p) cplus_free(p);
        emit(a, "valid", "MISMATCH");
    }

    /* Hardened layer has no RSIZE_MAX_MEM gate: host malloc(SIZE_MAX)
     * returns NULL with ENOMEM. Outcome (refusal) is the same. */
    errno = 0;
    p = cplus_malloc(SIZE_MAX);
    emit(a, "overflow", (p == NULL && errno == ENOMEM) ? "REFUSED" : "MISMATCH");
    if (p) cplus_free(p);

    errno = 0;
    p = cplus_malloc(0);
    emit(a, "zero",
         (p != NULL && cplus_alloc_is_live(p) && cplus_alloc_size(p) == 1u)
             ? "DEFINED" : "MISMATCH");
    if (p) cplus_free(p);

    cplus_alloc_fail_after(0);
    errno = 0;
    p = cplus_malloc(TOTAL);
    cplus_alloc_fail_reset();
    emit(a, "inject", (p == NULL && errno == ENOMEM) ? "REFUSED" : "MISMATCH");
    if (p) cplus_free(p);

    errno = 0;
    p = cplus_malloc(TOTAL);
    if (p) {
        write_payload_with_guard((unsigned char *)p);
        emit(a, "canary", guard_intact((unsigned char *)p) ? "CANARY_OK" : "MISMATCH");
        cplus_free(p);
    } else {
        emit(a, "canary", "MISMATCH");
    }
}

static void test_cplus_calloc(void) {
    const char *a = "cplus_calloc";
    void *p;

    errno = 0;
    p = cplus_calloc(4u, 16u);
    if (p) {
        unsigned char *b = (unsigned char *)p;
        size_t i;
        int zeroed = 1;
        for (i = 0; i < 64u; ++i) {
            if (b[i] != 0u) { zeroed = 0; break; }
        }
        cplus_free(p);
        emit(a, "valid", zeroed ? "OK" : "MISMATCH");
    } else {
        emit(a, "valid", "MISMATCH");
    }

    errno = 0;
    p = cplus_calloc(SIZE_MAX, 4u); /* overflow -> ENOMEM on hardened layer */
    emit(a, "overflow", (p == NULL && errno == ENOMEM) ? "REFUSED" : "MISMATCH");
    if (p) cplus_free(p);

    errno = 0;
    p = cplus_calloc(0u, 8u);
    emit(a, "zero",
         (p != NULL && cplus_alloc_is_live(p) && cplus_alloc_size(p) == 1u)
             ? "DEFINED" : "MISMATCH");
    if (p) cplus_free(p);

    cplus_alloc_fail_after(0);
    errno = 0;
    p = cplus_calloc(4u, 16u);
    cplus_alloc_fail_reset();
    emit(a, "inject", (p == NULL && errno == ENOMEM) ? "REFUSED" : "MISMATCH");
    if (p) cplus_free(p);

    errno = 0;
    p = cplus_calloc(TOTAL / 8u, 8u);
    if (p) {
        write_payload_with_guard((unsigned char *)p);
        emit(a, "canary", guard_intact((unsigned char *)p) ? "CANARY_OK" : "MISMATCH");
        cplus_free(p);
    } else {
        emit(a, "canary", "MISMATCH");
    }
}

static void test_cplus_realloc(void) {
    const char *a = "cplus_realloc";
    void *p, *q;

    errno = 0;
    p = cplus_malloc(32u);
    if (p) {
        q = cplus_realloc(p, 256u);
        if (q && cplus_alloc_is_live(q) && cplus_alloc_size(q) == 256u) {
            cplus_free(q);
            emit(a, "valid", "OK");
        } else {
            if (q) cplus_free(q); else cplus_free(p);
            emit(a, "valid", "MISMATCH");
        }
    } else {
        emit(a, "valid", "MISMATCH");
    }

    errno = 0;
    p = cplus_malloc(32u);
    if (p) {
        q = cplus_realloc(p, SIZE_MAX);
        emit(a, "overflow",
             (q == NULL && cplus_alloc_is_live(p) && cplus_alloc_size(p) == 32u)
                 ? "REFUSED" : "MISMATCH");
        cplus_free(p);
    } else {
        emit(a, "overflow", "MISMATCH");
    }

    errno = 0;
    p = cplus_malloc(32u);
    if (p) {
        q = cplus_realloc(p, 0u);
        emit(a, "zero",
             (q != NULL && cplus_alloc_is_live(q) && cplus_alloc_size(q) == 1u)
                 ? "DEFINED" : "MISMATCH");
        if (q) cplus_free(q); else cplus_free(p);
    } else {
        emit(a, "zero", "MISMATCH");
    }

    errno = 0;
    p = cplus_malloc(32u);
    if (p) {
        cplus_alloc_fail_after(0);
        q = cplus_realloc(p, 256u);
        cplus_alloc_fail_reset();
        emit(a, "inject",
             (q == NULL && cplus_alloc_is_live(p) && cplus_alloc_size(p) == 32u)
                 ? "PRESERVED" : "MISMATCH");
        cplus_free(p);
    } else {
        emit(a, "inject", "MISMATCH");
    }

    {
        unsigned char stack_obj[16];
        errno = 0;
        q = cplus_realloc(stack_obj, 32u);
        emit(a, "invalid", (q == NULL && errno == EINVAL) ? "REFUSED" : "MISMATCH");
        if (q) cplus_free(q);
    }

    errno = 0;
    p = cplus_malloc(TOTAL);
    if (p) {
        write_payload_with_guard((unsigned char *)p);
        q = cplus_realloc(p, TOTAL);
        emit(a, "canary",
             (q != NULL && guard_intact((unsigned char *)q)) ? "CANARY_OK" : "MISMATCH");
        if (q) cplus_free(q); else cplus_free(p);
    } else {
        emit(a, "canary", "MISMATCH");
    }
}

static void test_cplus_free(void) {
    const char *a = "cplus_free";
    void *p;
    unsigned long before, after;

    errno = 0;
    p = cplus_malloc(32u);
    if (p) {
        cplus_free(p);
        emit(a, "valid", cplus_alloc_is_live(p) ? "MISMATCH" : "OK");
    } else {
        emit(a, "valid", "MISMATCH");
    }

    errno = 0;
    cplus_free(NULL);
    emit(a, "null", (errno == 0) ? "DEFINED" : "MISMATCH");

    before = cplus_alloc_double_free_count();
    p = cplus_malloc(32u);
    if (p) {
        cplus_free(p);
        cplus_free(p); /* stale alias -> counted as double free */
        after = cplus_alloc_double_free_count();
        emit(a, "double", (after > before) ? "REFUSED" : "MISMATCH");
    } else {
        emit(a, "double", "MISMATCH");
    }

    {
        unsigned char stack_obj[16];
        before = cplus_alloc_invalid_free_count();
        cplus_free(stack_obj);
        after = cplus_alloc_invalid_free_count();
        emit(a, "invalid", (after > before) ? "REFUSED" : "MISMATCH");
    }

    errno = 0;
    p = cplus_malloc(TOTAL);
    if (p) {
        void *other = cplus_malloc(16u);
        write_payload_with_guard((unsigned char *)p);
        if (other) cplus_free(other);
        emit(a, "canary", guard_intact((unsigned char *)p) ? "CANARY_OK" : "MISMATCH");
        cplus_free(p);
    } else {
        emit(a, "canary", "MISMATCH");
    }
}

static void test_cplus_free_checked(void) {
    const char *a = "cplus_free_checked";
    void *p, *q;

    errno = 0;
    p = cplus_malloc(32u);
    if (p) {
        int rc = cplus_free_checked(&p);
        emit(a, "valid", (rc == 0 && p == NULL) ? "OK" : "MISMATCH");
    } else {
        emit(a, "valid", "MISMATCH");
    }

    errno = 0;
    p = cplus_malloc(32u);
    if (p) {
        void *alias = p;
        int rc1 = cplus_free_checked(&p); /* p -> NULL on success */
        q = alias;
        int rc2 = cplus_free_checked(&q); /* stale alias -> EINVAL, q -> NULL */
        emit(a, "double",
             (rc1 == 0 && rc2 == EINVAL && errno == EINVAL) ? "REFUSED" : "MISMATCH");
    } else {
        emit(a, "double", "MISMATCH");
    }

    {
        unsigned char stack_obj[16];
        q = stack_obj;
        errno = 0;
        emit(a, "invalid",
             (cplus_free_checked(&q) == EINVAL && errno == EINVAL)
                 ? "REFUSED" : "MISMATCH");
    }

    q = NULL;
    errno = 0;
    emit(a, "null", (cplus_free_checked(&q) == 0) ? "DEFINED" : "MISMATCH");

    errno = 0;
    p = cplus_malloc(TOTAL);
    if (p) {
        void *other = cplus_malloc(16u);
        write_payload_with_guard((unsigned char *)p);
        if (other) (void)cplus_free_checked(&other);
        emit(a, "canary", guard_intact((unsigned char *)p) ? "CANARY_OK" : "MISMATCH");
        cplus_free(p);
    } else {
        emit(a, "canary", "MISMATCH");
    }
}

static void test_cplus_aligned_alloc(void) {
    const char *a = "cplus_aligned_alloc";
    void *p;

    errno = 0;
    p = cplus_aligned_alloc(16u, 64u);
    if (p && (((uintptr_t)p & 15u) == 0u) && cplus_alloc_is_live(p)) {
        cplus_free(p);
        emit(a, "valid", "OK");
    } else {
        if (p) cplus_free(p);
        emit(a, "valid", "MISMATCH");
    }

    errno = 0;
    p = cplus_aligned_alloc(16u, SIZE_MAX); /* size not a multiple of 16 */
    emit(a, "overflow", (p == NULL && errno == EINVAL) ? "REFUSED" : "MISMATCH");
    if (p) cplus_free(p);

    errno = 0;
    p = cplus_aligned_alloc(16u, 0u); /* 0 % 16 == 0 -> defined on this host */
    emit(a, "zero",
         (p != NULL && (((uintptr_t)p & 15u) == 0u) && cplus_alloc_is_live(p))
             ? "DEFINED" : "MISMATCH");
    if (p) cplus_free(p);

    errno = 0;
    p = cplus_aligned_alloc(3u, 64u);
    emit(a, "bad_align", (p == NULL && errno == EINVAL) ? "REFUSED" : "MISMATCH");
    if (p) cplus_free(p);

    /* cplus_aligned_alloc delegates straight to host aligned_alloc() and,
     * unlike cplus_malloc/calloc/realloc, never consults the hardened
     * fail-injection counter (see libc/src/stdlib/stdlib_c17.c). Injection is
     * therefore not an observable dimension of THIS API -> BLOCKED on both
     * sides. A successful pointer must still be released. */
    cplus_alloc_fail_after(0);
    errno = 0;
    p = cplus_aligned_alloc(16u, 64u);
    cplus_alloc_fail_reset();
    emit(a, "inject", "BLOCKED");
    if (p) cplus_free(p);

    /* C11 aligned_alloc requires size % alignment == 0; TOTAL(72) is not a
     * multiple of 16, so use the next multiple (80) for the canary case. */
    errno = 0;
    p = cplus_aligned_alloc(16u, 80u);
    if (p) {
        write_payload_with_guard((unsigned char *)p);
        emit(a, "canary", guard_intact((unsigned char *)p) ? "CANARY_OK" : "MISMATCH");
        cplus_free(p);
    } else {
        emit(a, "canary", "MISMATCH");
    }
}

static void test_hardened_control_and_counters(void) {
    void *p;
    unsigned long before, after;
    unsigned char stack_obj[16];

    cplus_alloc_fail_after(0);
    errno = 0;
    p = cplus_malloc(32u);
    cplus_alloc_fail_reset();
    emit("cplus_alloc_fail_after", "inject",
         (p == NULL && errno == ENOMEM) ? "REFUSED" : "MISMATCH");
    if (p) cplus_free(p);

    cplus_alloc_fail_after(0);
    (void)cplus_malloc(8u);
    cplus_alloc_fail_reset();
    p = cplus_malloc(32u);
    emit("cplus_alloc_fail_reset", "reset", (p != NULL) ? "OK" : "MISMATCH");
    if (p) cplus_free(p);

    before = cplus_alloc_double_free_count();
    p = cplus_malloc(32u);
    if (p) {
        cplus_free(p);
        cplus_free(p);
        after = cplus_alloc_double_free_count();
        emit("cplus_alloc_double_free_count", "double",
             (after > before) ? "DETECTED" : "MISMATCH");
    } else {
        emit("cplus_alloc_double_free_count", "double", "MISMATCH");
    }

    before = cplus_alloc_invalid_free_count();
    cplus_free(stack_obj);
    after = cplus_alloc_invalid_free_count();
    emit("cplus_alloc_invalid_free_count", "invalid",
         (after > before) ? "DETECTED" : "MISMATCH");
}

int main(void) {
    /* SafeCRT */
    test_malloc_s();
    test_calloc_s();
    test_realloc_s();
    test_free_s();
    test_free_checked_s();
    test_aligned_alloc_s();
    test_safe_metadata_and_control();

    /* Hardened libc */
    test_cplus_malloc();
    test_cplus_calloc();
    test_cplus_realloc();
    test_cplus_free();
    test_cplus_free_checked();
    test_cplus_aligned_alloc();
    test_hardened_control_and_counters();
    return 0;
}
