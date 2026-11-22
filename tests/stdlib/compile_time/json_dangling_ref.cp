/* EXPECT: RUNTIME (requested REJECT; compiler cannot prove this statically).
 *
 * Negative: a json_view borrows storage inside its owning json document.
 * make_dangling() returns a view into a local document; when the function
 * returns, RAII frees that document and the returned view points at freed
 * memory.  C+ has no borrow annotation for class value types (json_view has
 * no drop(), so it is not an owned resource), and its borrow checker only
 * tracks raw `T *p = &owner` escapes.  The AST lifetime pass is also
 * inactive with yyjson.h in the translation unit.  Observed: check rc=0,
 * build rc=0, no [CPLUS- diagnostic.
 *
 * This is a genuine dangling reference; it needs a runtime/behavioral test
 * (or a compiler enhancement) rather than a compile-time claim.  The
 * positive counterpart is json_valid_ref.cp.
 *
 * Build flags required by this header (see json.json):
 *   -Ithird_party/yyjson -Lbuild/ext -lyyjson
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"

static json_view make_dangling(void) {
    error err = error();
    json doc = json("{\"name\":\"dangling\"}", &err);
    json_view root = doc.root();
    return root.member("name");   /* borrow escapes the owner's scope */
} /* doc is destroyed here; the returned view dangles */

int main(void) {
    error err = error();
    json_view escaped = make_dangling();
    const char *value = NULL;
    int rc = escaped.get_string(&value, &err);
    (void)rc;
    (void)value;
    return 0;
}
