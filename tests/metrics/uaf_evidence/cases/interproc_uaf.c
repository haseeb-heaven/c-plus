/* GAP CASE 07: use-after-free through a function parameter.
   The borrow checker does not track pointers passed to callees, so this
   case is ACCEPTED by STRICT static analysis. ASan must catch it at run time. */
#include <stdio.h>
#include <stdlib.h>

static void use(int *q) {
    printf("%d\n", *q);
}

int main(void) {
    int *p = (int *)malloc(sizeof(int));
    if (!p) return 1;
    *p = 21;
    free(p);
    use(p); /* stale pointer passed out of the analyzing function */
    return 0;
}
