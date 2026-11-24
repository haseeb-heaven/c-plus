#include <stdio.h>

struct Ref {
    int *p;
};

static struct Ref make_dangling(void) {
    int secret = 12345;
    struct Ref r = { &secret };
    return r;
}

static void clobber_stack(void) {
    volatile int junk[64];
    for (int i = 0; i < 64; i++) {
        junk[i] = 0x22222222;
    }
}

int main(void) {
    struct Ref r = make_dangling();
    clobber_stack();
    printf("BREACH: dangling stack ptr via struct return (read=%d)\n", *r.p);
    return 0;
}
