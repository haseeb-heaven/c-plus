/* Benchmark: C+ containers (safe RAII) doing the same work as bench_plain.c. */
#include <stdio.h>
#include <time.h>
#include "cplus/cplus.h"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"

static double ms_since(struct timespec a, struct timespec b) {
    return (b.tv_sec - a.tv_sec) * 1000.0 + (b.tv_nsec - a.tv_nsec) / 1000000.0;
}

int main(void) {
    struct timespec t0;
    struct timespec t1;
    long long sum = 0;
    int i;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    vector v = vector();
    for (i = 0; i < 200000; i++)
        v.push_back(i);
    for (i = 0; i < 200000; i++)
        sum += v.at((size_t)i);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double vec_ms = ms_since(t0, t1);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (i = 0; i < 20000; i++) {
        string s = string("a");
        s.append("bbbbbbbbbb");
        sum += (long long)s.size();
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double str_ms = ms_since(t0, t1);

    printf("%.3f %.3f %lld\n", vec_ms, str_ms, sum);
    return 0;
}
