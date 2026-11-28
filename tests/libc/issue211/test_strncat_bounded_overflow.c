/* Deterministic regression for issue #211: cplus_strncat_bounded() clamped its
 * source scan to dcap - dlen - 1, which made need <= dcap unconditional and
 * check_capacity() always pass, then delegated to cplus_strncat(d, s, n) with
 * the ORIGINAL unclamped n. A source longer than the remaining room overflowed
 * the destination (the issue reproducer wrote 20 bytes at d+2 of an 8-byte d).
 *
 * Every case uses a guard array placed immediately after a stack destination
 * (or the full byte contents of a tracked allocation) so both refusals and
 * exact-fit successes are checked byte-for-byte. No fault injection, timing,
 * or undefined behaviour is needed to observe the corrected behaviour. */
#include "cplus_libc.h"
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #cond);            \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

static int all_bytes(const void *p, size_t n, unsigned char v) {
    const unsigned char *b = (const unsigned char *)p;
    size_t i;
    for (i = 0; i < n; ++i)
        if (b[i] != v) return 0;
    return 1;
}

struct guarded {
    char d[8];
    unsigned char guard[32];
};

static void init_guarded(struct guarded *g, const char *prefix) {
    memset(g, 0, sizeof *g);
    strcpy(g->d, prefix);
    memset(g->guard, 0xA5, sizeof g->guard);
}

static void check_guarded(const struct guarded *g, const char *want) {
    CHECK(strcmp(g->d, want) == 0);
    CHECK(all_bytes(g->guard, sizeof g->guard, 0xA5));
}

/* Issue reproducer: dcap=8, dlen=2, room=5, n=20. The old code scanned only
 * 5 bytes, computed need=8, and the raw call appended all 20 A's at d+2. */
static void issue_reproducer(void) {
    struct guarded g;
    const char *src = "AAAAAAAAAAAAAAAAAAAA"; /* 20 bytes */
    init_guarded(&g, "hi");
    errno = 0;
    CHECK(cplus_strncat_bounded(g.d, src, 20, sizeof g.d) == NULL);
    CHECK(errno == ERANGE);
    check_guarded(&g, "hi");
}

/* One byte past the room must also be refused: scanning up to the room alone
 * hides the overflow, while scanning one byte further reveals it. */
static void one_byte_too_long(void) {
    struct guarded g;
    init_guarded(&g, "hi");
    errno = 0;
    CHECK(cplus_strncat_bounded(g.d, "AAAAAA", 20, sizeof g.d) == NULL);
    CHECK(errno == ERANGE);
    check_guarded(&g, "hi");
}

/* A caller-supplied capacity smaller than the object must be honoured too. */
static void smaller_known_capacity(void) {
    struct guarded g;
    init_guarded(&g, "hi");
    errno = 0; /* room = 5 - 2 - 1 = 2; source needs 3 bytes */
    CHECK(cplus_strncat_bounded(g.d, "ABC", 20, 5) == NULL);
    CHECK(errno == ERANGE);
    check_guarded(&g, "hi");
}

/* Valid appends still succeed: exact fit, a short source with n far larger
 * than the source, and n <= room with an unterminated tracked source. */
static void valid_appends(void) {
    struct guarded g;
    char *src;

    init_guarded(&g, "hi");
    errno = 0;
    CHECK(cplus_strncat_bounded(g.d, "AAAAA", 5, sizeof g.d) == g.d);
    CHECK(errno == 0);
    check_guarded(&g, "hiAAAAA");

    init_guarded(&g, "hi");
    errno = 0;
    CHECK(cplus_strncat_bounded(g.d, "ABC", 20, sizeof g.d) == g.d);
    CHECK(errno == 0);
    check_guarded(&g, "hiABC");

    src = (char *)cplus_malloc(8);
    CHECK(src != NULL);
    if (src != NULL) {
        memcpy(src, "ABCDEFGH", 8); /* no NUL inside the tracked source */
        init_guarded(&g, "hi");
        errno = 0;
        CHECK(cplus_strncat_bounded(g.d, src, 4, sizeof g.d) == g.d);
        CHECK(errno == 0);
        check_guarded(&g, "hiABCD");
        cplus_free(src);
    }
}

