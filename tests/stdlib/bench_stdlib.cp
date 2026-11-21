/* Allocation / churn benchmark for the owning containers.
 *
 * Measures the cost of vector growth and of string construction + append, both
 * of which go through the C+ managed allocator.  Run directly:
 *   build/stdlib-tests/bench_stdlib
 */
#include <stdio.h>
#include <time.h>
#include "cplus/cplus.h"
#include "stdlib/string.hp"
#include "stdlib/vector.hp"

static double elapsed_ms(struct timespec a, struct timespec b) {
    return (b.tv_sec - a.tv_sec) * 1000.0 +
           (b.tv_nsec - a.tv_nsec) / 1000000.0;
}

int main(void) {
    struct timespec t0;
    struct timespec t1;
    long sum = 0;
    int i;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    vector v = vector();
    for (i = 0; i < 200000; i++) {
        if (v.push_back(i) != 0)
            return 1;
    }
    for (i = 0; i < 200000; i++)
        sum += v.at((size_t)i);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double vec_ms = elapsed_ms(t0, t1);
    size_t vec_cap = v.capacity();

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (i = 0; i < 20000; i++) {
        string s = string("a");
        if (s.append("bbbbbbbbbb") != 0)
            return 1;
        sum += (long)s.size();
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double str_ms = elapsed_ms(t0, t1);

    print("bench", "vec_ms", vec_ms, "vec_cap", vec_cap, "str_ms", str_ms,
          "sum", sum);
    return 0;
}
