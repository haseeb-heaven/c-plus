/* TU A: allocate 200 blocks then free them all, leaving 200 tombstones in
 * the shared meta_tab.  No further allocations here, so TU A never rehashes
 * those tombstones away: TU B must understand them. */
#include <stddef.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"

static void *meta_a_blocks[200];

const void *meta_a_tomb(void) {
    return STDLIB_META_TOMB;
}

int meta_a_churn(void) {
    int i;
    for (i = 0; i < 200; i++) {
        size_t want = (size_t)(i + 1);
        void *p = stdlib_alloc(want);
        if (p == NULL || stdlib_alloc_size(p) != want)
            return 1;
        meta_a_blocks[i] = p;
    }
    for (i = 0; i < 200; i++) {
        stdlib_release(meta_a_blocks[i]);
        meta_a_blocks[i] = NULL;
    }
    return 0;
}
