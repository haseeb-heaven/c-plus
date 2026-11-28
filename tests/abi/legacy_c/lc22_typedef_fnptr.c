/* lc22: typedef'd function pointers -- a predicate registry used to count
 * and sum elements of an array that satisfy a caller-supplied test.
 */
#include <stddef.h>
#include <stdio.h>

typedef int (*predicate_fn)(int);

static int is_even(int x) { return (x % 2) == 0; }
static int is_positive(int x) { return x > 0; }

static void tally(const int *v, size_t n, predicate_fn pred,
                  int *count, int *sum)
{
    size_t i;
    *count = 0;
    *sum = 0;
    for (i = 0; i < n; ++i) {
        if (pred(v[i])) {
            ++*count;
            *sum += v[i];
        }
    }
}

int main(void)
{
    int data[6] = {-2, 1, 2, 3, 4, 5};
    predicate_fn preds[2] = {is_even, is_positive};
    int c0, s0, c1, s1;
    int ok = 1;

    tally(data, 6, preds[0], &c0, &s0);
    tally(data, 6, preds[1], &c1, &s1);
    ok &= (c0 == 3 && s0 == 4);   /* -2 + 2 + 4 */
    ok &= (c1 == 5 && s1 == 15);  /* 1 + 2 + 3 + 4 + 5 */

    printf("lc22_typedef_fnptr: %s even=%d/%d pos=%d/%d\n",
           ok ? "OK" : "FAIL", c0, s0, c1, s1);
    return ok ? 0 : 1;
}
