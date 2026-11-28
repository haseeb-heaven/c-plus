// EXPECT: RUNTIME
// Reason: the second close() observes an already-cleared handle and fails with
//         STDLIB_CATEGORY_LIFECYCLE at run time.  The checked close() wrapper
//         makes double-close recoverable; the compiler does not prove it, so
//         no [CPLUS- diagnostic is produced by `c+ check --strict`.
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"

int main(void) {
    error err = error();
    file f = file_temp(&err);
    if (!f.is_open())
        return 1;
    if (f.close(&err) != STDLIB_OK)
        return 2;
    if (f.close(&err) != STDLIB_FAIL)
        return 3;
    return err.category() == STDLIB_CATEGORY_LIFECYCLE ? 0 : 4;
}
