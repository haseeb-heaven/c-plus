// EXPECT: RUNTIME
// Reason: writing through a file after close() uses a cleared handle.  Both
//         file::write() and file::write_bytes() detect it and fail safely at
//         run time, but the compiler cannot prove the handle state, so no
//         [CPLUS- diagnostic is produced by `c+ check --strict`.
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
    if (f.write("x") != 0)
        return 3;
    return f.write_bytes("x", 1, &err) == STDLIB_FAIL ? 0 : 4;
}
