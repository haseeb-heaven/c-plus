/* lc12: variadic functions -- <stdarg> argument list traversal with a
 * leading fixed argument that supplies the count.
 */
#include <stdarg.h>
#include <stdio.h>

static int sum_n(int count, ...)
{
    va_list ap;
    int i;
    int s = 0;

    va_start(ap, count);
    for (i = 0; i < count; ++i) {
        s += va_arg(ap, int);
    }
    va_end(ap);
    return s;
}

static int max_n(int count, ...)
{
    va_list ap;
    int i;
    int m;

    va_start(ap, count);
    m = va_arg(ap, int);
    for (i = 1; i < count; ++i) {
        int v = va_arg(ap, int);
        if (v > m) {
            m = v;
        }
    }
    va_end(ap);
    return m;
}

int main(void)
{
    int s = sum_n(5, 1, 2, 3, 4, 5);
    int m = max_n(4, 7, 22, 3, 15);
    int ok = 1;

    ok &= (s == 15 && m == 22);
    printf("lc12_variadic: %s sum=%d max=%d\n", ok ? "OK" : "FAIL", s, m);
    return ok ? 0 : 1;
}
