/* lc04: one-dimensional arrays and array parameters (decay to pointers). */
#include <stddef.h>
#include <stdio.h>

static int sum_array(const int *v, size_t n)
{
    size_t i;
    int s = 0;
    for (i = 0; i < n; ++i) {
        s += v[i];
    }
    return s;
}

static void scale_array(int *v, size_t n, int k)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        v[i] *= k;
    }
}

int main(void)
{
    int a[5] = {1, 2, 3, 4, 5};
    size_t n = sizeof(a) / sizeof(a[0]);
    int ok = 1;

    ok &= (n == 5);
    ok &= (sum_array(a, n) == 15);
    scale_array(a, n, 3);
    ok &= (sum_array(a, n) == 45);
    ok &= (a[0] == 3 && a[4] == 15);

    printf("lc04_arrays: %s n=%lu sum=%d first=%d last=%d\n",
           ok ? "OK" : "FAIL", (unsigned long)n,
           sum_array(a, n), a[0], a[4]);
    return ok ? 0 : 1;
}
