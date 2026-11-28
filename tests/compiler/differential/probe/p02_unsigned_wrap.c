#include <stdio.h>
#include <limits.h>

int main(void) {
    unsigned int u = 0;
    u -= 1;
    printf("u=%u\n", u);
    u += 2;
    printf("u2=%u\n", u);

    unsigned char c = 0;
    c -= 1;
    printf("uc=%u\n", (unsigned)c);

    unsigned short s = 0;
    s -= 1;
    printf("us=%u\n", (unsigned)s);

    printf("max=%u max+1=%u\n", UINT_MAX, UINT_MAX + 1u);
    return 0;
}
