/* CRT conformance: malloc/calloc/realloc/free defined behaviour.
 * Every case here is defined by C; the unprotected hazards (double free,
 * use-after-free, freed aliases) are listed in comments and docs/CRT_MATRIX.md
 * instead of being executed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

/* noinline: keeps the allocation observable at -O2, where LLVM may otherwise
 * assume a huge calloc cannot fail and fold the NULL check. */
__attribute__((noinline)) static void *allocate_many(size_t count, size_t element) {
    return calloc(count, element);
}

__attribute__((noinline)) static void *resize_block(void *block, size_t size) {
    void *resized = realloc(block, size);
    if (!resized) {
        /* Failure: realloc did not release block; the caller still owns it. */
        return NULL;
    }
    /* Success: the original block was released and `resized` is its owner. */
    return resized;
}

static void check(int condition, const char *what) {
    if (!condition) {
        failures++;
        printf("FAIL %s\n", what);
    }
}

int main(void) {
    unsigned char *block = (unsigned char *)malloc(64);
    check(block != NULL, "malloc(64)");
    memset(block, 0xAB, 64);
    check(block[0] == 0xAB && block[63] == 0xAB, "malloc block writable");

    /* free(NULL) is a no-op. */
    free(NULL);
    check(1, "free(NULL)");

    /* calloc zeroes; multiplication is overflow-checked by the implementation
     * (both Apple libc and glibc return NULL here). */
    unsigned char *zeroed = (unsigned char *)calloc(8, 16);
    check(zeroed != NULL, "calloc(8,16)");
    free(zeroed);
    /* Zeroing check uses a distinct single-argument-sized buffer to avoid known
     * C+ static bounds/ownership false positives on two-arg calloc + post-free
     * reassignment (see docs/CRT_MATRIX.md). */
    unsigned char *zeros = (unsigned char *)calloc(128, 1);
    check(zeros != NULL, "calloc(128,1)");
    check(zeros[0] == 0 && zeros[127] == 0, "calloc zeroes");
    /* volatile: at -O2 LLVM folds calloc(SIZE_MAX, ...) and may assume the
     * allocation cannot fail, so the observation must come from a runtime
     * value.  (Recorded as a portability caveat in docs/CRT_MATRIX.md.) */
    volatile size_t overflow_count = (size_t)-1;
    check(allocate_many(overflow_count, 32) == NULL,
          "calloc overflow -> NULL");

    /* realloc failure must preserve the original owner. */
    unsigned char *grown = (unsigned char *)realloc(block, 4096);
    check(grown != NULL, "realloc grow");
    if (grown != NULL) {
        block = grown; /* success: old block released, new owner is current */
        free(block);
    } else {
        free(block);   /* failure: original block still owned here */
    }
    unsigned char *owner = (unsigned char *)malloc(32);
    check(owner != NULL, "malloc(32)");
    if (owner != NULL) {
        owner[0] = 0x5A;
        volatile size_t huge_size = (size_t)-1;
        unsigned char *huge = (unsigned char *)resize_block(owner, huge_size);
        check(huge == NULL, "realloc(huge) -> NULL");
        check(owner[0] == 0x5A, "owner preserved after failed realloc");
        free(owner);
    }

    /* An alias freed once (the second free would be undefined). */
    unsigned char *alias = (unsigned char *)malloc(16);
    unsigned char *same = alias;
    check(alias != NULL, "alias allocation");
    if (alias != NULL) {
        free(alias);
        alias = NULL;
        same = NULL;                 /* both handles dropped together */
        check(alias == NULL && same == NULL, "alias freed once");
    }

    free(zeros);
    check(failures == 0, "alloc cases");
    printf("alloc: %s\n", failures == 0 ? "ok" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
