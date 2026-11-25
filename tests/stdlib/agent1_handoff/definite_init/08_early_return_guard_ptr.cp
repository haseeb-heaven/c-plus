// EXPECT: REJECT
// An early return on the initialized path leaves the guard flag (`ready`) and
// the guarded resource pointer (`slot`) uninitialized on the fall-through
// path.  Reading either one afterwards is a definite-initialization error.
#include "cplus/cplus.h"
#include "stdlib/error.hp"

int g_condition;

int probe(void) {
    error *slot;
    int ready;
    if (g_condition) {
        slot = (error *)cplus_alloc(sizeof(error));
        ready = 1;
        return slot->failed; /* early return on the initialized path */
    }
    if (ready)                   /* guard flag itself is uninitialized here */
        return slot->failed;     /* guarded resource also uninitialized */
    return 0;
}

int main(void) { return probe(); }
