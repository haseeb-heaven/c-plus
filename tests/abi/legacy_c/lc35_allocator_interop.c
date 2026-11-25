/*
 * lc35 - allocator interop: malloc / realloc / free across size changes.
 *
 * Independent legacy-C ABI probe: verify contents are preserved by realloc,
 * growth is usable, and the shrunk block still holds the prefix.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    unsigned int *p;
    unsigned int i;
    int ok = 1;

    p = (unsigned int *)malloc(4u * sizeof(*p));
    if (p == NULL) {
        printf("lc35 result: FAIL malloc\n");
        return 1;
    }
    for (i = 0; i < 4u; ++i) {
        p[i] = 100u + i;
    }
    p = (unsigned int *)realloc(p, 8u * sizeof(*p));
    if (p == NULL) {
        printf("lc35 result: FAIL realloc grow\n");
        return 1;
    }
    for (i = 4u; i < 8u; ++i) {
        p[i] = 200u + i;
    }
    for (i = 0; i < 8u; ++i) {
        if (p[i] != (i < 4u ? 100u + i : 200u + i)) {
            ok = 0;
        }
    }
    p = (unsigned int *)realloc(p, 3u * sizeof(*p));
    if (p == NULL) {
        printf("lc35 result: FAIL realloc shrink\n");
        return 1;
    }
    for (i = 0; i < 3u; ++i) {
        if (p[i] != 100u + i) {
            ok = 0;
        }
    }
    free(p);
    if (!ok) {
        printf("lc35 result: FAIL content mismatch\n");
        return 1;
    }
    printf("lc35 result: OK grew=8 shrunk=3 first=100u last=102u\n");
    return 0;
}
