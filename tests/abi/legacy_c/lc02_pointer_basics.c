/* lc02: pointer basics -- indirection, aliasing and swap through pointers. */
#include <stdio.h>

static void swap_int(int *a, int *b)
{
    int t = *a;
    *a = *b;
    *b = t;
}

int main(void)
{
    int x = 11;
    int y = 29;
    int *p = &x;
    int ok = 1;

    swap_int(&x, &y);
    ok &= (x == 29 && y == 11);
    ok &= (*p == 29);
    *p += 3;
    ok &= (x == 32 && *p == 32);

    printf("lc02_pointer_basics: %s x=%d y=%d p=%d\n",
           ok ? "OK" : "FAIL", x, y, *p);
    return ok ? 0 : 1;
}
