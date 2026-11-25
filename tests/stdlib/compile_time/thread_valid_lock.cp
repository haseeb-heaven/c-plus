// EXPECT: ACCEPT
// Reason: a lock/unlock pair on a live mutex is the valid manual-locking shape;
// the handle is valid for both calls and is released exactly once.
#include "cplus/cplus.h"
#include "stdlib/thread.hp"

int main(void) {
    mutex m = mutex();
    m.lock();
    m.unlock();
    return m.is_valid() ? 0 : 1;
}
