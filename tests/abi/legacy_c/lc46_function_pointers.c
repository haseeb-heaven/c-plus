/*
 * lc46 - pointer patterns: function pointers, arrays of functions and
 * pointer-to-function-pointer round trips. Independent legacy-C ABI probe.
 */
#include <stdio.h>

typedef int (*op_fn)(int, int);

static int add(int a, int b) { return a + b; }
static int sub(int a, int b) { return a - b; }
static int mul(int a, int b) { return a * b; }

static const op_fn ops[3] = { add, sub, mul };
static const char ops_sym[3] = { '+', '-', '*' };

int main(void)
{
    op_fn chosen = ops[2];
    op_fn *slot = &chosen;
    int i;
    int results[3];
    int ok = 1;

    for (i = 0; i < 3; ++i) {
        results[i] = ops[i](20, 4);
    }
    if (results[0] != 24 || results[1] != 16 || results[2] != 80) {
        ok = 0;
    }
    if (*slot != mul || (*slot)(7, 6) != 42 || ops_sym[2] != '*') {
        ok = 0;
    }
    if (!ok) {
        printf("lc46 result: FAIL %d %d %d\n", results[0], results[1], results[2]);
        return 1;
    }
    printf("lc46 result: OK add=%d sub=%d mul=%d indirect=%d\n",
           results[0], results[1], results[2], (*slot)(7, 6));
    return 0;
}
