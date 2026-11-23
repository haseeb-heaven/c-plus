#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include "cplus/cplus.h"

static int g_batch;
int main(void) {
    char path[256];
    struct timespec t0, t1;
    FILE *f;
    g_batch = 7;
    snprintf(path, sizeof(path), "tests/fuzz10k/containers/results/_io_probe_%d.log", g_batch);
    f = freopen(path, "w", stdout);
    if (f == NULL) { fprintf(stderr, "freopen failed\n"); return 1; }
    clock_gettime(CLOCK_MONOTONIC, &t0);
    print("HELLO", 42);
    fflush(stdout);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    fprintf(stderr, "elapsed_ns=%lld\n", (long long)(t1.tv_sec - t0.tv_sec) * 1000000000LL + (long long)(t1.tv_nsec - t0.tv_nsec));
    fflush(stdout);
    fclose(stdout);
    return 0;
}
