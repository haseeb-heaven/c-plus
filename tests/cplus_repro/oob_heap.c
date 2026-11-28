#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

int *get_data(int *ptr, size_t size) {
    if (size < 0) {
        return NULL;
    }
    for (int x = 0; x < 10; x++) {
        ptr[x] = x + 10; // heap overflow: ptr is a 1-byte allocation
    }
    return ptr;
}

int main(void) {
    int *ptr = malloc(1);
    size_t size = 5;
    int *data = get_data(ptr, size);
    (void)data;
    printf("ptr=%d\n", *ptr);
    return 0;
}
