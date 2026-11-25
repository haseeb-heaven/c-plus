/* EXPECT: RUNTIME (requested REJECT; compiler cannot prove this statically).
 *
 * Negative: a json_builder is explicitly drop()ped and then mutated again.
 * add_int() after drop() touches a released yyjson_mut_doc.  The reason this
 * is not rejected mirrors json_access_after_destroy.cp: explicit drop() on
 * a class handle is not tracked as invalidation, the call lowers to a plain
 * `json_builder_add_int(&builder, ...)` function call, and the AST lifetime
 * pass is inactive with yyjson.h in the translation unit.  Observed:
 * check rc=0, build rc=0, no [CPLUS- diagnostic.
 *
 * At run time the dropped builder is inert (document == NULL), so add_int()
 * reports STDLIB_FAIL through error instead of corrupting memory.  Cover the
 * post-drop failure contract with a runtime test.
 *
 * Build flags required by this header (see json.json):
 *   -Ithird_party/yyjson -Lbuild/ext -lyyjson
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"

int main(void) {
    error err = error();
    json_builder builder = json_builder();
    if (!builder.valid())
        return 1;
    if (builder.add_int("before", 1, &err) != STDLIB_OK)
        return 2;

    builder.drop();                              /* releases mutable document */

    int rc = builder.add_int("after", 2, &err);  /* USE AFTER DESTROY */
    return (rc == STDLIB_OK) ? 3 : 0;            /* runtime failure, not a compile error */
}
