#include <stdio.h>
int main(void) {
    int s = 8807;
    unsigned u = 590u;
    long l = 7813L;
    printf("cmp_su=%d cmp_us=%d\n", s < (int)u, u < (unsigned)s);
    printf("cast=%lu sum=%ld\n", (unsigned long)u, (long)s + l);
    return 0;
}
