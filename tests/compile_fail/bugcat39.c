/* Bug 655: restrict violation via aliasing overlapping ranges. */
#include <stddef.h>

void add(int *restrict dst, const int *restrict src, int n) {
    for (int i = 0; i < n; i++)
        dst[i] += src[i];
}

int main(void) {
    int values[4] = {1, 2, 3, 4};
    add(values + 1, values, 3);
    return 0;
}
