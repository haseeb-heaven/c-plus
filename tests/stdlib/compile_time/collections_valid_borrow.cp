/* EXPECT: ACCEPT (positive control for a short-lived borrow).
 *
 * Reason: `borrow` points to the still-live owner `m` and is used only before
 * m.drop().  C+ accepts a pointer borrow whose lifetime is nested inside the
 * owner's lifetime.  The strmap part also shows that a stack key buffer can
 * be mutated after put(): strmap copies keys, so the caller's buffer is a
 * safe borrow and never dangles inside the map.  check and strict build must
 * both succeed with no [CPLUS- diagnostic, and the binary must exit 0.
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/collections.hp"

int main(void) {
    intmap m = intmap();
    m.put(1, 42);

    intmap *borrow = &m; /* borrow of a live owner */
    int out = -1;
    int rc = borrow->get(1, &out); /* last use of the borrow */
    print("valid-borrow", "get", rc, "out", out, "size", borrow->size());

    m.drop(); /* owner released after the borrow's last use */

    strmap sm = strmap();
    char stack_key[16] = "alpha";
    sm.put(stack_key, 9); /* strmap owns a copy of the key */
    stack_key[0] = 'x';   /* mutating the caller's buffer is safe */
    out = -1;
    rc = sm.get("alpha", &out);
    print("owned-key", "get", rc, "out", out, "size", sm.size());
    sm.drop();

    return 0;
}
