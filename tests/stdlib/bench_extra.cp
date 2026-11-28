/* Extra benchmarks: JSON parsing, file I/O and allocation throughput through
 * the C+ wrappers (compare with bench_extra.c doing the same work in C). */
#include <stdio.h>
#include <time.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"
#include "stdlib/file.hp"

static double ms_since(struct timespec a, struct timespec b) {
    return (b.tv_sec - a.tv_sec) * 1000.0 + (b.tv_nsec - a.tv_nsec) / 1000000.0;
}

int main(void) {
    struct timespec t0;
    struct timespec t1;
    error err = error();
    const char *text =
        "{\"name\":\"cplus\",\"stars\":5,\"ratio\":1.5,\"ok\":true,"
        "\"tags\":[\"a\",\"b\",\"c\",\"d\",\"e\",\"f\",\"g\",\"h\"]}";
    long long sink = 0;
    int i;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (i = 0; i < 20000; i++) {
        json doc = json(text, &err);
        if (doc.valid()) {
            json_view root = doc.root();
            json_view stars = root.member("stars");
            int value = 0;
            stars.get_int(&value, &err);
            sink += value;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double json_ms = ms_since(t0, t1);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (i = 0; i < 200; i++) {
        file in = file("tests/stdlib/data/sample.json", "r");
        char buf[512];
        if (in.is_open()) {
            int rc;
            while ((rc = in.read(buf, sizeof(buf), &err)) == STDLIB_OK)
                sink += buf[0];
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double file_ms = ms_since(t0, t1);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (i = 0; i < 200000; i++) {
        char *block = (char *)cplus_alloc(64);
        block[0] = (char)i;
        sink += block[0];
        cplus_release(block);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double alloc_ms = ms_since(t0, t1);

    printf("%.3f %.3f %.3f %lld\n", json_ms, file_ms, alloc_ms, sink);
    return 0;
}
