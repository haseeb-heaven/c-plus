#include <stdio.h>

static volatile int idx = 7;
static int a[4] = {1, 2, 3, 4};

int main(void) {
    printf("v=%d\n", a[idx]);
    return 0;
}
