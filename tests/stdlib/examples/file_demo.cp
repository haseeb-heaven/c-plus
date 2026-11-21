/* Demo: RAII files - owning handles, borrowed stdout, recoverable errors. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"

int main(void) {
    error err = error();
    file log = file("/tmp/cplus_file_demo.txt", "w");
    if (!log.is_open()) {
        print("cannot open log", err.what());
        return 1;
    }
    log.write("first line\n");
    log.write_bytes("second line\n", 12, &err);
    log.flush(&err);

    file reader = file("/tmp/cplus_file_demo.txt", "r");
    if (!reader.is_open())
        return 2;
    char line[32];
    while (reader.read_line(line, sizeof(line), &err) == STDLIB_OK)
        print("read", line);

    reader.seek(0, SEEK_SET, &err);
    char block[16];
    if (reader.read(block, sizeof(block) - 1, &err) == STDLIB_OK) {
        block[sizeof(block) - 1] = '\0';
        print("block", block);
    }

    file out = file_borrow(stdout);
    print("borrowed stdout", out.is_open(), out.write("done\n"));

    file missing = file("/nonexistent/demo.txt", "r");
    print("missing", missing.is_open(), err.failed_now());
    return 0;
}
