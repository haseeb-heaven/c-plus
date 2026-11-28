/* EXPECT: RUNTIME (requested REJECT; compiler cannot prove owner liveness).
 *
 * Reason: intmap::drop() releases the three backing buffers (keys, values,
 * state) and zeroes the class fields.  Every later call on the container is
 * therefore a use of a dead owner.  C+ tracks raw `managed` pointers and
 * `resource struct` handles, but it does not model the pointers stored inside
 * a lightweight `class`, so no [CPLUS-LIFETIME]/[CPLUS-OWNER] diagnostic is
 * produced here.  Calling drop() twice is likewise not diagnosed (the second
 * drop sees NULL and is a documented no-op).
 *
 * Exact observed outcome: c+ check --strict and c+ build --strict
 * --sanitize=none both SUCCEED with no [CPLUS- diagnostic.  At runtime the
 * checked accessors report the dead state (size() == 0, get() == -1, *out
 * untouched); a raw read of a stale internal pointer is silent.  This is a
 * compile-time capability gap, not C+ enforcement.
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/collections.hp"

int main(void) {
    intmap m = intmap();
    m.put(1, 42);
    m.put(2, 84);
    m.drop(); /* releases keys/values/state and zeroes the owner */

    int out = -1;
    int rc = m.get(1, &out); /* use after drop: dead owner */
    print("after-drop", "size", m.size(), "get", rc, "out", out);
    m.put(3, 7); /* reuse after drop: also unprovable */

    m.drop(); /* idempotent at runtime; not diagnosed statically */
    return 0;
}
