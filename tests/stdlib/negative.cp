/* Negative tests: every recoverable failure must report, not crash. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/io.hp"
#include "stdlib/string.hp"
#include "stdlib/vector.hp"
#include "stdlib/file.hp"
#include "stdlib/thread.hp"

int main(void) {
    StdError err;
    StdError_init(&err);
    int failures = 0;

    IntVec v = IntVec();
    int out = 0;
    if (v.reserve(SIZE_MAX / 2) != -1)
        failures++;
    if (v.get(0, &out) != -1)
        failures++;
    if (v.pop(&out) != -1)
        failures++;
    if (v.get(0, NULL) != -1)
        failures++;
    if (v.push(1) != 0 || v.get(0, &out) != 0 || out != 1)
        failures++;

    String s = String("a-b");
    char buf[4];
    if (s.field('-', 9, buf, sizeof(buf)) != -1)
        failures++;
    if (s.substring(50, 2, buf, sizeof(buf)) != -1)
        failures++;
    if (s.substring(0, 2, NULL, 4) != -1)
        failures++;
    if (s.find("zz") != -1)
        failures++;
    print("strings-ok", failures, s.field_count('-'), s.compare("a-b"));

    File missing = File_open("/nonexistent/cplus/definitely/missing.txt", "r");
    if (File_valid(&missing))
        failures++;
    if (File_read(&missing, buf, sizeof(buf), &err) != STDLIB_FAIL)
        failures++;
    if (!StdError_failed(&err))
        failures++;
    print("file-error", failures, StdError_code(&err));

    int wr = File_write(&missing, "x");
    int bytes = File_write_bytes(&missing, "x", 1, &err);
    if (wr != 0 || bytes != STDLIB_FAIL || !StdError_failed(&err))
        failures++;
    print("bad-handle-write", failures, wr, bytes);

    Guard no_owner = Guard_lock(NULL);
    if (Guard_holds(&no_owner))
        failures++;
    print("guard-null", failures, Guard_holds(&no_owner));

    return failures == 0 ? 0 : 1;
}
