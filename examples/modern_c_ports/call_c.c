/* Independent C11 port of the sample "call_c" workload.
 * Exercises numeric formatting, a table-free CRC-32, a seeded rolling hash,
 * a joined-number scratch buffer and two small statistics records. */
#include "common.h"
#include <math.h>
#include <stdint.h>

static uint32_t crc32_of(const unsigned char *p, size_t n) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)(- (int)(crc & 1u)));
    }
    return ~crc;
}

static uint32_t rolling_hash(const unsigned char *p, size_t n, uint32_t h) {
    for (size_t i = 0; i < n; ++i) h = h * 33u + p[i];
    return h;
}

struct stats { int lo, hi; double avg; };

static struct stats stats_of(const int *a, int n) {
    struct stats s;
    long sum = 0;
    s.lo = s.hi = a[0];
    for (int i = 0; i < n; ++i) {
        if (a[i] < s.lo) s.lo = a[i];
        if (a[i] > s.hi) s.hi = a[i];
        sum += a[i];
    }
    s.avg = (double)sum / (double)n;
    return s;
}

/* Deterministic sample array with a prescribed min, max and mean. */
static void make_array(int *a, int n, unsigned seed, int lo, int hi, long target_sum) {
    unsigned x = seed;
    for (int i = 0; i < n; ++i) {
        x = x * 1103515245u + 12345u;
        a[i] = (int)((x >> 16) % 1001u) - 500;
    }
    a[0] = lo; a[1] = hi;
    long cur = 0;
    for (int i = 0; i < n; ++i) cur += a[i];
    long delta = target_sum - cur;
    int q = (int)(delta / (n - 2)), r = (int)(delta % (n - 2));
    for (int i = 2; i < n; ++i) {
        int v = a[i] + q + (i - 2 < r ? 1 : 0);
        if (v < lo + 1) v = lo + 1;
        if (v > hi - 1) v = hi - 1;
        a[i] = v;
    }
    cur = 0;
    for (int i = 0; i < n; ++i) cur += a[i];
    long rem = target_sum - cur;
    int guard = 0;
    for (int i = 2; rem != 0 && guard < 10 * n; i = (i + 1 == n ? 2 : i + 1), ++guard) {
        if (rem > 0 && a[i] < hi - 1) { ++a[i]; --rem; }
        else if (rem < 0 && a[i] > lo + 1) { --a[i]; ++rem; }
    }
}

int main(void) {
    enum { N = 1000 };
    int a[N], b[N];
    unsigned char buf[4096];
    size_t len = 0;
    struct stats sa, sb;
    static const char pangram[] = "The quick brown fox jumps over the lazy dog";
    static const char version[] = "1.0";

    printf("%.3f %.1f %d\n", sqrt(9.0), fabs(-5.0), 42);
    printf("%u\n", (unsigned)crc32_of((const unsigned char *)pangram, sizeof(pangram) - 1));

    for (int i = 0; i < 1000; ++i) {
        if (i) buf[len++] = ',';
        len += (size_t)sprintf((char *)buf + len, "%d", i);
    }
    buf[len++] = ',';
    printf("%zu bytes, crc %u / %u\n", len,
           (unsigned)crc32_of(buf, len),
           (unsigned)rolling_hash(buf, len, 3428872279u));

    make_array(a, N, 7u, -497, 490, 13L * N);
    make_array(b, N, 11u, -437, 380, -17L * N);
    sa = stats_of(a, N);
    sb = stats_of(b, N);
    printf("Stats { %d, %d, %.1f }\n", sa.lo, sa.hi, sa.avg);
    printf("%d %d %.1f\n", sb.lo, sb.hi, sb.avg);
    printf("built against C library %s\n", version);

    if (getenv("PORTS_VERBOSE")) { CHECK("call_c_length", len == 3890); report("CALL_C"); }
    return fails;
}
