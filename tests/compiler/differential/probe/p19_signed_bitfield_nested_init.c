#include <stdio.h>

struct SB { int a : 3; int b : 4; };
struct Inner { int x, y; };
struct Outer { struct Inner in; int z; };

int main(void) {
    struct SB s;
    s.a = -3;
    s.b = 3;
    printf("sb=%d %d\n", s.a, s.b);

    struct Outer o = { .in = { .x = 1, .y = 2 }, .z = 3 };
    printf("o=%d %d %d\n", o.in.x, o.in.y, o.z);

    struct Outer o2 = { .in.y = 9 };
    printf("o2=%d %d %d\n", o2.in.x, o2.in.y, o2.z);
    return 0;
}
