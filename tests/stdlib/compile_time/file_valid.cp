// EXPECT: ACCEPT
// Reason: constructing, writing, checking, explicitly closing and dropping an
//         owned file is a valid, single-owner lifecycle.
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"

int main(void) {
    error err = error();
    file f = file_temp(&err);
    if (!f.is_open())
        return 1;
    if (f.write_bytes("x", 1, &err) != STDLIB_OK)
        return 2;
    if (f.flush(&err) != STDLIB_OK)
        return 3;
    if (f.close(&err) != STDLIB_OK)
        return 4;
    return f.is_open() ? 5 : 0;
}
