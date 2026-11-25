#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"
#include "stdlib/io.hp"
#include "stdlib/file.hp"

static char *arg_ptr(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

static unsigned udiv(unsigned a, unsigned b) {
    unsigned q = 0;
    unsigned r = a;
    for (int sh = 31; sh >= 0; --sh) {
        unsigned d = 1u << sh;
        if (b <= (UINT_MAX >> sh)) {
            unsigned v = b << sh;
            if (r >= v) { r -= v; q += d; }
        }
    }
    return q;
}

int main(int argc, char **argv) {
    error err = error();
    char buf[16];
    char one[1];
    size_t n = 0;
    int rc;
    int b = 0;
    if (argc > 1) {
        char *p = arg_ptr(argv, 1);
        b = atoi(p);
    }
    print("probe", argc, b, udiv(17u, 3u));
    {
        file f = file("examples/README.md", "r");
        if (f.is_open()) {
            while (n < sizeof(buf)) {
                rc = f.read(one, 1, &err);
                if (rc != STDLIB_OK) break;
                __builtin_memcpy(buf + n, one, 1);
                n++;
            }
            print("read", n, err.ok());
            f.drop();
        }
    }
    return 0;
}
