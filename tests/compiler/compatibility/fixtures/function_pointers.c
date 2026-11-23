/* Function-to-pointer conversion and calls through function pointers. */
#ifndef CPLUS_LEGACY_C
#error "compatibility fixture must be built in LEGACY C mode"
#endif
#include <stdio.h>

static int add(int a, int b) { return a + b; }
static int sub(int a, int b) { return a - b; }

static int apply(int (*fn)(int, int), int x, int y) { return fn(x, y); }

int main(void) {
    int (*fp)(int, int) = add;
    int (*fps[2])(int, int);

    fps[0] = add;
    fps[1] = sub;

    printf("%d %d\n", fp(3, 4), apply(sub, 9, 4));
    printf("%d %d\n", fps[0](1, 2), fps[1](1, 2));
    printf("%d\n", (int)(fp == add));
    return 0;
}
