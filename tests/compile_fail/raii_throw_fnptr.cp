/* Issue #47: an indirect call through a function pointer bound to a helper
 * that throws must not silently skip the live RAII owner's destructor. */
#include <stdio.h>
#include "cplus/cplus.h"

class Tracker {
    int id;
    Tracker(int id) { this->id = id; }
    void drop(void) { printf("DROP %d\n", this->id); }
};

static void throwing_fn(void) {
    throw "oops";
}

int main(void) {
    void (*fn_ptr)(void) = throwing_fn;
    try {
        Tracker t = Tracker(42);
        fn_ptr();
    } catch (Exception ex) {
        printf("CAUGHT %s\n", ex.message);
    }
    return 0;
}