/* A completely full destination accepts only a no-op append. */
static void full_destination(void) {
    struct guarded g;
    init_guarded(&g, "abcdefg"); /* 7 chars + NUL == 8 bytes */
    errno = 0;
    CHECK(cplus_strncat_bounded(g.d, "x", 0, sizeof g.d) == g.d);
    CHECK(errno == 0);
    check_guarded(&g, "abcdefg");

    errno = 0;
    CHECK(cplus_strncat_bounded(g.d, "", 1, sizeof g.d) == g.d);
    CHECK(errno == 0);
    check_guarded(&g, "abcdefg");

    errno = 0;
    CHECK(cplus_strncat_bounded(g.d, "x", 1, sizeof g.d) == NULL);
    CHECK(errno == ERANGE);
    check_guarded(&g, "abcdefg");
}

/* Tracked heap destination: capacity comes from the allocator when known is
 * (size_t)-1, and the entire 8-byte block must stay untouched on refusal. */
static void tracked_destination(void) {
    char *d = (char *)cplus_malloc(8);
    const char *src = "AAAAAAAAAAAAAAAAAAAA";
    CHECK(d != NULL);
    if (d == NULL) return;

    memset(d, 0, 8);
    strcpy(d, "hi");
    errno = 0;
    CHECK(cplus_strncat_bounded(d, src, 20, (size_t)-1) == NULL);
    CHECK(errno == ERANGE);
    CHECK(memcmp(d, "hi\0\0\0\0\0\0", 8) == 0);

    memset(d, 0, 8);
    strcpy(d, "hi");
    errno = 0;
    CHECK(cplus_strncat_bounded(d, "AAAAA", 5, (size_t)-1) == d);
    CHECK(errno == 0);
    CHECK(memcmp(d, "hiAAAAA\0", 8) == 0);
    cplus_free(d);
}

/* Interior pointers: base+8 has 8 bytes remaining, dlen=7 and no room, so
 * only an empty append is allowed and the rest of the block is unchanged. */
static void interior_destination(void) {
    char *base = (char *)cplus_malloc(16);
    CHECK(base != NULL);
    if (base == NULL) return;
    memset(base, 'B', 15);
    base[15] = '\0';

    errno = 0;
    CHECK(cplus_strncat_bounded(base + 8, "x", 1, (size_t)-1) == NULL);
    CHECK(errno == ERANGE);
    CHECK(base[15] == '\0' && all_bytes(base, 15, 'B'));

    errno = 0;
    CHECK(cplus_strncat_bounded(base + 8, "", 1, (size_t)-1) == base + 8);
    CHECK(errno == 0);
    CHECK(base[15] == '\0' && all_bytes(base, 15, 'B'));
    cplus_free(base);
}

/* A provable source capacity bounds the delegated call: the NUL at index 2
 * ends the append even though n is 20, and no byte past it is read. */
static void tracked_source(void) {
    char *src = (char *)cplus_malloc(4);
    struct guarded g;
    CHECK(src != NULL);
    if (src == NULL) return;
    memcpy(src, "AB\0Z", 4);
    init_guarded(&g, "");
    errno = 0;
    CHECK(cplus_strncat_bounded(g.d, src, 20, sizeof g.d) == g.d);
    CHECK(errno == 0);
    check_guarded(&g, "AB");
    cplus_free(src);
}

int main(void) {
    issue_reproducer();
    one_byte_too_long();
    smaller_known_capacity();
    valid_appends();
    full_destination();
    tracked_destination();
    interior_destination();
    tracked_source();
    if (failures != 0) {
        printf("test_strncat_bounded_overflow: %d failure(s)\n", failures);
        return 1;
    }
    puts("test_strncat_bounded_overflow: ok");
    return 0;
}
