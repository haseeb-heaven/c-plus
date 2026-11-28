#include <stdio.h>

int main(void) {
    int a, b;

    a = (1, 2, 3);
    printf("a=%d\n", a);

    b = (a++, a + 10);
    printf("b=%d a=%d\n", b, a);

    int x = ((a = 5), (a *= 2), a + 1);
    printf("x=%d\n", x);
    return 0;
}
