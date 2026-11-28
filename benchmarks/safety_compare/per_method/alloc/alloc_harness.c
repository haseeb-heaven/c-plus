/* Deterministic per-method memory-safety harness for the C+ checked
 * (SafeCRT) allocation APIs.
 *
 * Prints one line per case:  <method> <case> <token>
 *
 * Tokens:
 *   OK         operation succeeded (valid alloc/free)
 *   REFUSED    dangerous operation refused (overflow / double / invalid)
 *   DEFINED    zero-size / null no-op has defined behavior
 *   PRESERVED  failure injection returned NULL and left the original live
 *   CANARY_OK  guard canary survived all operations intact
 *   MISMATCH   observed behavior disagreed with the documented contract
 *
 * No raw game/app memory; only the checked allocation API is exercised.
 * Link: build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stdlib/safe/safe_alloc.h"

static void emit(const char *method, const char *testcase, const char *token) {
    printf("%s %s %s\n", method, testcase, token);
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

/* ------------------------------------------------------------------ malloc_s */
static void test_malloc_s(void) {
    const char *m = "malloc_s";
    void *p;

    errno = 0;
    p = malloc_s(TOTAL);
    if (p && cplus_safe_ptr_is_live(p)) {
        write_payload_with_guard((unsigned char *)p);
        free_s(p);
        emit(m, "valid", "OK");
    } else {
        if (p) free_s(p);
        emit(m, "valid", "MISMATCH");
    }

    errno = 0;
    p = malloc_s(SIZE_MAX); /* > RSIZE_MAX_MEM */
    emit(m, "overflow", (p == NULL && errno == EINVAL) ? "REFUSED" : "MISMATCH");
    if (p) free_s(p);

    errno = 0;
    p = malloc_s(0);
    emit(m, "zero",
         (p != NULL && cplus_safe_ptr_is_live(p) && cplus_safe_ptr_size(p) == 1u)
             ? "DEFINED"
             : "MISMATCH");
    if (p) free_s(p);

    cplus_safe_alloc_fail_after(0); /* next allocation fails deterministically */
    errno = 0;
    p = malloc_s(TOTAL);
    cplus_safe_alloc_fail_reset();
    emit(m, "inject", (p == NULL && errno == ENOMEM) ? "REFUSED" : "MISMATCH");
    if (p) free_s(p);

    errno = 0;
    p = malloc_s(TOTAL);
    if (p) {
        write_payload_with_guard((unsigned char *)p);
        emit(m, "canary", guard_intact((unsigned char *)p) ? "CANARY_OK" : "MISMATCH");
        free_s(p);
    } else {
        emit(m, "canary", "MISMATCH");
    }
}

/* ------------------------------------------------------------------ calloc_s */
static void test_calloc_s(void) {
    const char *m = "calloc_s";
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
        emit(m, "valid", zeroed ? "OK" : "MISMATCH");
    } else {
        emit(m, "valid", "MISMATCH");
    }

    errno = 0;
    p = calloc_s(SIZE_MAX, 4u); /* multiply overflow */
    emit(m, "overflow", (p == NULL && errno == EOVERFLOW) ? "REFUSED" : "MISMATCH");
    if (p) free_s(p);

    errno = 0;
    p = calloc_s(0u, 8u);
    emit(m, "zero",
         (p != NULL && cplus_safe_ptr_is_live(p) && cplus_safe_ptr_size(p) == 1u)
             ? "DEFINED"
             : "MISMATCH");
    if (p) free_s(p);

    cplus_safe_alloc_fail_after(0);
    errno = 0;
    p = calloc_s(4u, 16u);
    cplus_safe_alloc_fail_reset();
    emit(m, "inject", (p == NULL && errno == ENOMEM) ? "REFUSED" : "MISMATCH");
    if (p) free_s(p);

    errno = 0;
    p = calloc_s(TOTAL / 8u, 8u); /* 9 * 8 = 72 */
    if (p) {
        write_payload_with_guard((unsigned char *)p);
        emit(m, "canary", guard_intact((unsigned char *)p) ? "CANARY_OK" : "MISMATCH");
        free_s(p);
    } else {
        emit(m, "canary", "MISMATCH");
    }
}

