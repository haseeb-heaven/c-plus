/* Issue #3 acceptance: empty input, whitespace, EOF, negative, boundaries,
 * trailing garbage, embedded spaces, overflow, overlong lines, non-finite.
 *
 * Line-based checks run first (token readers leave the newline behind), then
 * the token-based conversions, then EOF. */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/io.hp"

int main(void) {
    error err = error();
    char line[16];
    int i = 0;
    long l = 0;
    float f = 0;
    double d = 0;
    int st;

    st = read_line(line, sizeof(line), &err);
    print("ws", st, strlen(line), line[0] == ' ');

    st = read_line(line, sizeof(line), &err);
    print("spaces", st, strcmp(line, "a b  c") == 0);

    st = read_line(line, sizeof(line), &err);
    print("toolong", st, err.category() == STDLIB_CATEGORY_BOUNDS,
          strlen(line) == 0);
    st = read_line(line, sizeof(line), &err);
    print("aligned", st, strcmp(line, "next") == 0);

    st = read_int(&i, &err);
    print("neg", st, i, err.category() == STDLIB_CATEGORY_OK);
    st = read_int(&i, &err);
    print("max", st, i == 2147483647);
    st = read_int(&i, &err);
    print("min", st, i == (-2147483647 - 1));
    st = read_long(&l, &err);
    print("lmax", st, l == 9223372036854775807L);

    st = read_int(&i, &err);
    print("garbage", st, err.category() == STDLIB_CATEGORY_INVALID);
    st = read_int(&i, &err);
    print("overflow", st, err.category() == STDLIB_CATEGORY_RANGE);

    st = read_float(&f, &err);
    print("inf", st, err.category() == STDLIB_CATEGORY_RANGE);
    st = read_double(&d, &err);
    print("nan", st, err.category() == STDLIB_CATEGORY_RANGE);
    st = read_float(&f, &err);
    print("floatbig", st, err.category() == STDLIB_CATEGORY_RANGE);
    st = read_double(&d, &err);
    print("finite", st, d > 3.13 && d < 3.15);

    st = read_int(&i, &err);
    print("plus", st, i == 5);

    st = read_line(line, sizeof(line), &err);
    st = read_int(&i, &err);
    print("eof", st, err.category() == STDLIB_CATEGORY_OK);
    return 0;
}
