/* TU A: includes stdlib/io.hp; TU B includes the same header again. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/io.hp"

int part_a(void) {
    error err = error();
    int value = 0;
    if (read_int(&value, &err) != STDLIB_OK && !err.failed_now())
        return 0; /* end of input is not a failure */
    return value == 7 ? 0 : 1;
}
