/* Issue #47 (strict-only): an indirect call through a function pointer whose
 * target is not resolvable in this TU cannot be proven non-throwing while a
 * try-region owner is live. */
#include <stdio.h>
#include "cplus/cplus.h"

class Tracker {
    int id;
    Tracker(int id) { this->id = id; }
    void drop(void) { printf("DROP %d\n", this->id); }
};

extern void unknown_helper(void);

int main(void) {
    void (*fp)(void) = unknown_helper;
    try {
        Tracker t = Tracker(42);
        fp();
    } catch (Exception ex) {
        printf("CAUGHT %s\n", ex.message);
    }
    return 0;
}
