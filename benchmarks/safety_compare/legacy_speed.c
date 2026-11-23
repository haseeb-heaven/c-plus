/* benchmarks/safety_compare/legacy_speed.c
 *
 * Plain-C ("legacy C") twin of the C+ compiled loop/nested workloads in this
 * directory.  It reproduces the exact arithmetic of loop.c and nested.c in a
 * single plain C source, with byte-identical checksums, so it can be compiled
 * with /usr/bin/clang -O2 and timed against the C+ safe build:
 *
 *   /usr/bin/clang -O2 benchmarks/safety_compare/legacy_speed.c -o <out>
 *   <out> loop     -> 19999999900000000   (sum 0..199999999)
 *   <out> nested   -> 16000000            (4000*4000; i*j - j*i == 0)
 *
 * WHY THIS FILE EXISTS
 * --------------------
 * loop.c and nested.c are themselves plain C.  Feeding them to
 * `c+ build --strict --sanitize=none` emits C that is byte-for-byte identical
 * to the source (verified with --emit-c): these pure integer loops contain no
 * C+ construct for the safe mode to instrument, so the C+ build adds no
 * bounds/null/ownership checks.  The two builds are therefore *not* a meaningful
 * "safe vs legacy" pair by themselves.  This file is the clean, self-contained
 * legacy-C reference with the same checksums; see legacy_speed_NOTES.md for the
 * honest interpretation and the exact commands used.
 *
 * Nothing here is a claim about language superiority: both variants run the
 * same three lines of arithmetic, which -O2 folds into a closed form.
 */
#include <stdio.h>
#include <string.h>

static long long run_loop(void) {
    volatile long long sink = 0;
    long long sum = 0;
    for (long long i = 0; i < 200000000LL; ++i)
        sum += i;
    sink = sum;
    return sink;
}

static long long run_nested(void) {
    volatile long long sink = 0;
    long long acc = 0;
    for (int i = 0; i < 4000; ++i)
        for (int j = 0; j < 4000; ++j)
            acc += (long long)i * j - (long long)j * i + 1;
    sink = acc;
    return sink;
}

int main(int argc, char **argv) {
    long long out;

    if (argc != 2)
        return 2;
    if (strcmp(argv[1], "loop") == 0)
        out = run_loop();
    else if (strcmp(argv[1], "nested") == 0)
        out = run_nested();
    else
        return 2;

    printf("%lld\n", out);
    return 0;
}
