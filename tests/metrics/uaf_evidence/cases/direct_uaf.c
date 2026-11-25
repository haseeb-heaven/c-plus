/* GAP CASE 01: direct use-after-free of a raw malloc allocation.
   Documented gap: raw malloc/free UAF is not detected in DEFAULT builds.
   STRICT static expectation: rejected with a CPLUS-LIFETIME diagnostic. */
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    int *p = (int *)malloc(sizeof(int));
    if (!p) return 1;
    *p = 42;
    free(p);
    printf("%d\n", *p); /* stale dereference */
    return 0;
}
