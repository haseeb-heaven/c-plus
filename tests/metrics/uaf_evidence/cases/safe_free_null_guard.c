/* SAFE CONTROL 11: free followed by null assignment and a null guard.
   No stale dereference. Must be accepted by STRICT and run cleanly under ASan
   (this is a false-positive guard for the evidence suite). */
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    int *p = (int *)malloc(sizeof(int));
    if (!p) return 1;
    *p = 101;
    free(p);
    p = NULL;
    if (p) printf("%d\n", *p); /* guard prevents the dereference */
    printf("safe\n");
    return 0;
}
