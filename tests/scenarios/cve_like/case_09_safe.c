/*
 * SAFE variant of case_09.c -- user-controlled VLA / stack clash.
 *
 * Baseline (original case_09.c):
 *   `uint64_t scratch[n]` sizes a STACK VLA from user input; `scratch[n-1]`
 *   then probes an attacker-chosen stack address. With len=100000000 the
 *   original dies with ASan `stack-overflow` (exit 134) while allocating the
 *   VLA (__asan_alloca_poison, case_09.c:7).
 *
 * Mitigation:
 *   - No user-controlled VLA anywhere in this file.
 *   - Explicit upper bound MAX_WINDOW; len==0 and len>MAX_WINDOW are rejected
 *     before any allocation (caller-side input validation).
 *   - The scratch window is an ordinary calloc() on the HEAP. The ordinary-C
 *     checked layers map it to an overflow-checked tracked allocator
 *     (calloc_s in stdlib/safe, cplus_calloc in the plain hardened libc), and
 *     map free() to the matching checked deallocator.
 *
 * OUTCOME: oversized lengths are refused; accepted lengths use a bounded,
 *          overflow-checked heap allocation, so no stack clash is reachable.
 *          HONEST LIMIT: stack-clash prevention is fundamentally a compiler /
 *          code-idiom issue (no user-sized VLAs, stack probing,
 *          -fstack-clash-protection). libc cannot patch a VLA the compiler has
 *          already emitted; this variant fixes the idiom, not "VLAs in general".
 */
#ifdef PLAIN_LIBC
#include "cplus_libc_safe.h"
#else
#include "stdlib/safe/safe_c.h"
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* 1 << 20 uint64_t elements == 8 MiB: a hard upper bound on heap scratch. */
#define MAX_WINDOW ((size_t)1u << 20)

static int process_window(size_t n, uint64_t *out)
{
    uint64_t *scratch;
    if (n == 0 || n > MAX_WINDOW) {
        return -1; /* caller-side bound: refuse before allocating */
    }
    scratch = (uint64_t *)calloc(n, sizeof(*scratch)); /* checked allocator */
    if (scratch == NULL) {
        return -2; /* overflow / OOM refused by the checked allocator */
    }
    scratch[0] = 1;
    scratch[n - 1] = 2;
    *out = scratch[0] + scratch[n - 1];
    free(scratch);
    return 0;
}

int main(void)
{
    size_t len = 0;
    uint64_t res = 0;
    int rc;

    printf("len: ");
    if (scanf("%zu", &len) != 1) {
        printf("OUTCOME: invalid length input\n");
        return 1;
    }

    rc = process_window(len, &res);
    if (rc == -1) {
        printf("OUTCOME: rejected oversized window (len=%zu, max=%zu)\n",
               len, MAX_WINDOW);
        return 2;
    }
    if (rc == -2) {
        printf("OUTCOME: checked calloc refused the window (len=%zu)\n", len);
        return 3;
    }

    printf("res: %llu\n", (unsigned long long)res);
    printf("OUTCOME: bounded heap window used safely (len=%zu)\n", len);
    return 0;
}
