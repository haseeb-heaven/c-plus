/* Issue #47 (strict-only): a call to a function declared extern in this TU
 * cannot be proven non-throwing while a try-region owner is live. */
#include <stdio.h>
#include "cplus/cplus.h"

class Tracker {
    int id;
    Tracker(int id) { this->id = id; }
    void drop(void) { printf("DROP %d\n", this->id); }
};

extern void throwing_helper(void);

int main(void) {
    try {
        Tracker t = Tracker(42);
        throwing_helper();
    } catch (Exception ex) {
        printf("CAUGHT %s\n", ex.message);
    }
    return 0;
}
