// EXPECT: RUNTIME
// Reason: close() clears the FILE* handle.  A later file::read() sees the NULL
//         handle and returns STDLIB_FAIL with an IO error at run time.  The
//         compiler does not track the closed/open state of a file object, so
//         `c+ check --strict` emits no [CPLUS- diagnostic for this pattern.
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"

int main(void) {
    error err = error();
    char buf[8];
    file f = file_temp(&err);
    if (!f.is_open())
        return 1;
    if (f.write_bytes("x", 1, &err) != STDLIB_OK)
        return 2;
    if (f.close(&err) != STDLIB_OK)
        return 3;
    return f.read(buf, sizeof(buf), &err) == STDLIB_FAIL ? 0 : 4;
}
