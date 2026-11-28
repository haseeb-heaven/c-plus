/* TU B: the same headers as TU A, plus a second reader. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/io.hp"

int part_b(void) {
    error err = error();
    char line[16];
    int i;
    int counted = 0;
    for (i = 0; i < 3; i++) {
        if (read_line(line, sizeof(line), &err) == STDLIB_OK)
            counted++;
    }
    return counted;
}
