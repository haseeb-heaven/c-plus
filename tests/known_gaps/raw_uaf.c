/* NOT DETECTED: raw free + stale dereference. */
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    int *ptr = malloc(sizeof(int));
    if (!ptr) return 1;
    *ptr = 42;
    free(ptr);
    printf("Value: %d\n", *ptr);
    return 0;
}
