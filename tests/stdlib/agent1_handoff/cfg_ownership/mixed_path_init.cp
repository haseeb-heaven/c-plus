/* EXPECT: REJECT (tagged C+ diagnostic) -- HANDOFF: P0 CFG-based ownership.
 *
 * Scenario 3: mixed-path initialization / release at a join point.  The
 * then-branch releases `s`; the else-branch keeps it live and appends to it.
 * The join merges {Released, Live} into MaybeReleased, so a use of `s` after
 * the join must be rejected on the released path.  `string::at()` is safe on
 * a dead owner, so there is no runtime UB in this reproducer.
 *
 * Observed with ./build/c+ check --strict: rc=0, "ok", no diagnostic.
 * Classification: ACCEPT (false negative).  See NOTES.md.
 */
#include "cplus/cplus.h"
#include "stdlib/string.hp"

int main(void) {
    string s = string("abc");
    int flag = 1;
    if (flag) {
        s.drop();
    } else {
        s.append("d");
    }
    /* join point: s is live on one incoming edge, released on the other */
    char c = s.at(0); /* USE AFTER RELEASE on the then-branch path */
    print("c", c);
    return 0;
}
