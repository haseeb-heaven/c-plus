/* Array-to-pointer decay, subscripting and pointer arithmetic. */
#ifndef CPLUS_LEGACY_C
#error "compatibility fixture must be built in LEGACY C mode"
#endif
#include <stdio.h>

int main(void) {
    int a[5] = { 10, 20, 30, 40, 50 };
    int *p = a;                  /* decay */

    printf("%d %d\n", (int)(sizeof(a) / sizeof(a[0])),
                      (int)(sizeof(a) == 5 * sizeof(int)));
    printf("%d %d %d\n", a[2], *(a + 2), p[2]);
    printf("%d %d\n", (int)(p == a), (int)(&a[0] == a));
    printf("%d %d\n", (int)((p + 5) - p), *(a + 1));
    return 0;
}
