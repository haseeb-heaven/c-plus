// EXPECT: REJECT
// Cross-function lifetime (P0): ptr_to_parameter() returns the address of its
// by-value parameter.  The parameter's storage is destroyed when the call
// returns, so the caller's pointer dangles.  The current RAII lowering only
// rejects a direct `return <borrow-of-local>;`; a parameter is a distinct
// storage class and must be covered by the same interprocedural summary.
#include "cplus/cplus.h"

static int *ptr_to_parameter(int value) {
    return &value;             /* address of parameter outlives the call */
}

int main(void) {
    int *p = ptr_to_parameter(9);
    return *p;                 /* reads the dead parameter slot */
}
