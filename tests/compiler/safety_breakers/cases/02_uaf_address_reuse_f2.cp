#include <stdio.h>
#include <stdlib.h>

int main(void) {
    int *a = (int *)malloc(sizeof(int));
    if (!a) return 1;
    *a = 10;
    int *stale = a;
    free(a);

    int *b = NULL;
    for (int i = 0; i < 32; i++) {
        int *cand = (int *)malloc(sizeof(int));
        if (!cand) return 1;
        *cand = 20;
        if (cand == stale) {
            b = cand;
            break;
        }
        free(cand);
    }
    if (!b) {
        printf("NO_REUSE: allocator did not reuse address\n");
        return 0;
    }
    *stale = 9999;
    printf("BREACH: silent corruption via reused address (F2): *b=%d\n", *b);
    free(b);
    return 0;
}
