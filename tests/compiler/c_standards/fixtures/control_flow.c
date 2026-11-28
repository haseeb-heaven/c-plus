/* Deterministic control flow; valid in every C standard under test. */
#include <stdio.h>

int main(void)
{
    int i;
    int sum = 0;

    for (i = 0; i < 10; ++i) {
        if (i % 2)
            continue;
        sum += i;
    }
    printf("sum=%d\n", sum);
    switch (sum) {
    case 20:
        printf("case=twenty\n");
        break;
    case 21:
        printf("case=twentyone\n");
        break;
    default:
        printf("case=other\n");
        break;
    }
    i = 0;
    while (i < 3) {
        sum += i;
        ++i;
    }
    printf("after=%d\n", sum);
    return 0;
}
