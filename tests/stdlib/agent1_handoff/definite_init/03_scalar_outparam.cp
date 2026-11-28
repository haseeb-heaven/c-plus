// EXPECT: REJECT
// A scalar out-parameter is written only on one path inside the callee.  The
// caller declares the output variable uninitialized and reads it after the
// call, so the path with no write leaves it indeterminate.
#include "cplus/cplus.h"

int g_condition;

void maybe_write(int *out) {
    if (g_condition)
        *out = 7;
}

int main(void) {
    int result; /* no initializer; callee writes it only on one path */
    maybe_write(&result);
    return result; /* indeterminate when g_condition == 0 */
}
