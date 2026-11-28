/* Paired benchmark: bounds-checked table lookup under an opaque index.
 *
 * `index` is opaque (volatile / black_box), so the optimizer cannot prove the
 * masked value is in range.  The fixed-capacity array then exercises C+ bounds
 * checking against Rust's slice bounds checking on the hot path. */
#include <stdio.h>

#define SLOTS 16
#define ITERATIONS 200000000LL

int main(void) {
    volatile int seed = 3;
    int values[SLOTS];
    long long total = 0;
    long long i;

    for (i = 0; i < SLOTS; i++)
        values[i] = (int)i + 3;

    for (i = 0; i < ITERATIONS; i++) {
        int index = seed & (SLOTS - 1);
        total += values[index];
    }

    printf("bounds_sum %lld\n", total);
    return 0;
}
