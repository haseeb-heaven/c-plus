/* Bug 619 (unsigned underflow in an array index): subtracting one from
   zero wraps to UINT_MAX and produces an out-of-bounds negative index. */
#include <stdio.h>

int main(void) {
    int a[4] = {1, 2, 3, 4};
    unsigned u = 0;
    printf("%d\n", a[u - 1u]);
    return 0;
}
