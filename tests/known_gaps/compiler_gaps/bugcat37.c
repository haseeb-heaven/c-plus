/* Bug 635 (negative index on a heap buffer): a runtime-computed negative
   index reads before the start of the allocation. */
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    int *buf = malloc(4 * sizeof(int));
    if (buf) {
        int index = -1;
        printf("%d\n", buf[index]);
        free(buf);
    }
    return 0;
}
