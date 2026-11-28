#include <stdint.h>
#include <stdio.h>

union Pun {
    uintptr_t addr;
    int *ptr;
};

int main(void) {
    union Pun u;
    u.addr = (uintptr_t)0xdeadbeefULL;
    volatile int x = *u.ptr;
    (void)x;
    printf("UNEXPECTED: read succeeded\n");
    return 0;
}
