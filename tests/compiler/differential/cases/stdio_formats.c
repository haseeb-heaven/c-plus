#include <stdio.h>
int main(void) {
    int i = 15503;
    unsigned u = 917u;
    long l = 36240L;
    printf("dec=%d hex=%x oct=%o\n", i, (unsigned)i, (unsigned)i);
    printf("uns=%u long=%ld char=%c str=%s\n", u, l, 'Q', "quoted");
    printf("pct=%% width=[%5d] zero=[%05d]\n", i, i);
    return 0;
}
