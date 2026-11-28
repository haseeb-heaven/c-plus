/* Compound literals (GNU extension supported by the gnu89 backend). */
#ifndef CPLUS_LEGACY_C
#error "compatibility fixture must be built in LEGACY C mode"
#endif
#include <stdio.h>

struct P { int x, y; };

static int show(struct P p) { return p.x * 10 + p.y; }

int main(void) {
    struct P p = (struct P){ 3, 4 };
    int *ip = (int[]){ 7, 8, 9 };

    printf("%d %d\n", p.x, p.y);
    printf("%d %d %d\n", ip[0], ip[1], ip[2]);
    printf("%d\n", show((struct P){ 2, 5 }));
    return 0;
}
