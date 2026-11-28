/* lc13: callbacks -- comparator passed to qsort and a visitor callback
 * applied element-by-element through a function pointer.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a;
    int y = *(const int *)b;
    return (x > y) - (x < y);
}

static void visit_double(int *v)
{
    *v *= 2;
}

static void for_each(int *v, size_t n, void (*fn)(int *))
{
    size_t i;
    for (i = 0; i < n; ++i) {
        fn(&v[i]);
    }
}

int main(void)
{
    int a[5] = {5, 3, 1, 4, 2};
    int ok = 1;

    qsort(a, 5, sizeof(a[0]), cmp_int);
    ok &= (a[0] == 1 && a[1] == 2 && a[4] == 5);
    for_each(a, 5, visit_double);
    ok &= (a[0] == 2 && a[4] == 10);

    printf("lc13_callbacks: %s sorted=%d,%d,%d,%d,%d\n",
           ok ? "OK" : "FAIL", a[0], a[1], a[2], a[3], a[4]);
    return ok ? 0 : 1;
}
