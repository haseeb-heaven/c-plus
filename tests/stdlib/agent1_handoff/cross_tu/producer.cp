/* P1 cross-TU reproducer, case A -- producer translation unit.
 *
 * Returns a view into a local `string`.  string::to_view() records the
 * address of the local's heap buffer; the automatic string_drop at scope exit
 * frees that buffer, so the returned view outlives its owner.
 */
#include "owner.hp"

string_view produce_view(void) {
    string local = string("owned in producer");
    return local.to_view(); /* borrowed view of a local: must not escape */
}
