/* Regressions for the checked close's double-close cache (AUDIT #1 and #3).
 *
 * The historical implementation deleted open-addressed entries by writing
 * NULL, which broke probe chains, and let a stream that was in BOTH the live
 * and closed tables fall through to host fclose() on a freed FILE*.  It also
 * wrapped its fixed caches, silently forgetting old double-close markers.
 */
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "safe_io.h"
#include "safe_error.h"

/* Mirrors hash_stream() in stdlib/safe/src/safe_io.c (512 slots). */
static size_t cache_bucket(const FILE *f) {
    uintptr_t x = (uintptr_t)f;
    x ^= x >> 33;
    x *= (uintptr_t)0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    return (size_t)(x & 511u);
}

static int count_fds(void) {
    DIR *d = opendir("/dev/fd");
    struct dirent *e;
    int n = 0;
    if (!d) return -1;
    while ((e = readdir(d)) != NULL) { (void)e; ++n; }
    closedir(d);
    return n;
}

/* Baseline: valid close, then a second close is refused without a second
 * host fclose(). */
static void test_basic_double_close(void) {
    FILE *f = cplus_fopen_s("/tmp/cplus_unit_fclose.tmp", "wb");
    assert(f != NULL);
    assert(cplus_fclose_s(f) == 0);
    errno = 0;
    assert(cplus_fclose_s(f) == EOF && errno == EINVAL);
    unlink("/tmp/cplus_unit_fclose.tmp");
}

/* AUDIT #1: two FILE* addresses in the same checked hash bucket must both
 * stay marked closed, and a second close of either must be refused.
 *
 * The old subcase tried to force the host allocator to reuse a freed FILE*
 * address, which on glibc could hand back a live chunk and perform a host
 * double-free inside the test itself.  Here all candidate streams are held
 * open at once, so every address is distinct and never recycled; a colliding
 * pair is guaranteed by the pigeonhole principle (600 addresses, 512 buckets).
 * The checked tracker is validated directly through cplus_io_stream_is_closed()
 * and the double-close refusal -- every construction below runs on every
 * platform and actually exercises the cache.
 * The deterministic reopen/tombstone variant (stubbed fopen/fclose) lives in
 * test_io_stream_cache.c. */
static void test_collision_reopen(void) {
    enum { POOL = 600 }; /* > 512 buckets: a collision must exist */
    FILE *pool[POOL];
    size_t bucket[POOL];
    FILE *a = NULL, *b = NULL;
    int i, j, before, after;

    before = count_fds();
    for (i = 0; i < POOL; ++i) {
        pool[i] = fopen("/dev/null", "r");
        assert(pool[i] != NULL);
        bucket[i] = cache_bucket(pool[i]);
    }
    for (i = 0; i < POOL && !b; ++i) {
        for (j = i + 1; j < POOL; ++j) {
            if (bucket[i] == bucket[j]) { a = pool[i]; b = pool[j]; break; }
        }
    }
    assert(a != NULL && b != NULL && a != b);
    assert(cache_bucket(a) == cache_bucket(b));

    /* Insert both markers: B probes past A in the shared bucket. */
    assert(cplus_fclose_s(a) == 0);
    assert(cplus_fclose_s(b) == 0);

    /* Direct query of the library's closed-stream tracking: both markers
     * remain reachable through the collision chain. */
    assert(cplus_io_stream_is_closed(a) == 1);
    assert(cplus_io_stream_is_closed(b) == 1);
    assert(cplus_safe_stream_was_closed(a) == 1);
    assert(cplus_safe_stream_was_closed(b) == 1);

    /* A second close of either is refused before reaching host fclose(), so
     * no freed FILE* is ever passed to the host (no UAF, no double-free). */
    errno = 0;
    assert(cplus_fclose_s(a) == EOF && errno == EINVAL);
    errno = 0;
    assert(cplus_fclose_s(b) == EOF && errno == EINVAL);

    /* The remaining raw streams were never registered; release them once. */
    for (i = 0; i < POOL; ++i)
        if (pool[i] != a && pool[i] != b) assert(fclose(pool[i]) == 0);

    after = count_fds();
    if (before >= 0 && after >= 0) assert(after <= before + 2);
}

/* AUDIT #3: 300 sequential closes (more than the plain layer's 256-entry
 * ring, exercising tombstone/compaction churn) must not lose the ability to
 * refuse a second close of an early stream.  A second open/close wave checks
 * that recycled addresses are handled without leaking descriptors. */
static void test_300_close_stress(void) {
    enum { N = 300 };
    FILE *arr[N];
    int before = count_fds(), after;
    int i;
    for (i = 0; i < N; ++i) {
        arr[i] = cplus_fopen_s("/dev/null", "r");
        assert(arr[i] != NULL);
    }
    for (i = 0; i < N; ++i) assert(cplus_fclose_s(arr[i]) == 0);

    errno = 0;
    assert(cplus_fclose_s(arr[0]) == EOF && errno == EINVAL);

    for (i = 0; i < N; ++i) {
        arr[i] = cplus_fopen_s("/dev/null", "r");
        assert(arr[i] != NULL);
    }
    for (i = 0; i < N; ++i) assert(cplus_fclose_s(arr[i]) == 0);

    errno = 0;
    assert(cplus_fclose_s(arr[N - 1]) == EOF && errno == EINVAL);
    after = count_fds();
    if (before >= 0 && after >= 0) assert(after <= before + 2);
}

/* Raw-host streams are unregistered.  A fresh raw stream at an address
 * recycled from a host-fclose()d stream is not in the closed cache and must
 * still be closable through the wrapper (no false rejection, no fd leak). */
static void test_raw_fopen_recycled(void) {
    int before = count_fds(), after;
    FILE *prime, *raw1, *raw2 = NULL;
    int i;
    /* Clear any closed marker left at the recyclable address by earlier
     * tests: a wrapper open unmarks it, host fclose leaves it unmarked. */
    prime = cplus_fopen_s("/dev/null", "r");
    assert(prime != NULL);
    assert(fclose(prime) == 0);
    raw1 = fopen("/dev/null", "r");
    assert(raw1 != NULL);
    assert(fclose(raw1) == 0); /* host close: no wrapper marker recorded */
    for (i = 0; i < 64 && !raw2; ++i) {
        FILE *f = fopen("/dev/null", "r");
        assert(f != NULL);
        if (f == raw1) raw2 = f;
        else assert(fclose(f) == 0);
    }
    assert(raw2 != NULL);
    assert(cplus_fclose_s(raw2) == 0);
    after = count_fds();
    if (before >= 0 && after >= 0) assert(after <= before + 2);
}

int main(void) {
    /* Collision test runs first while the cache has no marker for the
     * recyclable FILE* address used to reopen A. */
    test_collision_reopen();
    test_basic_double_close();
    test_300_close_stress();
    test_raw_fopen_recycled();
    puts("unit fclose_s: ok");
    return 0;
}
