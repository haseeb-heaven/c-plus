// EXPECT: REJECT
// Same guarded-resource pattern as 08, but the guard flag and resource pointer
// live in a by-value struct.  The guard (`ready`) is never consulted before the
// resource field (`slot`) is read on the fall-through path after an early
// return.  Definite initialization must reject the read.
//
// NOTE: the struct is zero-initialized today, so this is currently accepted
// (silent); at runtime the false path dereferences a NULL slot.
#include "cplus/cplus.h"
#include "stdlib/error.hp"

int g_condition;

struct Guard {
    int ready;
    error *slot;
};

int probe(void) {
    struct Guard guard;
    if (g_condition) {
        guard.slot = (error *)cplus_alloc(sizeof(error));
        guard.ready = 1;
        return guard.slot->failed; /* early return on the initialized path */
    }
    return guard.slot->failed; /* guard.ready never checked */
}

int main(void) { return probe(); }
