/* Behavioural tests for the additive hardening wrappers in
 * libc/src/stdlib/alloc_hardened.c. Run from the repository root:
 *
 *   WORK=$(mktemp -d)
 *   /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -Ilibc/install-include \
 *     libc/src/stdlib/alloc_hardened.c tests/libc/test_alloc_hardened.c \
 *     build/libc/libcplus_hardened.a -lm -pthread -o "$WORK/test_alloc_hardened"
 *   "$WORK/test_alloc_hardened"
 *
 * Prints "alloc_hardened: <checks> <failures>" and exits non-zero on failure.
 */
#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cplus_alloc_hardened.h"
#include "cplus_libc.h"

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, name)                                                      \
    do {                                                                       \
        ++g_checks;                                                            \
        if (!(cond)) {                                                         \
            ++g_failures;                                                      \
            fprintf(stderr, "alloc_hardened: FAIL %s\n", (name));              \
        }                                                                      \
    } while (0)

int main(void) {
    void *p = NULL;
    void *before_addr;
    void *alias;
    void *fresh = NULL;
    unsigned char *buf;
    cplus_alloc_meta meta_before;
    cplus_alloc_meta meta_after;
    int rc;
    int stack_obj = 7;
    void *bad = &stack_obj;

    cplus_alloc_fail_reset();

    /* 1. Normal path: fresh allocation, grow, content preservation. */
    rc = cplus_alloc_atomic_realloc(&p, 64);
    CHECK(rc == 0 && p != NULL, "atomic_realloc fresh returns 0");
    CHECK(p != NULL && cplus_alloc_size(p) == 64, "fresh tracked size 64");
    if (p == NULL) {
        printf("alloc_hardened: %d %d\n", g_checks, g_failures);
        return 1;
    }
    buf = (unsigned char *)p;
    memset(buf, 0x5Au, 64);
    CHECK(buf[0] == 0x5Au && buf[63] == 0x5Au, "canary initial");

    rc = cplus_alloc_atomic_realloc(&p, 128);
    CHECK(rc == 0 && p != NULL, "atomic_realloc grow returns 0");
    CHECK(p != NULL && cplus_alloc_size(p) == 128, "grown tracked size 128");
    if (p == NULL) {
        printf("alloc_hardened: %d %d\n", g_checks, g_failures);
        return 1;
    }
    buf = (unsigned char *)p;
    CHECK(buf[0] == 0x5Au && buf[63] == 0x5Au, "canary intact after grow");

    /* 2. Injected failure atomicity: original pointer, size, generation and
     * bytes must all survive a failed realloc. */
    CHECK(cplus_alloc_get_meta(p, &meta_before) == 0, "meta before injection");
    before_addr = p;
    buf = (unsigned char *)p;
    memset(buf, 0xA5u, 128);
    CHECK(buf[0] == 0xA5u && buf[127] == 0xA5u, "canary before injection");

    cplus_alloc_fail_after(0);
    errno = 0;
    rc = cplus_alloc_atomic_realloc(&p, 512);
    CHECK(rc == ENOMEM && errno == ENOMEM, "injected failure returns ENOMEM");
    CHECK(p == before_addr, "pointer unchanged on failure");
    cplus_alloc_fail_reset();

    CHECK(cplus_alloc_get_meta(p, &meta_after) == 0, "still live after failure");
    CHECK(meta_after.is_live == 1, "live flag unchanged after failure");
    CHECK(meta_after.byte_size == meta_before.byte_size, "size unchanged after failure");
    CHECK(meta_after.generation == meta_before.generation, "generation unchanged after failure");
    buf = (unsigned char *)before_addr;
    CHECK(buf[0] == 0xA5u && buf[127] == 0xA5u, "canary intact after injected failure");

    /* Invalid/untracked pointer is refused without modifying the slot. */
    errno = 0;
    rc = cplus_alloc_atomic_realloc(&bad, 16);
    CHECK(rc == EINVAL && errno == EINVAL, "untracked pointer refused");
    CHECK(bad == &stack_obj, "untracked pointer slot unchanged");

    /* 3. Checked free + double-free refusal via metadata. */
    alias = p;
    rc = cplus_alloc_checked_free(&p);
    CHECK(rc == 0 && p == NULL, "checked_free succeeds and NULLs pointer");
    CHECK(cplus_alloc_is_live(alias) == 0, "alias is no longer live");
    errno = 0;
    rc = cplus_alloc_checked_free(&alias);
    CHECK(rc == EINVAL && errno == EINVAL, "double free refused");
    CHECK(alias != NULL, "refused double free keeps caller pointer");

    /* 4. NULL pointer handling. */
    errno = 0;
    CHECK(cplus_alloc_checked_free(NULL) == EINVAL && errno == EINVAL,
          "checked_free(NULL slot) is EINVAL");
    fresh = NULL;
    errno = 0;
    CHECK(cplus_alloc_checked_free(&fresh) == EINVAL && errno == EINVAL,
          "checked_free(NULL value) is EINVAL");
    errno = 0;
    CHECK(cplus_alloc_atomic_realloc(NULL, 16) == EINVAL && errno == EINVAL,
          "atomic_realloc(NULL slot) is EINVAL");

    /* A NULL value is a legal fresh-allocation request for realloc semantics. */
    rc = cplus_alloc_atomic_realloc(&fresh, 16);
    CHECK(rc == 0 && fresh != NULL, "atomic_realloc from NULL allocates");
    CHECK(cplus_alloc_checked_free(&fresh) == 0 && fresh == NULL,
          "fresh block freed and NULLed");

    /* 5. State dump to a tmpfile: bounded, content-bearing, path-free. */
    {
        FILE *f = tmpfile();
        char line[256];
        int nlines = 0;
        int saw_live = 0;
        int saw_ok = 0;
        int saw_fail = 0;
        int saw_refused = 0;
        int has_path = 0;

        CHECK(f != NULL, "tmpfile for state dump");
        if (f != NULL) {
            cplus_alloc_state_dump(f);
            CHECK(fflush(f) == 0, "state dump flushed");
            rewind(f);
            while (fgets(line, (int)sizeof(line), f) != NULL) {
                ++nlines;
                if (strstr(line, "live_count=") != NULL) saw_live = 1;
                if (strstr(line, "atomic_realloc_ok=") != NULL) saw_ok = 1;
                if (strstr(line, "atomic_realloc_fail=") != NULL) saw_fail = 1;
                if (strstr(line, "checked_free_refused=") != NULL) saw_refused = 1;
                if (strchr(line, '/') != NULL) has_path = 1;
            }
            CHECK(nlines >= 8 && nlines <= 24, "state dump is bounded");
            CHECK(saw_live && saw_ok && saw_fail && saw_refused,
                  "state dump has expected counters");
            CHECK(!has_path, "state dump contains no paths");
            fclose(f);
        }
    }
    cplus_alloc_state_dump(NULL);
    CHECK(1, "NULL stream tolerated");

    cplus_alloc_fail_reset();
    printf("alloc_hardened: %d %d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
