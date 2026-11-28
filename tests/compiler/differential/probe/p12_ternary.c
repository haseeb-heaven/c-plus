#include <stdio.h>

int main(void) {
    int a = 1, b = 2;

    int m = (a > b) ? a : b;
    printf("m=%d\n", m);

    printf("n=%d\n", a ? b : 99);
    printf("nest=%d\n", a ? (b ? 10 : 20) : 30);

    int c = 5;
    int r = (c > 3) ? c++ : c--;
    printf("assign=%d c=%d\n", r, c);
    return 0;
}
