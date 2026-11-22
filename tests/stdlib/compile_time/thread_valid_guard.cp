// EXPECT: ACCEPT
// Reason: a guard over a live mutex acquires the critical section and releases
// it exactly once; the owner stays valid for the whole guard lifetime. This is
// the canonical valid RAII shape for stdlib/thread.hp.
#include "cplus/cplus.h"
#include "stdlib/thread.hp"

int main(void) {
    mutex m = mutex();
    guard g = guard(&m);
    if (!g.holds())
        return 1;
    g.unlock();
    return g.holds() ? 1 : 0;
}
