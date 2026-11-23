/* GAP CASE 04: array-of-pointer use-after-free (constant slot index).
   STRICT static expectation: rejected; dereference of released slots[0]. */
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    int **slots = (int **)malloc(sizeof(int *) * 2);
    if (!slots) return 1;
    slots[0] = (int *)malloc(sizeof(int));
    if (!slots[0]) return 1;
    *slots[0] = 11;
    free(slots[0]);
    printf("%d\n", *slots[0]); /* stale dereference via array slot */
    return 0;
}
