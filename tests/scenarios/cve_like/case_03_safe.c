/* BASELINE: case_03.c crashes; SAFE: defined refusal/valid result. */
/* Ordinary C APIs plus either safe header:
 *   default     : "stdlib/safe/safe_c.h"          (SafeCRT layer)
 *   -DPLAIN_LIBC: "cplus_libc_safe.h"             (plain hardened-libc layer)
 * The BASELINE assigns realloc()'s result directly to the only pointer that
 * owns the block, overwrites it with a forged address, then frees the forgery.
 * Here realloc() always goes through a temporary, failure is checked BEFORE the
 * owner is replaced, no forged address is ever stored, and only live pointers
 * are freed.  The failure path is exercised deterministically with this
 * allocator's failure injection (it preserves the original block).
 */
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>

#ifdef PLAIN_LIBC
#include "cplus_libc_safe.h"
#define CPLUS_FAIL_AFTER(n)  cplus_alloc_fail_after((n))
#define CPLUS_FAIL_RESET()   cplus_alloc_fail_reset()
typedef cplus_alloc_meta cplus_meta_t;
static int cplus_meta_get(const void *p, cplus_meta_t *m) {
    return cplus_alloc_get_meta(p, m);
}
#else
#include "stdlib/safe/safe_c.h"
#define CPLUS_FAIL_AFTER(n)  cplus_safe_alloc_fail_after((n))
#define CPLUS_FAIL_RESET()   cplus_safe_alloc_fail_reset()
typedef cplus_safe_alloc_meta cplus_meta_t;
static int cplus_meta_get(const void *p, cplus_meta_t *m) {
    return cplus_safe_alloc_get_meta(p, m);
}
#endif

struct DynamicTable { size_t capacity; uint64_t *entries; };

int main(void) {
    struct DynamicTable t = {
        .capacity = 16,
        .entries = (uint64_t *)calloc(16, sizeof(uint64_t))
    };
    if (!t.entries) { puts("result: refusal - initial allocation failed"); return 1; }

    uint64_t nc = 0;
    printf("cap: ");
    if (scanf("%" SCNu64, &nc) != 1) { puts("result: refusal - bad input"); return 1; }

    /* Byte-count overflow guard: keeps the request inside a representable,
     * safe size before it ever reaches realloc(). */
    if (nc > (uint64_t)((SIZE_MAX >> 1) / sizeof(uint64_t))) {
        puts("result: refusal - requested capacity overflows a safe byte count");
        free(t.entries);
        return 0;
    }

    /* (1) Verify the allocator's failure preservation on a scratch block: an
     * injected realloc failure must leave the original pointer/contents intact.
     * The scratch block keeps this demonstration separate from the real table. */
    uint64_t *scratch = (uint64_t *)calloc(4, sizeof(uint64_t));
    if (!scratch) { puts("result: refusal - scratch allocation failed"); free(t.entries); return 1; }
    cplus_meta_t meta;
    unsigned long long gen_before = 0;
    scratch[0] = UINT64_C(0x1122334455667788);
    if (cplus_meta_get(scratch, &meta) == 0) gen_before = meta.generation;

    CPLUS_FAIL_AFTER(0); /* next allocation through this API must fail */
    uint64_t *tmp = (uint64_t *)realloc(scratch, 32 * sizeof(uint64_t));
    CPLUS_FAIL_RESET();  /* never leave injection armed for later calls */

    if (tmp == NULL) {
        unsigned long long gen_after = 0;
        if (cplus_meta_get(scratch, &meta) == 0) gen_after = meta.generation;
        if (scratch[0] == UINT64_C(0x1122334455667788) &&
            gen_before != 0 && gen_after == gen_before) {
            puts("result: refusal - injected realloc failure; original pointer,");
            puts("        contents and generation preserved (owner not overwritten)");
        } else {
            puts("result: refusal - realloc failed; original block left untouched");
        }
    } else {
        /* Injection did not fire (defensive): the temporary owns the grown block. */
        puts("result: note - injection did not fire; scratch grown via temporary");
        scratch = tmp;
    }
    free(scratch); /* only the known-valid scratch pointer is released */

    /* (2) Real resize: the temporary is checked before the owner is replaced. */
    uint64_t *grown = (uint64_t *)realloc(t.entries, (size_t)nc * sizeof(uint64_t));
    if (grown == NULL) {
        puts("result: refusal - realloc failed; original block still owned and freed");
        free(t.entries); /* still valid: realloc failure preserved it */
        return 0;
    }
    t.entries = grown;
    t.capacity = (size_t)nc;
    printf("result: ok capacity=%zu (new base used; no forged address)\n", t.capacity);
    free(t.entries); /* only a valid, tracked base pointer is freed */
    return 0;
}
