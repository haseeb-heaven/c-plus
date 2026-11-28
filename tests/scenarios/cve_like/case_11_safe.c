/*
 * SAFE variant of case_11.c -- arena `a->offset + bytes` overflow.
 *
 * Baseline (original case_11.c):
 *   `if (a->offset + bytes > a->size)` wraps in size_t for an attacker-chosen
 *   huge request (e.g. size=18446744073709551615), so the bound check is
 *   bypassed and the arena offset is corrupted. ASan: no report, exit 0.
 *   UBSan: "store to misaligned address ... requires 8 byte alignment" at
 *   case_11.c:10 -- the wrapped offset leaves the counter slot unaligned.
 *
 * Mitigation:
 *   - Caller-side arithmetic fix, with NO addition in the guard:
 *         if (bytes > a->size || a->offset > a->size - bytes) return NULL;
 *     This cannot wrap, so a huge request is refused before a pointer is
 *     handed out and the offset can never exceed the arena.
 *   - The trailing counter is written with an ordinary memcpy (no alignment
 *     requirement). The ordinary-C checked layers bound that copy against the
 *     tracked block capacity, so it also cannot pass the end of the arena.
 *
 * OUTCOME: overflow-sized requests are refused by the callers' guard; the
 *          counter write is bounded and alignment-safe (no genuine UB).
 */
#ifdef PLAIN_LIBC
#include "cplus_libc_safe.h"
#else
#include "stdlib/safe/safe_c.h"
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Arena {
    uint8_t *base;
    size_t size, offset;
};

static void *arena_push(struct Arena *a, size_t bytes)
{
    /* Overflow-free equivalent of (a->offset + bytes > a->size). */
    if (bytes > a->size || a->offset > a->size - bytes) {
        return NULL;
    }
    void *p = a->base + a->offset;
    a->offset += bytes;
    return p;
}

int main(void)
{
    struct Arena a = { .base = (uint8_t *)malloc(4096), .size = 4096, .offset = 64 };
    const uint64_t value = 123;
    size_t req = 0;
    uint8_t *c;
    void *slot;

    if (a.base == NULL) {
        printf("OUTCOME: arena allocation failed\n");
        return 1;
    }

    printf("size: ");
    if (scanf("%zu", &req) != 1) {
        printf("OUTCOME: invalid size input\n");
        free(a.base);
        return 1;
    }

    c = (uint8_t *)arena_push(&a, req);
    if (c == NULL) {
        printf("OUTCOME: request refused by overflow-safe arena guard (size=%zu)\n",
               req);
        free(a.base);
        return 2;
    }

    slot = arena_push(&a, sizeof(uint64_t));
    if (slot == NULL) {
        printf("OUTCOME: counter slot unavailable; no write performed\n");
        free(a.base);
        return 0;
    }

    /* memcpy is alignment-agnostic; the checked wrapper bounds the write. */
    memcpy(slot, &value, sizeof(value));
    printf("OUTCOME: bounded counter write completed (value=%llu)\n",
           (unsigned long long)value);

    free(a.base);
    return 0;
}
