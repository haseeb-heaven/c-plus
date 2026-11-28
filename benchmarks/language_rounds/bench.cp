/* Same 2M-iteration workload as bench.rs / bench.zig / bench.goose. */
#include <stdio.h>
#include <time.h>

#include "cplus/cplus.h"

static long long now_ns(void) {
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000000000LL + t.tv_nsec;
}

int main(void) {
    const long iterations = 2000000;
    long index;
    unsigned long long checksum = 0;
    long long started_ns;
    long long ended_ns;
    struct Pair {
        long x;
        long y;
    } pair = { 1, 2 };

    started_ns = now_ns();
    for (index = 0; index < iterations; ++index) {
        pair.x = (pair.x * 3 + pair.y) & 0xffffffL;
        pair.y = (pair.y + index * 7) & 0xffffffL;
        checksum += (unsigned long long)(pair.x ^ pair.y);
    }
    ended_ns = now_ns();

    printf("CPLUS %lld %llu\n", ended_ns - started_ns, checksum);
    return checksum == 0 ? 1 : 0;
}
