/* GAP CASE 08: use-after-free after a void* cast breaks alias tracking.
   ACCEPTED by STRICT static analysis; ASan must catch it at run time. */
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    int *p = (int *)malloc(sizeof(int));
    if (!p) return 1;
    *p = 51;
    void *v = (void *)p; /* cast hides the alias from the analyzer */
    free(p);
    printf("%d\n", *(int *)v); /* stale dereference */
    return 0;
}
