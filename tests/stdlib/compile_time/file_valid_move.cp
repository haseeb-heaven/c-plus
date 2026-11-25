// EXPECT: ACCEPT
// Reason: cplus_move() transfers file ownership exactly once.  The source is
//         zeroed, both cleanups are safe, and only the new owner is used.
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"

int main(void) {
    error err = error();
    file a = file_temp(&err);
    file b = cplus_move(a);
    if (!b.is_open())
        return 1;
    if (b.write_bytes("x", 1, &err) != STDLIB_OK)
        return 2;
    return b.close(&err) == STDLIB_OK ? 0 : 3;
}
