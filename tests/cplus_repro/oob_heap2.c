#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

/* Direct indexing into a compile-time-known local array of length 1. */
int main(void) {
    int buf[1];
    size_t size = 5;
    if (size < 0) {
        return 1;
    }
    for (int x = 0; x < 10; x++) {
        buf[x] = x + 10; // overflow: buf has length 1, x runs 0..9
    }
    printf("buf=%d\n", buf[0]);
    return 0;
}
