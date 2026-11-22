// EXPECT: RUNTIME
// Reason: using a guard after its mutex has been dropped is safe only because
// mutex::drop() NULLs the handle and mutex::unlock() checks it before use. C+
// does not model that owner liveness relationship, so no compile-time proof is
// available; runtime design, not static checking, prevents the fault.
#include "cplus/cplus.h"
#include "stdlib/thread.hp"

int main(void) {
    mutex m = mutex();
    guard g = guard(&m);
    m.drop();            // releases the underlying OS mutex handle
    g.unlock();          // inert: m.handle is NULL after drop
    return 0;
}
