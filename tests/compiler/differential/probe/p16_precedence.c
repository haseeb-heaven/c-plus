#include <stdio.h>

int main(void) {
    int a = 1, b = 2, c = 3;

    printf("r1=%d\n", a + b * c);
    printf("r2=%d\n", (a + b) * c);
    printf("r3=%d\n", a << 2 + 1);
    printf("r4=%d\n", 1 & 3 == 3);
    printf("r5=%d\n", a | b & c);
    printf("r6=%d\n", !a + 1);
    return 0;
}
