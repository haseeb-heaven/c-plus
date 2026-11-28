#include <stdio.h>
union U { int i; unsigned u; double d; };
enum E { E_ZERO = 0, E_SEVEN = 7, E_NINE = 9 };
int main(void) {
    union U v;
    v.i = 54240;
    printf("i=%d u=%u\n", v.i, v.u);
    v.d = 4523 / 2.0;
    printf("d=%.6f size=%zu\n", v.d, sizeof(union U));
    printf("enum=%d,%d,%d\n", E_ZERO, E_SEVEN, E_NINE);
    return 0;
}
