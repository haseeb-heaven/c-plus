#include <stdio.h>
struct Inner { int x; int y; };
struct Outer { char tag; struct Inner in; double d; };
int main(void) {
    struct Outer o;
    o.tag = 91; o.in.x = 8284; o.in.y = 2077; o.d = 2383 / 4.0;
    struct Outer *p = &o;
    printf("tag=%d x=%d y=%d d=%.6f\n", p->tag, p->in.x, p->in.y, p->d);
    return 0;
}
