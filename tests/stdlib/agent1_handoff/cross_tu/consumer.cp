/* P1 cross-TU reproducer, case A -- consumer translation unit.
 *
 * The definition of produce_view() is not visible here; only the declaration
 * in owner.hp is.  Reading through the returned view dereferences memory
 * released in producer.cp.
 */
#include <stdio.h>
#include "cplus/cplus.h"
#include "owner.hp"

int main(void) {
    string_view view = produce_view();
    print("view size", view.size());
    return 0;
}
