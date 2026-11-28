/* Issue #222 (part 1): the allocation metadata tombstone must be shared
 * across translation units.  TU A frees blocks (writing tombstones into the
 * shared meta_tab); TU B then grows/rehashes that table.  With a per-TU
 * static sentinel, TU B mistakes the tombstones for live keys, meta_used
 * saturates and later allocations are no longer recorded. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"

const void *meta_a_tomb(void);
const void *meta_b_tomb(void);
int meta_a_churn(void);
int meta_b_alloc_and_verify(void);

int main(void) {
    size_t live_before = stdlib_state.meta_live;
    int a;
    int b;
    print("tomb-shared", meta_a_tomb() == meta_b_tomb(),
          STDLIB_META_TOMB == meta_a_tomb());
    a = meta_a_churn();
    b = meta_b_alloc_and_verify();
    print("cross-tu-meta", a == 0, b == 0,
          stdlib_state.meta_live == live_before);
    return (a == 0 && b == 0 &&
            stdlib_state.meta_live == live_before &&
            meta_a_tomb() == meta_b_tomb()) ? 0 : 1;
}
