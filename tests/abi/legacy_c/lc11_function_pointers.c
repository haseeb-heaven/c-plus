/* lc11: function pointers -- a dispatch table of binary operations. */
#include <stdio.h>

typedef int (*binop_fn)(int, int);

static int add(int a, int b) { return a + b; }
static int sub(int a, int b) { return a - b; }
static int mul(int a, int b) { return a * b; }

int main(void)
{
    binop_fn ops[3] = {add, sub, mul};
    int r0 = ops[0](9, 4);
    int r1 = ops[1](9, 4);
    int r2 = ops[2](9, 4);
    int ok = 1;

    ok &= (r0 == 13 && r1 == 5 && r2 == 36);
    ok &= (sizeof(binop_fn) == sizeof(void *));

    printf("lc11_function_pointers: %s r=%d,%d,%d fpsize=%lu\n",
           ok ? "OK" : "FAIL", r0, r1, r2,
           (unsigned long)sizeof(binop_fn));
    return ok ? 0 : 1;
}
