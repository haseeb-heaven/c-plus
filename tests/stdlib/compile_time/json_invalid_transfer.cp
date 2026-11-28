/* EXPECT: RUNTIME (requested REJECT; no [CPLUS- diagnostic is produced).
 *
 * Negative: ownership of a json document is transferred by copying the
 * resource handle (`json b = a;`) instead of by cplus_move(a).  Both handles
 * would then own the same yyjson_doc, giving a double free at scope exit.
 *
 * The compiler DOES reject this with a semantic error
 * ("resource 'b' cannot be copied from 'a'"; check rc=1, build rc=1), but
 * that diagnostic comes from the RAII lowering pass and has no [CPLUS- tag.
 * The suite explicitly counts only [CPLUS- diagnostics as C+ ownership
 * enforcement, so the observed classification here is PARTIAL, not REJECT.
 * Two related probes that are also plain (untagged) errors:
 *   - `json b = cplus_move(a); a.root();` -> "use-after-move"
 *   - borrow of a used after cplus_move(a) -> "borrow ... used after move"
 *
 * Treat this as a coverage gap in diagnostic tagging; the underlying copy
 * rule itself is enforced.  Pair with a runtime test for moved-from state.
 *
 * Build flags required by this header (see json.json):
 *   -Ithird_party/yyjson -Lbuild/ext -lyyjson
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"

int main(void) {
    error err = error();
    json a = json("{\"n\":1}", &err);
    if (!a.valid())
        return 1;

    json b = a;   /* INVALID TRANSFER: copy instead of cplus_move(a) */
    (void)b;

    return 0;
}
