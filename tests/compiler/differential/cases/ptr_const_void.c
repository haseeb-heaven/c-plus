#include <stdio.h>
int main(void) {
    int x = 95545;
    const int *cp = &x;
    void *vp = (void *)cp;
    const int *back = (const int *)vp;
    printf("val=%d eq=%d sizes=%zu,%zu\n", *back, cp == back,
           sizeof(cp), sizeof(vp));
    return 0;
}
