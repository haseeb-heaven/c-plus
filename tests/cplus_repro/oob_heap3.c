#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

/* Same bug, but the write happens through a pointer parameter whose extent
   is not known at the call site: catches whether interprocedural bounds
   analysis exists. */
int *get_data(int *ptr, size_t size) {
    if (size < 0) {
        return NULL;
    }
    for (int x = 0; x < 10; x++) {
        ptr[x] = x + 10; // overflow: ptr points to a 1-element local array
    }
    return ptr;
}

int main(void) {
    int buf[1];
    size_t size = 5;
    int *data = get_data(buf, size);
    (void)data;
    printf("buf=%d\n", buf[0]);
    return 0;
}
