/* SAFE CONTROL 12: alias is used BEFORE free; value is snapshotted first.
   Must be accepted by STRICT and run cleanly under ASan. */
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    int *p = (int *)malloc(sizeof(int));
    if (!p) return 1;
    *p = 111;
    int *q = p;
    int snapshot = *q; /* alias dereferenced while allocation is live */
    free(p);
    printf("%d\n", snapshot);
    return 0;
}
