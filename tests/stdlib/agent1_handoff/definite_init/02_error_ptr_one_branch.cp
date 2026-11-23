// EXPECT: REJECT
// A raw `error *` owner is assigned only on the true branch, then read after
// the join.  The false path leaves it indeterminate; definite initialization
// must reject the member read.
#include "cplus/cplus.h"
#include "stdlib/error.hp"

int g_condition; /* models a runtime decision */

int main(void) {
    error *slot;
    if (g_condition)
        slot = (error *)cplus_alloc(sizeof(error));
    return slot->failed; /* uninitialized on the false path */
}
