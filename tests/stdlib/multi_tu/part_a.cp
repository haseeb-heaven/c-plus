/* TU A: includes file.hp + io.hp; TU B includes the same headers again. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/io.hp"
#include "stdlib/file.hp"

int part_a(void) {
    StdError err;
    char line[32];
    int rc = 0;
    StdError_init(&err);

    File out = File_open("/tmp/cplus_multi_tu.txt", "w");
    if (!File_valid(&out))
        return 1;
    File_write(&out, "shared header\n");
    if (File_flush(&out, &err) != STDLIB_OK)
        return 1;

    File in = File_open("/tmp/cplus_multi_tu.txt", "r");
    if (!File_valid(&in))
        return 1;
    if (File_read_line(&in, line, sizeof(line), &err) != STDLIB_OK)
        rc = 1;
    if (line[0] != 's')
        rc = 1;
    return rc;
}
