/* Foundation smoke test: string, vector, file, mutex, safe input. */
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

    string s = string("hello");
    int appended = s.append(" world");
    print("str", s.size(), s.c_str(), appended, s.equals("hello world"),
          s.compare("hello world"), s.find("world"));

    char slice[8];
    s.substr(6, 5, slice, sizeof(slice));
    print("slice", slice, s.count_fields(' '));
    char field0[8];
    char field1[8];
    s.field(' ', 0, field0, sizeof(field0));
    s.field(' ', 1, field1, sizeof(field1));
    print("fields", field0, field1, s.field(' ', 5, field0, sizeof(field0)));

    string_view view = s.to_view();
    print("view", view.size(), view.equals("hello world"));
    s.reserve(128);
    print("capacity", s.capacity());

    vector v = vector();
    int i;
    for (i = 0; i < 5; i++) {
        if (v.push_back(i * i) != 0)
            return 10;
    }
    int got = -1;
    int ok = v.at_checked(3, &got);
    int bad = v.at_checked(99, &got);
    int last = -1;
    v.pop_back(&last);
    print("vec", v.size(), v.capacity(), ok, got, bad, last);
    v.clear();
    print("vec-clear", v.size(), v.pop_back(&last));

    file out = file("/tmp/cplus_foundation.txt", "w");
    if (!out.is_open())
        return 11;
    out.write("line one\n");
    out.write_bytes("line two\n", 9, &err);
    out.flush(&err);

    file in = file("/tmp/cplus_foundation.txt", "r");
    if (!in.is_open())
        return 12;
    char line[32];
    int rd = in.read_line(line, sizeof(line), &err);
    print("file", rd, line);

    file missing = file("/nonexistent/cplus/dir/x.txt", "r");
    print("missing", missing.is_open());

    file std_out = file_borrow(stdout);
    print("borrowed", std_out.is_open());

    mutex m = mutex();
    if (!m.is_valid())
        return 13;
    {
        guard g = guard(&m);
        print("guard", g.holds());
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
