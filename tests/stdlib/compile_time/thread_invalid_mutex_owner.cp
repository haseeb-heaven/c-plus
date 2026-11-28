// EXPECT: RUNTIME
// Reason: a guard over a zero-initialised (invalid) mutex is inert because
// mutex::lock/unlock check the handle before touching it. C+ does not prove
// owner validity at compile time, so this is a runtime-enforced property.
#include "cplus/cplus.h"
#include "stdlib/thread.hp"

int main(void) {
    mutex m;             // zero-initialised: no OS resource, is_valid() == 0
    guard g = guard(&m); // owner points at an invalid mutex
    int held = g.holds();
    int valid = m.is_valid();
    g.unlock();          // no-op: handle is NULL
    return (held && !valid) ? 0 : 1;
}
