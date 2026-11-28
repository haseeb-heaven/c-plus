/* Demo: RAII mutex guard, including the null and zero-initialised cases. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/thread.hp"

static long counter = 0;

int main(void) {
    mutex lock = mutex();
    if (!lock.is_valid()) {
        print("no mutex");
        return 1;
    }

    int i;
    for (i = 0; i < 1000; i++) {
        guard held = guard(&lock);
        if (held.holds()) {
            counter++;
            held.unlock();
        }
    }
    print("counter", counter);

    lock.lock();
    lock.unlock();

    guard none = guard(NULL);
    print("null guard", none.holds(), lock.is_valid(), lock.native() != NULL);

    mutex empty;
    print("zero-init mutex", empty.is_valid());
    return 0;
}
