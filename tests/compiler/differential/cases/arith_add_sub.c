#include <stdio.h>
int main(void) {
    int a = 83619, b = 77332, c = 91187, d = 70800;
    printf("add=%d sub=%d add2=%d sub2=%d\n", a + b, a - b, c + d, c - d);
    printf("neg=%d id=%d\n", -a, b + 0);
    return 0;
}
