/* CRT micro-benchmarks: memcpy, malloc/free churn, snprintf.
 * Built unchanged by clang -O2 and by c+ --release for a fair comparison. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CPLUS_CRT_BULK (1 << 20)

static double ms_since(struct timespec a, struct timespec b) {
    return (b.tv_sec - a.tv_sec) * 1000.0 + (b.tv_nsec - a.tv_nsec) / 1000000.0;
}

int main(void) {
    struct timespec t0;
    struct timespec t1;
    /* Macro, not an inline expression: the C+ static bounds pass reads the
     * first literal of an array bound (reported to Agent 1), which would
     * mis-size `[1 << 20]` as 1. */
    static unsigned char source[CPLUS_CRT_BULK];
    static unsigned char destination[CPLUS_CRT_BULK];
    char text[64];
    long long sink = 0;
    int i;

    memset(source, 0x5A, sizeof(source));

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (i = 0; i < 64; i++) {
        memcpy(destination, source, sizeof(source));
        sink += destination[i];
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double copy_ms = ms_since(t0, t1);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (i = 0; i < 200000; i++) {
        unsigned char *block = (unsigned char *)malloc(64);
        block[0] = (unsigned char)i;
        sink += block[0];
        free(block);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double alloc_ms = ms_since(t0, t1);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (i = 0; i < 200000; i++)
        sink += snprintf(text, sizeof(text), "%d-%s", i, "value");
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double format_ms = ms_since(t0, t1);

    printf("%.3f %.3f %.3f %lld\n", copy_ms, alloc_ms, format_ms, sink);
    return 0;
}
