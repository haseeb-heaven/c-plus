/* Benchmark: the hand-written C equivalent of bench_compare.cp. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double ms_since(struct timespec a, struct timespec b) {
    return (b.tv_sec - a.tv_sec) * 1000.0 + (b.tv_nsec - a.tv_nsec) / 1000000.0;
}

int main(void) {
    struct timespec t0;
    struct timespec t1;
    long long sum = 0;
    int i;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    size_t used = 0;
    size_t space = 0;
    int *items = NULL;
    for (i = 0; i < 200000; i++) {
        if (used == space) {
            size_t want = space ? space * 2 : 4;
            int *fresh = (int *)malloc(sizeof(int) * want);
            if (used)
                memcpy(fresh, items, sizeof(int) * used);
            free(items);
            items = fresh;
            space = want;
        }
        items[used++] = i;
    }
    for (i = 0; i < 200000; i++)
        sum += items[i];
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double vec_ms = ms_since(t0, t1);
    free(items);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (i = 0; i < 20000; i++) {
        size_t cap = 16;
        size_t len = 1;
        char *s = (char *)malloc(cap);
        s[0] = 'a';
        s[1] = '\0';
        if (len + 11 > cap) {
            cap = len + 11;
            s = (char *)realloc(s, cap);
        }
        memcpy(s + len, "bbbbbbbbbb", 11);
        sum += (long long)strlen(s);
        free(s);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double str_ms = ms_since(t0, t1);

    printf("%.3f %.3f %lld\n", vec_ms, str_ms, sum);
    return 0;
}
