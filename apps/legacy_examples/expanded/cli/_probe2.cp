#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"
#include "stdlib/io.hp"
#include "stdlib/file.hp"

static int parse_int(const char *text, int *out) {
    char *end = (char *)text;
    long v;
    if (text == NULL) return 0;
    errno = 0;
    v = strtol(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE || v < INT_MIN || v > INT_MAX)
        return 0;
    *out = (int)v;
    return 1;
}

int main(void) {
    error err = error();
    vector v = vector();
    char lines[4][32];
    string s = string("hello world");
    int out = 0;
    char field[32];
    char sub[8];
    char line[64];
    int i;
    v.push_back(10);
    v.push_back(20);
    print("vec", v.size(), v.at_checked(1, &out), out);
    v.erase(0, &out);
    print("erase", out, v.size());
    print("str", s.size(), s.at(0), s.find("world"), s.find("zzz"));
    s.field(' ', 1, field, sizeof(field));
    s.substr(0, 5, sub, sizeof(sub));
    print("field", field, sub);
    for (i = 0; i < 4; i++) {
        snprintf(lines[i], sizeof(lines[i]), "L%d", i);
    }
    print("lines", lines[2]);
    error_fail(&err, CPLUS_EXIT_UNSUPPORTED, "divide by zero");
    print("err", err.ok(), err.failed_now(), err.category(), err.code(), err.what());
    {
        file f = file("apps/legacy_examples/README.md", "r");
        int rc = f.read_line(line, sizeof(line), &err);
        print("rl", rc, err.ok(), line);
        f.drop();
    }
    s.drop();
    v.drop();
    return 0;
}
