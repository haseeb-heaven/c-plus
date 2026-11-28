#include <stdio.h>
static int scale(int x) { return x * 8; }
static int apply(int (*fn)(int), int x) { return fn(x); }
static int (*active)(int) = scale;
int main(void) {
    printf("nested=%d direct=%d\n", apply(active, 547), active(166));
    return 0;
}
