/* Deterministic stream-cache regressions using stubbed host fopen/fclose.
 *
 * The checked layer only cares about FILE* identity: cplus_fopen_s() records
 * whatever fopen() returns, and cplus_fclose_s() records the close marker and
 * then calls fclose().  Interposing both symbols in THIS test binary gives us
 * a controlled FILE* sequence, so we can exercise the collision/tombstone
 * probe chain and growth past 512 entries without depending on how a host
 * allocator recycles addresses and without ever calling fclose() on a freed
 * FILE* (the reason the old collision test skipped Linux).
 */
#define _POSIX_C_SOURCE 200112L
#define CPLUS_SAFE_NO_UNPREFIXED_MACROS
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include "safe_io.h"
#include "safe_error.h"

#define CACHE_SLOTS 512u
#define FAKE_COUNT 8192u

static FILE g_fake[FAKE_COUNT];
static FILE *g_sequence[16];
static size_t g_seq_len;
static size_t g_seq_pos;
static size_t g_fclose_calls;

/* Host interposition: calls from libcplus_safecrt.a resolve here too. */
FILE *fopen(const char *name, const char *mode) {
    (void)name;
    (void)mode;
    assert(g_seq_pos < FAKE_COUNT);
    if (g_seq_pos < g_seq_len) return g_sequence[g_seq_pos++];
    return &g_fake[g_seq_pos++];
}

int fclose(FILE *stream) {
    (void)stream;
    ++g_fclose_calls;
    return 0; /* never dereference the fake FILE*, never free anything */
}

/* Mirrors hash_stream() in safe_io.c while the cache has its initial 512
 * slots, so a chosen pair really does collide in the library's table. */
static size_t cache_bucket(const FILE *f) {
    uintptr_t x = (uintptr_t)f;
    x ^= x >> 33;
    x *= (uintptr_t)0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    return (size_t)(x & (CACHE_SLOTS - 1u));
}

static void reset_stub(void) {
    g_seq_len = 0;
    g_seq_pos = 0;
    g_fclose_calls = 0;
}

/* A and B share a hash bucket.  Closing both, then reopening at A's exact
 * address must turn A's marker into a tombstone WITHOUT hiding B's marker
 * further along the probe chain.  A second close of B must be refused and
 * must not reach the host fclose(). */
static void test_collision_reopen_tombstone(void) {
    FILE *a = &g_fake[0], *b = NULL, *reopened;
    size_t i, calls;

    for (i = 1; i < FAKE_COUNT; ++i) {
        if (cache_bucket(&g_fake[i]) == cache_bucket(a)) { b = &g_fake[i]; break; }
    }
    assert(b != NULL && b != a);

    reset_stub();
    g_sequence[0] = a;
    g_sequence[1] = b;
    g_sequence[2] = a; /* simulate the allocator reissuing A's address */
    g_seq_len = 3;

    assert(cplus_fopen_s("a", "r") == a);
    assert(cplus_fclose_s(a) == 0);
    assert(cplus_fopen_s("b", "r") == b);
    assert(cplus_fclose_s(b) == 0);
    assert(g_fclose_calls == 2);

    assert(cplus_io_stream_is_closed(a) == 1);
    assert(cplus_io_stream_is_closed(b) == 1);

    calls = g_fclose_calls;
    errno = 0;
    assert(cplus_fclose_s(a) == EOF && errno == EINVAL);
    errno = 0;
    assert(cplus_fclose_s(b) == EOF && errno == EINVAL);
    assert(g_fclose_calls == calls); /* refused closes never reach the host */

    /* Reopen at A's address: clears A's marker with a tombstone in the same
     * bucket, leaving B's marker reachable past it. */
    reopened = cplus_fopen_s("a-again", "r");
    assert(reopened == a);
    assert(cplus_io_stream_is_closed(a) == 0);
    assert(cplus_io_stream_is_closed(b) == 1);

    errno = 0;
    assert(cplus_fclose_s(b) == EOF && errno == EINVAL);
    assert(g_fclose_calls == calls);
    assert(cplus_fclose_s(reopened) == 0);
    assert(g_fclose_calls == calls + 1);
}

/* Issue #70: more than 512 distinct closed FILE* values must all close
 * successfully (the old fixed table returned EOF/EMFILE forever after 512). */
static void test_many_distinct_closes(void) {
    enum { N = 600 };
    size_t i, calls;

    reset_stub();
    for (i = 0; i < N; ++i) {
        FILE *f = cplus_fopen_s("many", "r");
        assert(f != NULL);
        errno = 0;
        assert(cplus_fclose_s(f) == 0);
        assert(errno != EMFILE);
        assert(cplus_io_stream_is_closed(f) == 1);
    }
    assert(g_fclose_calls == N);
    calls = g_fclose_calls;
    errno = 0;
    assert(cplus_fclose_s(&g_fake[0]) == EOF && errno == EINVAL);
    assert(g_fclose_calls == calls);
}

/* Issue #70: the live table must grow too -- >512 simultaneously open
 * checked streams must all register and then all close. */
static void test_many_live_streams(void) {
    enum { N = 600 };
    FILE *fps[N];
    size_t i, calls;

    reset_stub();
    for (i = 0; i < N; ++i) {
        fps[i] = cplus_fopen_s("live", "r");
        assert(fps[i] != NULL);
        errno = 0;
        assert(errno != EMFILE);
    }
    assert(cplus_safe_open_stream_count() == N);

    for (i = 0; i < N; ++i) {
        errno = 0;
        assert(cplus_fclose_s(fps[i]) == 0);
        assert(errno != EMFILE);
    }
    assert(cplus_safe_open_stream_count() == 0);
    assert(g_fclose_calls == N);
    calls = g_fclose_calls;
    for (i = 0; i < N; i += 100) {
        errno = 0;
        assert(cplus_fclose_s(fps[i]) == EOF && errno == EINVAL);
    }
    assert(g_fclose_calls == calls);
}

int main(void) {
    test_collision_reopen_tombstone();
    test_many_distinct_closes();
    test_many_live_streams();
    puts("test_io_stream_cache: ok");
    return 0;
}
