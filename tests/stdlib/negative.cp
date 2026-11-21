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
    error err = error();
    int failures = 0;

    vector v = vector();
    int out = 0;
    if (v.reserve(SIZE_MAX / 2) != -1)
        failures++;
    if (v.at_checked(0, &out) != -1)
        failures++;
    if (v.pop_back(&out) != -1)
        failures++;
    if (v.at_checked(0, NULL) != -1)
        failures++;
    if (v.push_back(1) != 0 || v.at_checked(0, &out) != 0 || out != 1)
        failures++;

    string s = string("a-b");
    char buf[4];
    if (s.field('-', 9, buf, sizeof(buf)) != -1)
        failures++;
    if (s.substr(50, 2, buf, sizeof(buf)) != -1)
        failures++;
    if (s.substr(0, 2, NULL, 4) != -1)
        failures++;
    if (s.find("zz") != -1)
        failures++;
    print("strings-ok", failures, s.count_fields('-'), s.compare("a-b"));

    file missing = file("/nonexistent/cplus/definitely/missing.txt", "r");
    if (missing.is_open())
        failures++;
    if (missing.read(buf, sizeof(buf), &err) != STDLIB_FAIL)
        failures++;
    if (!err.failed_now())
        failures++;
    print("file-error", failures, err.code());

    int wr = missing.write("x");
    int bytes = missing.write_bytes("x", 1, &err);
    if (wr != 0 || bytes != STDLIB_FAIL || !err.failed_now())
        failures++;
    print("bad-handle-write", failures, wr, bytes);

    err.set(CPLUS_EXIT_IO, "synthetic");
    if (!err.failed_now() || err.code() != (int)CPLUS_EXIT_IO || !err.ok() == 0)
        failures++;
    error_reset(&err);
    if (err.failed_now() || err.ok() != 1)
        failures++;
    print("error-methods", failures, err.ok(), err.code());

    guard no_owner = guard(NULL);
    if (no_owner.holds())
        failures++;
    print("guard-null", failures, no_owner.holds());

    return failures == 0 ? 0 : 1;
}
