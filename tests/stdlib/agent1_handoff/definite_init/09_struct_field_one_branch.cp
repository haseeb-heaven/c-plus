// EXPECT: REJECT
// The uninitialized value is a struct field rather than a local scalar: the
// field is written on only one branch and read after the join.  Definite
// initialization should look through aggregates the same way it looks through
// locals.
//
// NOTE: C+ zero-initializes by-value structs, so this is currently accepted
// (silent) even though no explicit initializer reaches the read.
#include "cplus/cplus.h"
#include "stdlib/error.hp"

int g_condition;

struct Holder {
    error *slot;
};

int main(void) {
    struct Holder holder;
    if (g_condition)
        holder.slot = (error *)cplus_alloc(sizeof(error));
    return holder.slot->failed; /* field uninitialized on the false path */
}
