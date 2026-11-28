/* Deterministic integer arithmetic; valid in every C standard under test. */
#include <stdio.h>

static int add(int a, int b)
{
    return a + b;
}

static int mul(int a, int b)
{
    return a * b;
}

int main(void)
{
    int a = 17;
    int b = 5;
    unsigned int u = 4000000000U;

    printf("add=%d mul=%d\n", add(a, b), mul(a, b));
    printf("div=%d mod=%d\n", a / b, a % b);
    printf("shl=%d shr=%d\n", a << 2, a >> 1);
    printf("u=%u\n", u + 1U);
    printf("neg=%d\n", -a + 1);
    return 0;
}
