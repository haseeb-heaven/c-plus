/* Designated initializers (GNU extension supported by the gnu89 backend). */
#ifndef CPLUS_LEGACY_C
#error "compatibility fixture must be built in LEGACY C mode"
#endif
#include <stdio.h>

struct Point { int x, y, z; };

int main(void) {
    struct Point p = { .z = 30, .x = 10 };   /* y == 0 */
    int a[6] = { [4] = 44, [1] = 11 };        /* other elements == 0 */

    printf("%d %d %d\n", p.x, p.y, p.z);
    printf("%d %d %d %d %d %d\n",
           a[0], a[1], a[2], a[3], a[4], a[5]);
    return 0;
}
