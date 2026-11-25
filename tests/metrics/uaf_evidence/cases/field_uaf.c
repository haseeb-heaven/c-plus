/* GAP CASE 03: use-after-free through a struct pointer field.
   STRICT static expectation: rejected; subscript of released pointer. */
#include <stdio.h>
#include <stdlib.h>

struct Box {
    int *v;
};

int main(void) {
    struct Box b;
    b.v = (int *)malloc(sizeof(int));
    if (!b.v) return 1;
    *b.v = 9;
    free(b.v);
    printf("%d\n", b.v[0]); /* stale field dereference */
    return 0;
}
