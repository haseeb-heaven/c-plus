// EXPECT: RUNTIME
// Reason: a guard constructed with a NULL owner holds no critical section, so
// unlock() is a guarded no-op. C+ has no ownership state for a guard, so the
// "unlock without ownership" property is only enforceable at runtime.
#include "cplus/cplus.h"
#include "stdlib/thread.hp"

int main(void) {
    guard g = guard(NULL);
    g.unlock();          // no-op: no owner to release
    return g.holds() ? 1 : 0;
}
