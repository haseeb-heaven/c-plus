/* struct/union layout: sizeof and offsetof. */
#ifndef CPLUS_LEGACY_C
#error "compatibility fixture must be built in LEGACY C mode"
#endif
#include <stdio.h>
#include <stddef.h>

struct S { char c; int i; short s; };
union U { int i; char c[4]; double d; };

int main(void) {
    printf("%d %d %d\n", (int)sizeof(struct S),
                         (int)offsetof(struct S, i),
                         (int)offsetof(struct S, s));
    printf("%d %d %d\n", (int)sizeof(union U),
                         (int)sizeof(((struct S *)0)->c),
                         (int)sizeof(((struct S *)0)->i));
    printf("%d %d\n", (int)(offsetof(struct S, c) == 0),
                      (int)(sizeof(union U) >= sizeof(int)));
    return 0;
}
