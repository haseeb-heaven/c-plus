/* GAP CASE 06: use-after-realloc via a stale original pointer.
   STRICT static expectation: rejected; raw realloc result needs explicit
   success/failure handling (CPLUS-ALIAS). */
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    int *p = (int *)malloc(sizeof(int));
    if (!p) return 1;
    *p = 13;
    int *q = (int *)realloc(p, sizeof(int) * 4);
    if (!q) return 1;
    printf("%d\n", *p); /* p is stale after successful realloc */
    free(q);
    return 0;
}
