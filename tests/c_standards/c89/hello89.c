/* hello89.c -- a deliberately strict ISO C90 (ANSI C) program.
 *
 * Constraints observed here:
 *   - block comments only (no // comments)
 *   - all declarations appear at the start of their enclosing block
 *   - no C99 types (<stdint.h>, long long, _Bool, etc.)
 *   - no C99 initializers, VLAs, compound literals, or mixed declarations
 *
 * Reference build:
 *   /usr/bin/clang -std=c89 -pedantic-errors hello89.c -o hello89.ref
 */

#include <stdio.h>

#define ROWS 3

static int add(int a, int b)
{
    return a + b;
}

int main(void)
{
    int i;
    int total;
    const char *msg;

    msg = "C89/ISO C90 source compiled by the C+ driver";
    total = 0;
    for (i = 0; i < ROWS; i++) {
        total = add(total, i);
        printf("line %d: %s\n", i, msg);
    }
    printf("total=%d\n", total);
    return 0;
}

