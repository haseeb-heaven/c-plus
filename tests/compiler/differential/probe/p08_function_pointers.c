#include <stdio.h>

int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }
int mul(int a, int b) { return a * b; }

int main(void) {
    int (*fp)(int, int) = add;
    printf("fp=%d\n", fp(3, 4));

    int (*ops[3])(int, int) = { add, sub, mul };
    for (int i = 0; i < 3; i++) {
        printf("ops%d=%d\n", i, ops[i](10, 2));
    }

    fp = sub;
    printf("fp2=%d\n", fp(9, 3));
    return 0;
}
