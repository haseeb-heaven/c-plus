/* S5 regression: the weak view registry's table size cannot be overridden
 * per translation unit.  An inconsistent STDLIB_VIEW_TABLE_BITS must be a
 * compile error, not a silent per-TU layout disagreement. */
#define STDLIB_VIEW_TABLE_BITS 13u
#include "stdlib/view_lifetime.hp"

int main(void) {
    return 0;
}
