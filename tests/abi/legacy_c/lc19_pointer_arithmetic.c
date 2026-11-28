/* lc19: pointer arithmetic -- offsetting, difference, array decay and
 * string walking with char pointers.
 */
#include <stddef.h>
#include <stdio.h>

int main(void)
{
    int a[6] = {10, 20, 30, 40, 50, 60};
    int *p = a;
    const char *s = "hello";
    const char *q = s;
    int ok = 1;

    ok &= (*(p + 2) == 30);
    ok &= (p[4] == 50);
    ok &= ((p + 5) - p == 5);
    ++p;
    ok &= (*p == 20);
    while (*q != '\0') {
        ++q;
    }
    ok &= ((size_t)(q - s) == 5);

    printf("lc19_pointer_arithmetic: %s third=%d diff=%ld slen=%lu\n",
           ok ? "OK" : "FAIL", *(a + 2), (long)((a + 5) - a),
           (unsigned long)(q - s));
    return ok ? 0 : 1;
}
