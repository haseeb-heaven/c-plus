/* EOF handling: an exhausted stream is reported, never fatal. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/io.hp"

int main(void) {
    error err = error();
    int value = 0;
    double ratio = 0;
    char line[16];
    int st_int = read_int(&value, &err);
    int st_line = read_line(line, sizeof(line), &err);
    int st_double = read_double(&ratio, &err);
    print("eof", st_int, st_line, st_double, err.failed_now());
    return (st_int == STDLIB_EOF && st_line == STDLIB_EOF &&
            st_double == STDLIB_EOF && !err.failed_now())
               ? 0
               : 1;
}
