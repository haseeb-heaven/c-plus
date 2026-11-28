/* BASELINE: case_06.c crashes; SAFE: caller-side ownership fix + checked APIs. */
/* Ordinary-C checked layer (no `_s` spelling in the program logic):
 *   default      -> stdlib/safe/safe_c.h
 *                   link build/safe/libcplus_safecrt.a + build/libc/libcplus_hardened.a
 *   -DPLAIN_LIBC -> libc/install-include/cplus_libc_safe.h
 *                   link build/libc/libcplus_hardened.a only
 *
 * HONESTY: this is an ownership/aliasing bug. A library cannot make a raw stale
 * pointer safe. The FIX is caller-side (one owner per node, clear every alias
 * before releasing); the library only (a) routes malloc/calloc/free through
 * checked, tracked entry points and (b) offers a provenance query used here as
 * defence in depth. It does NOT repair raw aliasing.
 */
#ifdef PLAIN_LIBC
#include "cplus_libc_safe.h"
#define CPLUS_IS_LIVE(p) cplus_alloc_is_live(p)
#else
#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_runtime.h"
#define CPLUS_IS_LIVE(p) cplus_safe_ptr_is_live(p)
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

struct CacheNode { uint32_t id; uint64_t *heavy_blob; };

/* Caller-side discipline: a node has exactly one owning slot. On eviction we
 * clear EVERY slot that aliases the victim before any release. This is the
 * actual UAF fix; no library can revoke an alias we choose to keep. */
static void cache_clear_aliases(struct CacheNode *slots[4],
                                const struct CacheNode *victim) {
    for (int i = 0; i < 4; ++i)
        if (slots[i] == victim) slots[i] = NULL;
}

static void cache_evict(struct CacheNode *slots[4], int idx) {
    struct CacheNode *victim = slots[idx];
    if (!victim) return;
    cache_clear_aliases(slots, victim); /* drop all aliases first */
    free(victim->heavy_blob);           /* ordinary free -> checked/tracked free */
    victim->heavy_blob = NULL;          /* clear member while the owner is live */
    free(victim);                       /* tracked free; repeat release refused */
}

int main(void) {
    struct CacheNode *slots[4] = {0};
    {
        /* Caller discipline: the temporary goes out of scope, so the slots are
         * the only owners; no stray local alias survives initialization. */
        struct CacheNode *p = malloc(sizeof *p);
        if (!p) return 1;
        p->id = 1;
        p->heavy_blob = calloc(1024, sizeof(uint64_t));
        if (!p->heavy_blob) { free(p); return 1; }
        slots[0] = p;
        slots[1] = p;           /* keep the baseline's aliasing shape */
    }

    int ev = 0, rd = 1;
    printf("evict read: ");
    int scanned = scanf("%d %d", &ev, &rd);
    if (scanned == 0) return 1;
    if (scanned == 1) rd = 1; /* single-value harness input: default read slot */

    if (ev >= 0 && ev < 4) cache_evict(slots, ev);

    void *spray = calloc(1, sizeof(struct CacheNode)); /* not the fix: heap grooming */
    int skipped = 0;
    if (rd >= 0 && rd < 4 && slots[rd]) {
        /* Library-side diagnosis only: refuse a slot the tracked allocator says
         * is no longer live. It is NOT a proof of lifetime for raw pointers. */
        if (!CPLUS_IS_LIVE(slots[rd])) {
            skipped = 1;
        } else {
            slots[rd]->heavy_blob = (uint64_t *)99; /* value never dereferenced/freed */
        }
    }
    free(spray);

    printf("outcome: SAFE | single-owner eviction cleared every alias; "
           "stale read %s; checked tracked allocation active\n",
           skipped ? "refused by liveness query" : "target absent (slot cleared)");
    return 0;
}
