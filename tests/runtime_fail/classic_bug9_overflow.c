/* Bug 9: signed integer overflow. */
#include <limits.h>
#include <stdio.h>
int main(void) {
    int max = INT_MAX;
    int overflowed = max + 1;
    printf("%d\n", overflowed);
    return 0;
}
