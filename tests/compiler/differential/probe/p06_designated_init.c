#include <stdio.h>

struct P { int x, y, z; };

int main(void) {
    int a[6] = { [1] = 10, [4] = 40 };
    printf("a=%d %d %d %d %d %d\n", a[0], a[1], a[2], a[3], a[4], a[5]);

    struct P p = { .y = 2, .x = 1 };
    printf("p=%d %d %d\n", p.x, p.y, p.z);

    char s[4] = { [3] = 'Z' };
    printf("s=%d %d %d %d\n", s[0], s[1], s[2], s[3]);
    return 0;
}
