/* The equivalent C for bench_extra.cp: yyjson, fopen/fread, malloc/free. */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "yyjson.h"

static double ms_since(struct timespec a, struct timespec b) {
    return (b.tv_sec - a.tv_sec) * 1000.0 + (b.tv_nsec - a.tv_nsec) / 1000000.0;
}

int main(void) {
    struct timespec t0;
    struct timespec t1;
    const char *text =
        "{\"name\":\"cplus\",\"stars\":5,\"ratio\":1.5,\"ok\":true,"
        "\"tags\":[\"a\",\"b\",\"c\",\"d\",\"e\",\"f\",\"g\",\"h\"]}";
    long long sink = 0;
    int i;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (i = 0; i < 20000; i++) {
        yyjson_doc *doc = yyjson_read(text, strlen(text), YYJSON_READ_NOFLAG);
        if (doc != NULL) {
            const yyjson_val *root = yyjson_doc_get_root(doc);
            const yyjson_val *stars = yyjson_obj_get(root, "stars");
            sink += yyjson_get_int(stars);
            yyjson_doc_free((yyjson_doc *)doc);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double json_ms = ms_since(t0, t1);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (i = 0; i < 200; i++) {
        FILE *f = fopen("tests/stdlib/data/sample.json", "r");
        if (f != NULL) {
            char buf[512];
            while (fread(buf, 1, sizeof(buf), f) > 0)
                sink += buf[0];
            fclose(f);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double file_ms = ms_since(t0, t1);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (i = 0; i < 200000; i++) {
        char *block = (char *)malloc(64);
        block[0] = (char)i;
        sink += block[0];
        free(block);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double alloc_ms = ms_since(t0, t1);

    printf("%.3f %.3f %.3f %lld\n", json_ms, file_ms, alloc_ms, sink);
    return 0;
}
