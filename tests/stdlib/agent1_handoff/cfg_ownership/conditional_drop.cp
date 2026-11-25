/* EXPECT: REJECT (tagged C+ diagnostic) -- HANDOFF: P0 CFG-based ownership.
 *
 * Scenario 1: conditional drop.  `s.drop()` executes only on the flag==true
 * edge.  At the join the owner is Live on the false edge and Released on the
 * true edge, so `s.at(0)` is a use-after-release on a feasible path and must
 * be rejected.  `string::at()` returns '\0' for a dead owner, so the program
 * has no runtime UB -- this is a static ownership violation only.
 *
 * Observed with ./build/c+ check --strict: rc=0, "ok", no diagnostic.
 * Classification: ACCEPT (false negative).  See NOTES.md.
 */
#include "cplus/cplus.h"
#include "stdlib/string.hp"

int main(void) {
    string s = string("abc");
    int flag = 1;
    if (flag)
        s.drop();
    char c = s.at(0); /* USE AFTER RELEASE on the flag==true path */
    print("c", c);
    return 0;
}
