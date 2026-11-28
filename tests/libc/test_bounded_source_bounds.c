/* Regression tests for issue #89: source-capacity enforcement in the plain
 * libc bounded layer, bounded strncat scanning, and the atomic unknown-mode
 * flag.  Every refusal must leave the destination byte-for-byte intact. */
#define _DEFAULT_SOURCE 1
#define _DARWIN_C_SOURCE 1
#include "cplus_libc.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_ANON /* Linux spells the same flag MAP_ANONYMOUS */
#define MAP_ANON MAP_ANONYMOUS
#endif

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

/* 1. cplus_memcpy_bounded: tracked destination has room, tracked source does
 * not. The old code validated only the destination and over-read 32 bytes
 * from a 4-byte heap block. */
static void memcpy_source_overread(void) {
    unsigned char *dst = (unsigned char *)cplus_malloc(64);
    unsigned char *src = (unsigned char *)cplus_malloc(4);
    CHECK(dst != NULL && src != NULL);
    if (!dst || !src) return;
    memset(src, 0x11, 4);
    memset(dst, 0x5A, 64);
    errno = 0;
    CHECK(cplus_memcpy_bounded(dst, src, 32, (size_t)-1) == NULL);
    CHECK(errno == ERANGE);
    CHECK(all_bytes(dst, 64, 0x5A)); /* dest intact on refusal */
    cplus_free(src);
    cplus_free(dst);
}

/* 2. Same defect in cplus_memmove_bounded. */
static void memmove_source_overread(void) {
    unsigned char *dst = (unsigned char *)cplus_malloc(64);
    unsigned char *src = (unsigned char *)cplus_malloc(4);
    CHECK(dst != NULL && src != NULL);
    if (!dst || !src) return;
    memset(src, 0x22, 4);
    memset(dst, 0x6B, 64);
    errno = 0;
    CHECK(cplus_memmove_bounded(dst, src, 32, (size_t)-1) == NULL);
    CHECK(errno == ERANGE);
    CHECK(all_bytes(dst, 64, 0x6B));
    cplus_free(src);
    cplus_free(dst);
}

/* 3. Interior source pointers resolve remaining capacity, not base size. */
static void interior_source_bounds(void) {
    unsigned char *src = (unsigned char *)cplus_malloc(16);
    unsigned char *dst = (unsigned char *)cplus_malloc(64);
    unsigned char *interior;
    CHECK(src != NULL && dst != NULL);
    if (!src || !dst) return;
    interior = src + 12; /* exactly 4 readable bytes remain */
    memset(interior, 0x33, 4);
    memset(dst, 0x7C, 64);
    errno = 0;
    CHECK(cplus_memcpy_bounded(dst, interior, 8, (size_t)-1) == NULL);
    CHECK(errno == ERANGE);
    CHECK(all_bytes(dst, 64, 0x7C));

    /* Exact fit is still accepted and copies the real source bytes. */
    errno = 0;
    CHECK(cplus_memcpy_bounded(dst, interior, 4, (size_t)-1) == dst);
    CHECK(errno == 0);
    CHECK(all_bytes(dst, 4, 0x33));
    CHECK(all_bytes(dst + 4, 60, 0x7C));
    cplus_free(dst);
    cplus_free(src);
}

/* 4. Unknown source capacity keeps raw-C behaviour: a literal source is not
 * a refusal, even in strict mode (the public API has no source-capacity arg). */
static void unknown_source_preserved(void) {
    unsigned char *dst = (unsigned char *)cplus_malloc(64);
    CHECK(dst != NULL);
    if (!dst) return;
    memset(dst, 0, 64);
    cplus_libc_set_unknown_capacity_mode(1);
    errno = 0;
    CHECK(cplus_memcpy_bounded(dst, "ABC", 3, cplus_alloc_remaining_capacity(dst)) == dst);
    CHECK(errno == 0);
    CHECK(memcmp(dst, "ABC", 3) == 0);
    cplus_libc_set_unknown_capacity_mode(0);
    cplus_free(dst);
}

/* 5. cplus_strncat_bounded must scan at most n source bytes. The source is
 * placed at the very end of a readable page followed by PROT_NONE: the old
 * cplus_strlen(s) call faults here, while a bounded scan never touches the
 * guard page. n == 0 and a non-NUL-terminated n-byte source are both covered. */
