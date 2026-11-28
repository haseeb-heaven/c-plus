#include <stdio.h>
static int add(int a, int b) { return a + b; }
static int sub(int a, int b) { return a - b; }
static int mul(int a, int b) { return a * b; }
typedef int (*op_fn)(int, int);
int main(void) {
    op_fn table[3] = {add, sub, mul};
    int a = 589, b = 677;
    printf("ops=%d,%d,%d\n", table[0](a, b), table[1](a, b), table[2](a, b));
    return 0;
}
