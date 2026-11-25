/* GAP CASE 10: array-of-pointer use-after-free with a runtime index.
   The dynamic slot index defeats the constant-index static check, so STRICT
   accepts this. The runner invokes the binary with no argv, so i == 0.
   ASan must catch the stale dereference at run time. */
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    (void)argv;
    int **slots = (int **)malloc(sizeof(int *) * 2);
    if (!slots) return 1;
    slots[0] = (int *)malloc(sizeof(int));
    if (!slots[0]) return 1;
    *slots[0] = 81;
    int i = argc - 1; /* 0 when run with no arguments */
    free(slots[0]);
    printf("%d\n", *slots[i]); /* stale dereference, dynamic index */
    return 0;
}
