/* EXPECT: RUNTIME (requested REJECT; compiler cannot prove this statically).
 *
 * Negative: an owned json handle is explicitly drop()ped and then read
 * again.  drop() frees the yyjson_doc, so every later call on doc uses dead
 * state.  C+ registers a class with drop() as a resource for RAII cleanup,
 * but it does not model an explicit drop() as invalidating the handle: the
 * method call lowers to `json_root(&doc)`, a plain function call that the
 * lifetime pass does not inspect.  In addition, the AST lifetime pass does
 * not run at all when stdlib/json.hp brings yyjson.h into the translation
 * unit (a null dereference in the same TU is also not diagnosed), so this
 * file cannot produce a [CPLUS-LIFETIME] diagnostic.
 *
 * Observed with the required build flags: c+ check --strict rc=0 and
 * c+ build --strict --sanitize=none rc=0, no [CPLUS- diagnostic.  At run
 * time the dropped handle is inert (document == NULL), so root() returns an
 * invalid view instead of crashing.  Use a runtime test to assert the
 * post-drop contract; do not claim a compile-time guarantee here.
 *
 * Build flags required by this header (see json.json):
 *   -Ithird_party/yyjson -Lbuild/ext -lyyjson
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"

int main(void) {
    error err = error();
    json doc = json("{\"n\":1}", &err);
    if (!doc.valid())
        return 1;

    doc.drop();                    /* releases document, sets it to NULL */

    json_view root = doc.root();   /* USE AFTER DESTROY */
    return root.valid() ? 2 : 0;   /* inert at run time; not proven at compile time */
}
