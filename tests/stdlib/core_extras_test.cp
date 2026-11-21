/* Tests for the checked slice, filesystem helpers and clocks. */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/file.hp"
#include "stdlib/fs.hp"
#include "stdlib/time.hp"

int main(void) {
    error err = error();
    int value = 0;

    vector v = vector();
    v.push_back(10);
    v.push_back(20);
    v.push_back(30);
    slice all = v.view();
    print("slice", all.size(), all.valid(), all.at_checked(1, &value) == 0 &&
                                           value == 20,
          all.at_checked(9, &value) == -1);
    slice tail = all.sub(1, 99);
    print("sub", tail.size(), tail.at_checked(0, &value) == 0 && value == 20);
    slice empty = all.sub(7, 2);
    print("sub-empty", empty.size(), empty.valid() == 0);
    v.clear();
    slice cleared = v.view();
    print("after-clear", cleared.size());

    char path[64];
    snprintf(path, sizeof(path), "/tmp/cplus_fs_%d.txt", 4242);
    fs_remove(path, &err);
    file scratch = file(path, "w");
    if (scratch.is_open())
        scratch.write("0123456789");
    scratch.drop();
    print("fs-exists", fs_exists(path), fs_is_file(path), fs_is_dir(path) == 0,
          fs_size(path, &err) == 10);
    print("fs-dir", fs_is_dir("/tmp"), fs_exists("/nonexistent/cplus") == 0);
    char moved[64];
    snprintf(moved, sizeof(moved), "%s.moved", path);
    print("fs-rename", fs_rename(path, moved, &err) == STDLIB_OK,
          fs_exists(moved), fs_exists(path) == 0);
    print("fs-remove", fs_remove(moved, &err) == STDLIB_OK, fs_exists(moved) == 0);
    print("fs-missing", fs_size("/nonexistent/cplus", &err) == -1,
          err.category() == STDLIB_CATEGORY_IO);
    char dir[64];
    snprintf(dir, sizeof(dir), "/tmp/cplus_fs_dir_%d", 4242);
    fs_remove(dir, &err);
    print("fs-mkdir", fs_mkdir(dir, &err) == STDLIB_OK, fs_is_dir(dir));
    fs_remove(dir, &err);

    unsigned long long before = time_monotonic_ms();
    time_sleep_ms(20);
    unsigned long long after = time_monotonic_ms();
    print("clock", time_now_ms() > 0, time_now_ns() > 0, after >= before,
          after - before >= 10, time_monotonic_ns() > 0);
    print("monotonic-order", time_monotonic_ns() >= time_monotonic_ns());

    char stamp[32];
    unsigned long long fixed = 1700000000ULL * 1000000000ULL;
    print("format", time_format_iso8601(fixed, stamp, sizeof(stamp), &err) ==
                        STDLIB_OK,
          strcmp(stamp, "2023-11-14T22:13:20Z") == 0);
    print("format-small", time_format_iso8601(fixed, stamp, 4, &err) == STDLIB_FAIL,
          stamp[0] == '\0', err.category() == STDLIB_CATEGORY_BOUNDS);
    return 0;
}
