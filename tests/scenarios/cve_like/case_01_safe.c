/* BASELINE: case_01.c crashes; SAFE: this file reports a defined refusal. */
/* Layer: default = SafeCRT ordinary C names (stdlib/safe/safe_c.h).
 *        -DPLAIN_LIBC = standalone hardened libc (cplus_libc_safe.h).
 * Set cplus_runtime_set_violation_trap(0) under SafeCRT so a capacity refusal
 * is returned as NULL/ERANGE instead of being turned into _exit(70); this is
 * the documented mode for inspecting refusal return values. */
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <inttypes.h>
#ifdef PLAIN_LIBC
#include "cplus_libc_safe.h"
#else
#include "stdlib/safe/safe_c.h"
#include "safe_runtime.h"
#endif

struct Batch { uint32_t item_count; uint8_t payload[]; };

int main(void) {
    uint64_t count = 0;

#ifdef CPLUS_SAFE_C_ENABLE
    cplus_runtime_set_violation_trap(0);
#endif

    printf("count: ");
    if (scanf("%" SCNu64, &count) != 1) {
        printf("case_01: refused (no valid count)\n");
        return 0;
    }
    if (count == 0) {
        printf("case_01: refused (zero count)\n");
        return 0;
    }

    /* Caller check the raw C version omits: the original truncates the
     * 64-bit count to uint32_t, allocates for the truncated value, then
     * loops over the ORIGINAL 64-bit count -> heap overflow.  A correct
     * program must reject any value that cannot survive the narrowing. */
    if (count > (uint64_t)UINT32_MAX) {
        printf("case_01: refused (count exceeds 32-bit; 64->32 truncation)\n");
        return 0;
    }
    uint32_t items = (uint32_t)count;

    /* Caller check: use overflow-checked multiplication.  calloc(n,size)
     * (calloc_s / cplus_calloc) rejects a wrapping n*size product with
     * EOVERFLOW, so a 32-bit size_t build cannot silently under-allocate.
     * One element = the header plus one 32-byte payload slot. */
    struct Batch *b = calloc((size_t)items, sizeof(struct Batch) + 32u);
    if (!b) {
        printf("case_01: refused (allocation overflow or OOM)\n");
        return 0;
    }
    b->item_count = items;

    /* Still an ordinary hardened memset: the library resolves the interior
     * pointer b->payload + i*32 to the remaining tracked capacity, so an
     * out-of-range slot is refused (ERANGE) rather than written. */
    for (uint32_t i = 0; i < items; ++i) {
        memset(b->payload + (size_t)i * 32u, 0xAA, 32u);
    }
    free(b);
    printf("case_01: ok (items=%u)\n", items);
    return 0;
}
