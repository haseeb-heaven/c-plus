/* lc23: variadic ABI -- forwarding a va_list from a variadic entry point to
 * a va_list consumer, a common pattern in legacy printf-style wrappers.
 */
#include <stdarg.h>
#include <stdio.h>

static int vsum(int count, va_list ap)
{
    int i;
    int s = 0;
    for (i = 0; i < count; ++i) {
        s += va_arg(ap, int);
    }
    return s;
}

static int sum(int count, ...)
{
    va_list ap;
    int r;
    va_start(ap, count);
    r = vsum(count, ap);
    va_end(ap);
    return r;
}

static double vavg(int count, va_list ap)
{
    int i;
    double s = 0.0;
    for (i = 0; i < count; ++i) {
        s += (double)va_arg(ap, int);
    }
    return s / (double)count;
}

static double average(int count, ...)
{
    va_list ap;
    double r;
    va_start(ap, count);
    r = vavg(count, ap);
    va_end(ap);
    return r;
}

int main(void)
{
    int s = sum(4, 2, 4, 6, 8);
    double a = average(4, 2, 4, 6, 8);
    int ok = 1;

    ok &= (s == 20);
    ok &= (a == 5.0);
    printf("lc23_variadic_va_list: %s sum=%d avg=%.1f\n",
           ok ? "OK" : "FAIL", s, a);
    return ok ? 0 : 1;
}