/* ----------------------------------------------------------------- realloc_s */
static void test_realloc_s(void) {
    const char *m = "realloc_s";
    void *p, *q;

    errno = 0;
    p = malloc_s(32u);
    if (p) {
        q = realloc_s(p, 256u);
        if (q && cplus_safe_ptr_is_live(q) && cplus_safe_ptr_size(q) == 256u) {
            free_s(q);
            emit(m, "valid", "OK");
        } else {
            if (q) free_s(q); else free_s(p);
            emit(m, "valid", "MISMATCH");
        }
    } else {
        emit(m, "valid", "MISMATCH");
    }

    errno = 0;
    p = malloc_s(32u);
    if (p) {
        q = realloc_s(p, SIZE_MAX);
        emit(m, "overflow",
             (q == NULL && errno == EINVAL && cplus_safe_ptr_is_live(p) &&
              cplus_safe_ptr_size(p) == 32u)
                 ? "REFUSED"
                 : "MISMATCH");
        free_s(p);
    } else {
        emit(m, "overflow", "MISMATCH");
    }

    errno = 0;
    p = malloc_s(32u);
    if (p) {
        q = realloc_s(p, 0u);
        emit(m, "zero",
             (q != NULL && cplus_safe_ptr_is_live(q) && cplus_safe_ptr_size(q) == 1u)
                 ? "DEFINED"
                 : "MISMATCH");
        if (q) free_s(q); else free_s(p);
    } else {
        emit(m, "zero", "MISMATCH");
    }

    errno = 0;
    p = malloc_s(32u);
    if (p) {
        cplus_safe_alloc_fail_after(0);
        q = realloc_s(p, 256u);
        cplus_safe_alloc_fail_reset();
        emit(m, "inject",
             (q == NULL && errno == ENOMEM && cplus_safe_ptr_is_live(p) &&
              cplus_safe_ptr_size(p) == 32u)
                 ? "PRESERVED"
                 : "MISMATCH");
        free_s(p);
    } else {
        emit(m, "inject", "MISMATCH");
    }

    {
        unsigned char stack_obj[16];
        errno = 0;
        q = realloc_s(stack_obj, 32u); /* not owned by this allocator */
        emit(m, "invalid", (q == NULL && errno == EINVAL) ? "REFUSED" : "MISMATCH");
        if (q) free_s(q);
    }

    errno = 0;
    p = malloc_s(TOTAL);
    if (p) {
        write_payload_with_guard((unsigned char *)p);
        q = realloc_s(p, TOTAL);
        emit(m, "canary",
             (q != NULL && guard_intact((unsigned char *)q)) ? "CANARY_OK" : "MISMATCH");
        if (q) free_s(q); else free_s(p);
    } else {
        emit(m, "canary", "MISMATCH");
    }
}

/* ------------------------------------------------------------- free_checked_s */
static void test_free_checked_s(void) {
    const char *m = "free_checked_s";
    void *p;

    errno = 0;
    p = malloc_s(32u);
    if (p) {
        int rc = free_checked_s(p);
        emit(m, "valid",
             (rc == CPLUS_SAFE_OK && !cplus_safe_ptr_is_live(p)) ? "OK" : "MISMATCH");
    } else {
        emit(m, "valid", "MISMATCH");
    }

    errno = 0;
    p = malloc_s(32u);
    if (p) {
        void *alias = p; /* stale alias */
        int rc1 = free_checked_s(p);
        int rc2 = free_checked_s(alias);
        emit(m, "double",
             (rc1 == CPLUS_SAFE_OK && rc2 == CPLUS_SAFE_EINVAL && errno == EINVAL)
                 ? "REFUSED"
                 : "MISMATCH");
    } else {
        emit(m, "double", "MISMATCH");
    }

    {
        unsigned char stack_obj[16];
        errno = 0;
        emit(m, "invalid",
             (free_checked_s(stack_obj) == CPLUS_SAFE_EINVAL && errno == EINVAL)
                 ? "REFUSED"
                 : "MISMATCH");
    }

    errno = 0;
    emit(m, "null", (free_checked_s(NULL) == CPLUS_SAFE_OK) ? "DEFINED" : "MISMATCH");

    errno = 0;
    p = malloc_s(TOTAL);
    if (p) {
        void *other = malloc_s(16u);
        write_payload_with_guard((unsigned char *)p);
        if (other) (void)free_checked_s(other);
        emit(m, "canary", guard_intact((unsigned char *)p) ? "CANARY_OK" : "MISMATCH");
        free_s(p);
    } else {
        emit(m, "canary", "MISMATCH");
    }
}

/* ------------------------------------------------------------- aligned_alloc_s */
static void test_aligned_alloc_s(void) {
    const char *m = "aligned_alloc_s";
    void *p;

    errno = 0;
    p = aligned_alloc_s(16u, 64u);
    if (p && (((uintptr_t)p & 15u) == 0u) && cplus_safe_ptr_is_live(p)) {
        free_s(p);
        emit(m, "valid", "OK");
    } else {
        if (p) free_s(p);
        emit(m, "valid", "MISMATCH");
    }

    errno = 0;
    p = aligned_alloc_s(16u, SIZE_MAX);
    emit(m, "overflow", (p == NULL && errno == EINVAL) ? "REFUSED" : "MISMATCH");
    if (p) free_s(p);

    errno = 0;
    p = aligned_alloc_s(16u, 0u);
    emit(m, "zero",
         (p != NULL && cplus_safe_ptr_is_live(p) && cplus_safe_ptr_size(p) == 1u)
             ? "DEFINED"
             : "MISMATCH");
    if (p) free_s(p);

    errno = 0;
    p = aligned_alloc_s(3u, 64u); /* not a power of two, < sizeof(void*) check too */
    emit(m, "bad_align", (p == NULL && errno == EINVAL) ? "REFUSED" : "MISMATCH");
    if (p) free_s(p);

    cplus_safe_alloc_fail_after(0);
    errno = 0;
    p = aligned_alloc_s(16u, 64u);
    cplus_safe_alloc_fail_reset();
    emit(m, "inject", (p == NULL && errno == ENOMEM) ? "REFUSED" : "MISMATCH");
    if (p) free_s(p);

    errno = 0;
    p = aligned_alloc_s(16u, TOTAL);
    if (p) {
        write_payload_with_guard((unsigned char *)p);
        emit(m, "canary", guard_intact((unsigned char *)p) ? "CANARY_OK" : "MISMATCH");
        free_s(p);
    } else {
        emit(m, "canary", "MISMATCH");
    }
}

int main(void) {
    test_malloc_s();
    test_calloc_s();
    test_realloc_s();
    test_free_checked_s();
    test_aligned_alloc_s();
    return 0;
}
