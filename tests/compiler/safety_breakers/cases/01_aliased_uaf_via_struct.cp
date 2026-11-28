#include <stdio.h>
#include <stdlib.h>

struct Box {
    int *p;
};

int main(void) {
    struct Box b;
    b.p = (int *)malloc(sizeof(int));
    if (!b.p) return 1;
    *b.p = 42;
    int *dangling = b.p;
    free(b.p);
    *dangling = 0x1337;
    printf("BREACH: wrote to freed memory via struct alias: %d\n", *dangling);
    return 0;
}
