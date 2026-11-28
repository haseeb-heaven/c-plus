/* EXPECT: RUNTIME (requested REJECT; C+ lowering rejects, but UNTAGGED).
 *
 * Reason: cplus_move(a) zeroes `a` and transfers the owned buffers to `b`,
 * so reading `a` afterwards is a use-after-move.  The C+ RAII lowering pass
 * DOES reject this at compile time with:
 *   error: use-after-move: use of moved-from resource 'a'
 * A borrowed pointer alias (`intmap *p = &a;`) is rejected too, with
 * "borrow 'p' of 'a' is used after that resource was moved".  Neither message
 * has a "[CPLUS-" prefix, so per the suite rule they do not count as tagged
 * C+ enforcement and the shared runner reports PARTIAL, not REJECT.
 *
 * Exact observed outcome: check and strict build both exit 1 on the
 * `a.size()` use below; no [CPLUS- token in either output stream.  The
 * rejection is real but untagged, which is the gap recorded here.
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/collections.hp"

int main(void) {
    intmap a = intmap();
    a.put(3, 5);
    a.put(4, 6);

    intmap b = cplus_move(a); /* a is now zeroed / moved-from */
    int out = -1;
    int rc = b.get(3, &out); /* legal use of the new owner */
    print("moved", rc, "out", out);

    print("moved-from", a.size()); /* use after move -> untagged error */
    b.drop();
    return 0;
}
