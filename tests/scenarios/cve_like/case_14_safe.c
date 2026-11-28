/* BASELINE: case_14.c crashes; SAFE: defined refusal/valid result. */
/* Ordinary C APIs plus either safe header:
 *   default     : "stdlib/safe/safe_c.h"          (SafeCRT layer)
 *   -DPLAIN_LIBC: "cplus_libc_safe.h"             (plain hardened-libc layer)
 * The BASELINE adds skip to the cursor, resets only on exact equality with
 * capacity, then indexes the ring with an out-of-range cursor (heap-buffer-
 * overflow for e.g. skip=100).  Here the capacity is validated, the skip is
 * validated against overflow, the cursor wraps with modulo, and the index is
 * checked to be in range before the access.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#ifdef PLAIN_LIBC
#include "cplus_libc_safe.h"
#else
#include "stdlib/safe/safe_c.h"
#endif

struct Ring { uint64_t *slots; size_t capacity, cursor; };

int main(void) {
    struct Ring r = {
        .slots = (uint64_t *)calloc(32, sizeof(uint64_t)),
        .capacity = 32,
        .cursor = 0
    };
    if (!r.slots) { puts("result: refusal - initial allocation failed"); return 1; }

    size_t skip = 0;
    printf("skip: ");
    if (scanf("%zu", &skip) != 1) { puts("result: refusal - bad input"); return 1; }

    /* Validate the modulus and the addition before either is used. */
    if (r.capacity == 0) {
        puts("result: refusal - ring capacity is zero");
        free(r.slots);
        return 0;
    }
    if (skip > SIZE_MAX - r.cursor) {
        puts("result: refusal - cursor + skip overflows");
        free(r.slots);
        return 0;
    }

    /* Modulo wrapping instead of equality-only reset. */
    r.cursor = (r.cursor + skip) % r.capacity;

    /* Independently verify the index is in range before accessing. */
    if (r.cursor >= r.capacity) {
        puts("result: refusal - cursor out of range");
        free(r.slots);
        return 0;
    }
    printf("slot: %llu\n", (unsigned long long)r.slots[r.cursor]);
    printf("result: ok cursor=%zu (modulo wrap, in-range access)\n", r.cursor);
    free(r.slots);
    return 0;
}
