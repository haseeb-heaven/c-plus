#define _POSIX_C_SOURCE 200809L
#include <stdio.h>

/* BUG 737: writing through a pointer whose type does not match the object.
 * This is the same strict-aliasing error made when mapped memory is cast to
 * an incompatible type before being written. */
void bug737_mmap_wrong_type_write(void) {
    float storage = 0.0f;
    int *p = (int *)&storage;
    *p = 42;
}

int main(void) { return 0; }
