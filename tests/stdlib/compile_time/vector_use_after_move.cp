/* EXPECT: RUNTIME (requested REJECT; C+ lowering rejects, but UNTAGGED).
 *
 * Reason: cplus_move(a) zeroes a and transfers the owned buffer to b, so
 * reading a afterwards is a use-after-move.  The C+ RAII lowering pass DOES
 * reject this at compile time with:
 *   error: use-after-move: use of moved-from resource 'a'
 * That message has NO "[CPLUS-" prefix, so per the suite rule it does not
 * count as tagged C+ enforcement and the shared runner reports PARTIAL,
 * not REJECT.  Clang emits no warning of its own.
 *
 * Exact observed outcome: check and strict build exit 1 on the `a.size()`
 * use below; no [CPLUS- token in either output stream.
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"

int main(void) {
    vector a = vector();
    a.push_back(3);
    a.push_back(5);

    vector b = cplus_move(a); /* a is now zeroed/moved-from */
    int out = -1;
    int rc = b.at_checked(1, &out); /* legal use of the new owner */
    print("moved", rc, "out", out);

    print("moved-from", a.size()); /* use after move -> compile-time error */
    b.drop();
    return 0;
}
