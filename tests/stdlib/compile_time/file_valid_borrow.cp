// EXPECT: ACCEPT
// Reason: file_borrow() wraps an existing stream without taking ownership, and
//         a raw pointer borrow of a live local file is valid while its owner
//         is still in scope.
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"

int main(void) {
    error err = error();
    file out = file_borrow(stdout);
    if (!out.is_open())
        return 1;
    if (out.flush(&err) != STDLIB_OK)
        return 2;

    file owned = file_temp(&err);
    file *borrowed = &owned;
    if (!borrowed->is_open())
        return 3;
    return borrowed->write_bytes("x", 1, &err) == STDLIB_OK ? 0 : 4;
}
