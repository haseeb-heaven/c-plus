#include <stdio.h>
struct Pair { int a; int b; };
static struct Pair make_pair(int a, int b) {
    struct Pair p; p.a = a; p.b = b; return p;
}
static int pair_sum(struct Pair p) { return p.a + p.b; }
int main(void) {
    struct Pair p = make_pair(2235, 5208);
    printf("a=%d b=%d sum=%d\n", p.a, p.b, pair_sum(p));
    return 0;
}
