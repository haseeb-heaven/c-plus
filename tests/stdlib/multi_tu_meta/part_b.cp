/* TU B: include the same metadata header and allocate enough live blocks to
 * hit the grow/full-table path while TU A's tombstones are present.  With a
 * per-TU sentinel the tombstones look live, consume slots, and allocations
 * stop being recorded (size queries then return 0). */
#include <stddef.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"

static void *meta_b_blocks[500];

const void *meta_b_tomb(void) {
    return STDLIB_META_TOMB;
}

int meta_b_alloc_and_verify(void) {
    int i;
    for (i = 0; i < 500; i++) {
        size_t want = (size_t)(i + 1);
        void *p = stdlib_alloc(want);
        if (p == NULL || stdlib_alloc_size(p) != want)
            return 1;
        meta_b_blocks[i] = p;
    }
    for (i = 0; i < 500; i++) {
        stdlib_release(meta_b_blocks[i]);
        meta_b_blocks[i] = NULL;
    }
    return 0;
}
