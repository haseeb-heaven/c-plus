// EXPECT: REJECT
// Reason: the heap-allocated error slot is released and then its state is
// read. That is a use-after-free lifecycle violation; the C+ checker must
// prove the pointer is dead at the point of the member access.
#include "cplus/cplus.h"
#include "stdlib/error.hp"

int main(void) {
    error *slot = (error *)cplus_alloc(sizeof(error));
    error_reset(slot);
    cplus_release(slot);

    return slot->failed; /* use after release: member access on a dead slot */
}
