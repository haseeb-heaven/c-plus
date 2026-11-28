/* EXPECT: REJECT (tagged C+ diagnostic) -- HANDOFF: P0 CFG-based ownership.
 *
 * Scenario 2: conditional move.  `b = cplus_move(a)` executes only on the
 * flag==true edge.  At the join `a` is MovedFrom on that edge and Live on the
 * false edge (MaybeMovedFrom), while `b` is MaybeOwning.  `a.size()` must be
 * rejected on the may-moved path.  `vector::size()` is safe on a zeroed
 * moved-from owner, so this is a static ownership violation only, not UB.
 *
 * Observed with ./build/c+ check --strict: rc=1, UNTAGGED
 * "use-after-move: use of moved-from resource 'a'" (no [CPLUS- prefix).
 * Classification: GAP-UNTAGGED.  The current checker is path-insensitive and
 * also mis-locates the diagnostic.  See NOTES.md.
 */
#include "cplus/cplus.h"
#include "stdlib/vector.hp"

int main(void) {
    vector a = vector();
    a.push_back(3);
    vector b = vector();
    int flag = 1;
    if (flag)
        b = cplus_move(a);
    size_t n = a.size(); /* USE AFTER MOVE on the flag==true path */
    print("n", n);
    b.drop();
    return 0;
}
