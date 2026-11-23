/*
 * lc41 - shared-library boundary simulation (part 1): exported functions with
 * external linkage, reached through a dispatch table of function pointers.
 *
 * Building this file with `-c` yields an object whose symbol table contains
 * the lc41_* functions (verified with nm in PART2_NOTES.md). At runtime the
 * table simulates a dlsym-style API surface without a platform dependency.
 */
#include <stdio.h>

/* "exported" API */
int lc41_add(int a, int b)
{
    return a + b;
}

int lc41_mul(int a, int b)
{
    return a * b;
}

const char *lc41_version(void)
{
    return "lc41-v1";
}

struct lc41_api {
    int (*add)(int, int);
    int (*mul)(int, int);
    const char *(*version)(void);
};

int main(void)
{
    struct lc41_api api = { lc41_add, lc41_mul, lc41_version };
    int sum = api.add(6, 7);
    int product = api.mul(6, 7);
    const char *version = api.version();

    if (sum != 13 || product != 42 || version == NULL ||
        version[0] != 'l' || version[2] != '4') {
        printf("lc41 result: FAIL sum=%d product=%d\n", sum, product);
        return 1;
    }
    printf("lc41 result: OK sum=%d product=%d version=%s\n",
           sum, product, version);
    return 0;
}
