/* EXPECT: REJECT (tagged C+ diagnostic) -- HANDOFF: P0 CFG-based ownership.
 *
 * Scenario 4: loop-carried release.  The release is nested in an `if` inside
 * the loop body, so the loop exit merges {Released (i==0 taken), Live} into
 * MaybeReleased.  A CFG analysis needs a fixed point over the loop back-edge
 * to carry the release out of the loop.  `s.at(0)` after the loop must be
 * rejected.  `string::at()` is safe on a dead owner: no runtime UB here.
 *
 * Observed with ./build/c+ check --strict: rc=0, "ok", no diagnostic.
 * Classification: ACCEPT (false negative).  See NOTES.md.
 */
#include "cplus/cplus.h"
#include "stdlib/string.hp"

int main(void) {
    string s = string("abc");
    for (int i = 0; i < 2; i++) {
        if (i == 0)
            s.drop();
    }
    char c = s.at(0); /* USE AFTER RELEASE carried out of the loop */
    print("c", c);
    return 0;
}
