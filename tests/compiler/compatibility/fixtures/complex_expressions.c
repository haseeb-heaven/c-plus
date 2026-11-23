/* Complex expressions: precedence, conditional, comma, shifts, casts. */
#ifndef CPLUS_LEGACY_C
#error "compatibility fixture must be built in LEGACY C mode"
#endif
#include <stdio.h>

int main(void) {
    int a = 5, b = 3, c = 2;
    int r1 = a + b * c;               /* 11 */
    int r2 = (a + b) * c;             /* 16 */
    int r3 = a > b ? a : b;           /* 5 */
    int r4 = (a, b, c);               /* comma operator: 2 */
    int r5 = a++ + b;                 /* 8, a becomes 6 */
    int r6 = (a << 1) | (b & 1);      /* 12 | 1 = 13 */

    printf("%d %d %d\n", r1, r2, r3);
    printf("%d %d %d\n", r4, r5, r6);
    printf("%d %d\n", (int)(a == 6), (int)(sizeof(a) == sizeof(int)));
    printf("%d\n", (a & 1) ? 1 : ((b & 1) ? 2 : 3));
    return 0;
}
