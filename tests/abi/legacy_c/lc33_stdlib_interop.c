/*
 * lc33 - C library interop: stdlib (qsort, strtol, abs, ldiv).
 *
 * Independent legacy-C ABI probe: callback ABI (qsort comparator), numeric
 * conversion with end-pointer validation, and integer helper routines.
 */
#include <stdio.h>
#include <stdlib.h>

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a;
    int y = *(const int *)b;
    return (x > y) - (x < y);
}

int main(void)
{
    int values[6] = { 9, -3, 7, 0, 5, 2 };
    char text[] = "  -42rest";
    char *end = NULL;
    long parsed;
    ldiv_t d;
    int i, sum = 0;

    qsort(values, 6, sizeof(values[0]), cmp_int);
    for (i = 0; i < 6; ++i) {
        sum += values[i] * values[i];
    }
    parsed = strtol(text, &end, 10);
    d = ldiv(77L, 10L);

    if (values[0] != -3 || values[1] != 0 || values[2] != 2 ||
        values[3] != 5 || values[4] != 7 || values[5] != 9 ||
        sum != 168 || parsed != -42L || end == NULL || *end != 'r' ||
        abs(-33) != 33 || d.quot != 7L || d.rem != 7L) {
        printf("lc33 result: FAIL parsed=%ld end='%c' quot=%ld rem=%ld\n",
               parsed, end != NULL ? *end : '?', d.quot, d.rem);
        return 1;
    }
    printf("lc33 result: OK min=%d max=%d sumsq=%d parsed=%ld quot=%ld rem=%ld\n",
           values[0], values[5], sum, parsed, d.quot, d.rem);
    return 0;
}
