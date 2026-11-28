/* Nested declarations: nested struct/union members and block-scope shadowing. */
#ifndef CPLUS_LEGACY_C
#error "compatibility fixture must be built in LEGACY C mode"
#endif
#include <stdio.h>

struct Outer {
    int tag;
    struct { int a; int b; } inner;
    union { int i; char c[4]; } u;
};

int main(void) {
    int x = 1;
    struct Outer o;

    o.tag = 7;
    o.inner.a = 8;
    o.inner.b = 9;
    o.u.i = 10;
    printf("%d %d %d %d\n", o.tag, o.inner.a, o.inner.b, o.u.i);
    {
        int x = 100;             /* shadows the outer x */
        printf("%d\n", x);
    }
    printf("%d\n", x);
    return 0;
}
