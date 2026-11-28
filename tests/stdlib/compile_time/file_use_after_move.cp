// EXPECT: REJECT
// Reason: cplus_move() zeroes the source file handle.  Any further use of the
//         moved-from owner 'a' is use-after-move and must be rejected.
// Observed: `c+ check --strict` and `c+ build --strict --sanitize=none` fail
//           with "error: use-after-move: use of moved-from resource 'a'".
//           That diagnostic is untagged (no [CPLUS- prefix), so the strict
//           harness reports PARTIAL rather than REJECT for this case.
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"

int main(void) {
    error err = error();
    file a = file_temp(&err);
    file b = cplus_move(a);
    if (b.write_bytes("x", 1, &err) != STDLIB_OK)
        return 1;
    return a.write_bytes("y", 1, &err) == STDLIB_FAIL ? 0 : 2;
}
