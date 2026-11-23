#line 1 "probe_c99.c"
/* Negative control: C99-only declaration after a statement.
 * Used to prove that -std reaches the C backend and is enforced.
 * With -std=c89 -pedantic-errors this must be rejected. */
#include <stdio.h>

int main(void)
{
    int i;
    i = 1;
    printf("i=%d\n", i);
    int j = 2; /* C99 mixed declaration: illegal in C90 */
    printf("j=%d\n", j);
    return 0;
}