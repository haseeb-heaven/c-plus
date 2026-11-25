#include <stdio.h>

typedef unsigned long size_t_alias;
typedef int arr3_t[3];
typedef int (*binop_t)(int, int);

int add(int a, int b) { return a + b; }

int main(void) {
    typedef short my_short;
    my_short s = 1234;
    printf("s=%d\n", s);

    arr3_t a = { 1, 2, 3 };
    printf("a=%d %d %d\n", a[0], a[1], a[2]);

    binop_t op = add;
    printf("op=%d\n", op(6, 7));

    size_t_alias z = 42;
    printf("z=%lu\n", z);
    return 0;
}
