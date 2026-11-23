/* GAP CASE 09: use-after-free through an alias returned by a helper.
   Interprocedural return-value aliases are not tracked; STRICT accepts this.
   ASan must catch the stale dereference at run time. */
#include <stdio.h>
#include <stdlib.h>

static int *identity(int *x) {
    return x;
}

int main(void) {
    int *p = (int *)malloc(sizeof(int));
    if (!p) return 1;
    *p = 61;
    int *q = identity(p);
    free(p);
    printf("%d\n", *q); /* stale alias returned from helper */
    return 0;
}
