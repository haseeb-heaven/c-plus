/* Function declarations: prototypes, forward declarations and K&R style. */
#ifndef CPLUS_LEGACY_C
#error "compatibility fixture must be built in LEGACY C mode"
#endif
#include <stdio.h>

static int twice(int x);         /* forward prototype */
static int add3(int, int, int);  /* unnamed parameters */
static int mul();                /* old-style (K&R) declaration */

int main(void) {
    printf("%d %d\n", twice(21), add3(1, 2, 3));
    printf("%d\n", mul(6, 7));
    return 0;
}

static int twice(int x) { return 2 * x; }
static int add3(int a, int b, int c) { return a + b + c; }

static int mul(a, b)
int a;
int b;
{
    return a * b;
}
