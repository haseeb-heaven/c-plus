/* Foundation smoke test: String, IntVec, File, Thread, safe input. */
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

    String s = String("hello");
    int appended = s.append(" world");
    print("str", s.len(), s.c_str(), appended, s.eq("hello world"),
          s.compare("hello world"), s.find("world"));

    char slice[8];
    s.substring(6, 5, slice, sizeof(slice));
    print("slice", slice, s.field_count(' '));
    char field0[8];
    char field1[8];
    s.field(' ', 0, field0, sizeof(field0));
    s.field(' ', 1, field1, sizeof(field1));
    print("fields", field0, field1, s.field(' ', 5, field0, sizeof(field0)));

    StringView view = s.view();
    print("view", StringView_len(&view), StringView_eq(&view, "hello world"));
    s.reserve(128);
    print("capacity", s.capacity());

    IntVec v = IntVec();
    int i;
    for (i = 0; i < 5; i++) {
        if (v.push(i * i) != 0)
            return 10;
    }
    int got = -1;
    int ok = v.get(3, &got);
    int bad = v.get(99, &got);
    int last = -1;
    v.pop(&last);
    print("vec", v.len(), v.capacity(), ok, got, bad, last);
    v.clear();
    print("vec-clear", v.len(), v.pop(&last));

    File out = File_open("/tmp/cplus_foundation.txt", "w");
    if (!File_valid(&out))
        return 11;
    File_write(&out, "line one\n");
    File_write_bytes(&out, "line two\n", 9, &err);
    File_flush(&out, &err);

    File in = File_open("/tmp/cplus_foundation.txt", "r");
    if (!File_valid(&in))
        return 12;
    char line[32];
    int rd = File_read_line(&in, line, sizeof(line), &err);
    print("file", rd, line);

    File missing = File_open("/nonexistent/cplus/dir/x.txt", "r");
    print("missing", File_valid(&missing));

    File std = File_borrow(stdout);
    print("borrowed", File_valid(&std));

    Mutex mutex = Mutex_new();
    if (!Mutex_valid(&mutex))
        return 13;
    {
        Guard guard = Guard_lock(&mutex);
        print("guard", Guard_holds(&guard));
    }

    int count = 0;
    long total = 0;
    double ratio = 0;
    float small = 0;
    char text[16];
    int a = read_int(&count, &err);
    int b = read_long(&total, &err);
    int c = read_double(&ratio, &err);
    int d = read_float(&small, &err);
    int e = read_line(text, sizeof(text), &err);
    print("input", a, count, b, total, c, ratio, d, small, e, text);
    return 0;
}
