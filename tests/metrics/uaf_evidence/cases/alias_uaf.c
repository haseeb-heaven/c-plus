/* GAP CASE 02: use-after-free through a simple local alias.
   STRICT static expectation: rejected; alias creation is tracked. */
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    int *p = (int *)malloc(sizeof(int));
    if (!p) return 1;
    *p = 7;
    int *q = p; /* alias created before free */
    free(p);
    printf("%d\n", *q); /* stale dereference through alias */
    return 0;
}
