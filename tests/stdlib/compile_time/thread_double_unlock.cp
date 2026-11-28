// EXPECT: RUNTIME
// Reason: unlocking the same guard twice is silently inert because guard::unlock()
// clears its owner pointer on the first call. C+ does not model guard ownership
// state, so this safety property can only be observed at runtime, not proven at
// compile time.
#include "cplus/cplus.h"
#include "stdlib/thread.hp"

int main(void) {
    mutex m = mutex();
    guard g = guard(&m);
    g.unlock();
    g.unlock();   // second unlock: no-op, owner already cleared
    return 0;
}
