#include <stdio.h>
#include <stdlib.h>

static void write_oob(int *p, int idx) {
    p[idx] = 0x5555;
}

int main(void) {
    int *arr = (int *)malloc(4 * sizeof(int));
    if (!arr) return 1;
    write_oob(arr, 12);
    printf("BREACH: silent heap OOB write via decayed pointer param: val=%d\n", arr[12]);
    free(arr);
    return 0;
}
