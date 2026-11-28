// OP: function-pointer call with an unproven target.
// The table index comes from a volatile value, so the callee of the indirect
// call cannot be proven at compile time. This is the dispatch-bounds shape
// documented in docs/libc/DISPATCH_BOUNDS_HANDOFF.md.
#include "cplus/cplus.h"

typedef int (*handler_fn)(void);

static int h0(void) { return 0; }
static int h1(void) { return 1; }
static int h2(void) { return 2; }

int main(void) {
    handler_fn table[3] = {h0, h1, h2};
    volatile int cmd = 99;              /* unproven selector */
    handler_fn f = table[cmd];          /* target not provably in range */
    return f();                         /* indirect call via unproven target */
}
