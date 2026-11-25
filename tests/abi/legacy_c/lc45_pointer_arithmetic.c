/*
 * lc45 - pointer patterns: arithmetic, difference, and array decay.
 *
 * Independent legacy-C ABI probe: walks a fixed array with pointers only,
 * computes ptrdiff via subtraction and verifies address ordering.
 */
#include <stdio.h>

int main(void)
{
    int values[5] = { 5, 10, 15, 20, 25 };
    int *p = values;
    int *end = values + 5;
    long distance = (long)(end - p);
    long sum = 0;
    int steps = 0;

    while (p < end) {
        sum += *p;
        ++p;
        ++steps;
    }
    --p; /* last element */
    if (distance != 5L || steps != 5 || sum != 75L || *p != 25 ||
        p - values != 4 || &values[4] - p != 0 ||
        *(end - 1) != 25 || values != &values[0]) {
        printf("lc45 result: FAIL dist=%ld steps=%d sum=%ld last=%d\n",
               distance, steps, sum, *p);
        return 1;
    }
    printf("lc45 result: OK dist=%ld steps=%d sum=%ld last=%d\n",
           distance, steps, sum, *p);
    return 0;
}