static void strncat_bounded_scan(void) {
    long ps = sysconf(_SC_PAGESIZE);
    char *base;
    char *src;
    char *dst;
    char *r;
    CHECK(ps > 0);
    if (ps <= 0) return;
    base = (char *)mmap(NULL, (size_t)ps * 2u, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON, -1, 0);
    CHECK(base != MAP_FAILED);
    if (base == MAP_FAILED) return;
    CHECK(mprotect(base + ps, (size_t)ps, PROT_NONE) == 0);
    src = base + ps - 2; /* two readable bytes, then the guard page */
    src[0] = 'A';
    src[1] = 'B';

    dst = (char *)cplus_malloc(16);
    CHECK(dst != NULL);
    if (dst == NULL) {
        (void)munmap(base, (size_t)ps * 2u);
        return;
    }
    dst[0] = 'Z';
    dst[1] = '\0';
    errno = 0;
    r = cplus_strncat_bounded(dst, src, 2, (size_t)-1);
    CHECK(r == dst && errno == 0 && strcmp(dst, "ZAB") == 0);

    dst[0] = 'Q';
    dst[1] = '\0';
    errno = 0;
    r = cplus_strncat_bounded(dst, src, 0, (size_t)-1);
    CHECK(r == dst && errno == 0 && strcmp(dst, "Q") == 0);

    /* A huge n is harmless when the source NUL is within n: the scan stops
     * at the terminator and the addition overflow guard keeps `need` sane. */
    errno = 0;
    r = cplus_strncat_bounded(dst, "xy", SIZE_MAX, (size_t)-1);
    CHECK(r == dst && errno == 0 && strcmp(dst, "Qxy") == 0);

    cplus_free(dst);
    (void)munmap(base, (size_t)ps * 2u);
}

/* 6. C1: a tracked source whose capacity exactly equals the destination room
 * ends at the source allocation edge. bounded_strlen() consumes all `room`
 * bytes with no NUL, so the old one-byte lookahead read s[room] past the
 * block. Refuse without that read; exact-fit untracked sources and tracked
 * sources with an in-capacity NUL must still append. */
static void strcat_bounded_source_edge(void) {
    enum { DCAP = 16, DLEN = 2 };
    const size_t room = DCAP - DLEN - 1u; /* 13 */
    char *dst = (char *)cplus_malloc(DCAP);
    char *src;
    char *r;
    size_t i;
    CHECK(dst != NULL);
    if (!dst) return;

    /* Tracked source, capacity == room, no NUL anywhere in the block. */
    src = (char *)cplus_malloc(room);
    CHECK(src != NULL);
    if (!src) { cplus_free(dst); return; }
    for (i = 0; i < room; ++i) src[i] = 'x';

    dst[0] = 'a'; dst[1] = 'b'; dst[2] = '\0';
    errno = 0;
    r = cplus_strcat_bounded(dst, src, (size_t)-1);
    CHECK(r == NULL && errno == ERANGE);
    CHECK(strcmp(dst, "ab") == 0); /* destination intact on refusal */
    cplus_free(src);

    /* Control: untracked source with its NUL exactly at the room boundary. */
    dst[0] = 'a'; dst[1] = 'b'; dst[2] = '\0';
    errno = 0;
    r = cplus_strcat_bounded(dst, "abcdefghijklm", (size_t)-1);
    CHECK(r == dst && errno == 0);
    CHECK(strcmp(dst, "ababcdefghijklm") == 0);

    /* Control: tracked source, capacity == room, NUL inside the block. */
    src = (char *)cplus_malloc(room);
    CHECK(src != NULL);
    if (src != NULL) {
        for (i = 0; i < room - 1u; ++i) src[i] = 'y';
        src[room - 1u] = '\0';
        dst[0] = 'a'; dst[1] = 'b'; dst[2] = '\0';
        errno = 0;
        r = cplus_strcat_bounded(dst, src, (size_t)-1);
        CHECK(r == dst && errno == 0);
        CHECK(strlen(dst) == DLEN + room - 1u);
        CHECK(memcmp(dst, "ab", DLEN) == 0 && dst[DLEN] == 'y');
        cplus_free(src);
    }
    cplus_free(dst);
}

/* 7. Concurrent mode toggling must be race-free without touching the public
 * API. All capacities here are known, so results stay deterministic. */
static void *toggle_mode(void *arg) {
    int i;
    (void)arg;
    for (i = 0; i < 2000; ++i)
        cplus_libc_set_unknown_capacity_mode(i & 1);
    return NULL;
}

static void atomic_mode_toggle(void) {
    pthread_t th;
    unsigned char *dst = (unsigned char *)cplus_malloc(32);
    unsigned char *src = (unsigned char *)cplus_malloc(8);
    int i;
    CHECK(dst != NULL && src != NULL);
    if (!dst || !src) return;
    memset(src, 0x44, 8);
    CHECK(pthread_create(&th, NULL, toggle_mode, NULL) == 0);
    for (i = 0; i < 2000; ++i) {
        errno = 0;
        CHECK(cplus_memcpy_bounded(dst, src, 8,
                                   cplus_alloc_remaining_capacity(dst)) == dst);
        CHECK(errno == 0 && all_bytes(dst, 8, 0x44));
    }
    (void)pthread_join(th, NULL);
    cplus_libc_set_unknown_capacity_mode(0);
    cplus_free(src);
    cplus_free(dst);
}

int main(void) {
    memcpy_source_overread();
    memmove_source_overread();
    interior_source_bounds();
    unknown_source_preserved();
    strncat_bounded_scan();
    strcat_bounded_source_edge();
    atomic_mode_toggle();
    if (failures != 0) {
        printf("test_bounded_source_bounds: %d failure(s)\n", failures);
        return 1;
    }
    puts("test_bounded_source_bounds: ok");
    return 0;
}
