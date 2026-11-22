// EXPECT: REJECT
// Reason: error_reset() explicitly accepts a NULL target (a no-op), but a
// null error* is outside the contract for member access. The checker must
// reject the null-pointer dereference.
#include "cplus/cplus.h"
#include "stdlib/error.hp"

int main(void) {
    error *slot = 0;

    error_reset(slot); /* contract-valid: NULL target is handled */

    return slot->failed; /* contract-invalid: null member access */
}
