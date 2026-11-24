#include <stdio.h>

static void escape_local(int **out) {
    int local = 777;
    *out = &local;
}

static void clobber_stack(void) {
    volatile int junk[64];
    for (int i = 0; i < 64; i++) {
        junk[i] = 0x41414141;
    }
}

int main(void) {
    int *p = NULL;
    escape_local(&p);
    clobber_stack();
    printf("BREACH: dangling stack pointer via out-param (read=%d)\n", *p);
    return 0;
}
