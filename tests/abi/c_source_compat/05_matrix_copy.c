/* Legacy 05 -- flat matrix copy (malloc / memset / memcpy / printf).
 *
 * A 3x4 integer matrix allocated as one block, zeroed, filled, copied, then
 * reduced to a checksum. All writes stay inside the tracked allocation.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    const int rows = 3, cols = 4;
    const size_t cells = (size_t)3 * 4;
    const size_t bytes = cells * sizeof(int);
    int *m, *copy;
    int r, c, k;
    long sum = 0;

    m = (int *)malloc(bytes);
    if (!m) return 2;
    memset(m, 0, bytes);

    k = 0;
    for (r = 0; r < rows; ++r) {
        for (c = 0; c < cols; ++c) {
            m[r * cols + c] = ++k;
        }
    }

    copy = (int *)malloc(bytes);
    if (!copy) { free(m); return 2; }
    memcpy(copy, m, bytes);

    for (k = 0; k < (int)cells; ++k) sum += copy[k];
    printf("matrix_copy: sum=%ld last=%d\n", sum, copy[cells - 1]);

    free(copy);
    free(m);
    return 0;
}
