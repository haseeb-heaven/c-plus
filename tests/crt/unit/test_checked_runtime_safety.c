/*
 * test_checked_runtime_safety.c
 *
 * Runtime safety EVIDENCE for the checked _s implementations exported by
 * build/safe/libcplus_safecrt.a.  This test exercises actual runtime
 * constraint checks (null / zero length / bounds / overlap / termination /
 * allocation failure) and records observable results.
 *
 * Subjects (all symbols verified present in libcplus_safecrt.a via `nm -g`):
 *   - Literal safeclib "checked" entry points with explicit object-size
 *     bounds: cplus_sc__memcpy_s_chk, cplus_sc__strcpy_s_chk,
 *     cplus_sc__strcat_s_chk  (call the CHK_* / *_OVR_CLEAR paths directly).
 *   - The C+ checked adapters: cplus_memcpy_s / memmove_s / memset_s /
 *     memcmp_s / strcpy_s / strcat_s / strncpy_s / strncat_s.
 *   - The checked allocator: malloc_s / calloc_s / free_s / free_checked_s,
 *     plus cplus_safe_alloc_fail_after / fail_reset fault injection.
 *
 * HONEST SCOPE: these are RUNTIME constraint checks.  Passing this test does
 * NOT prove compile-time safety, pointer provenance, ownership, lifetime or
 * alias/freed-memory safety.  It only shows the named checks fired and that
 * the documented failure behavior (refusal + canary preservation) was
 * observed for the tested inputs.  See docs/libc/CHECKED_RUNTIME_SAFETY.md.
 *
 * Build / run (from the repository root):
 *   /usr/bin/clang -std=c99 -Wall -Wextra -pedantic -Istdlib/safe \
 *     tests/crt/unit/test_checked_runtime_safety.c \
 *     build/safe/libcplus_safecrt.a -lm -pthread -o /tmp/checked_runtime
 *   /tmp/checked_runtime
 *
 * Output contract: "checked_runtime: <checks> <failures>" on stdout; a
 * non-zero exit status iff failures > 0.  Individual failures go to stderr.
 */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "safe_alloc.h"
#include "safe_error.h"
#include "safe_log.h"
#include "safe_memory.h"
#include "safe_string.h"

/* Safeclib constraint codes (see stdlib/safe/safe_memory.h / safe_string.h).
 * These are POSIX-style positive codes, NOT platform errno values. */
#define SC_ESNULLP 400 /* null pointer            */
#define SC_ESZEROL 401 /* length is zero          */
#define SC_ESLEMAX 403 /* length exceeds max      */
#define SC_ESOVRLP 404 /* overlap                 */
#define SC_ESNOSPC 406 /* destination too small   */

#define BOS_UNKNOWN ((size_t)-1)

/* Exported by libcplus_safecrt.a (safeclib checked core + handler setters). */
typedef void (*sc_handler_t)(const char *, void *, int);
extern void cplus_sc_set_mem_constraint_handler_s(sc_handler_t h);
extern void cplus_sc_set_str_constraint_handler_s(sc_handler_t h);
extern int cplus_sc__memcpy_s_chk(void *dest, size_t dmax, const void *src,
                                  size_t smax, size_t destbos, size_t srcbos);
extern int cplus_sc__strcpy_s_chk(char *dest, size_t dmax, const char *src,
                                  size_t destbos);
extern int cplus_sc__strcat_s_chk(char *dest, size_t dmax, const char *src,
                                  size_t destbos);

static int g_checks;
static int g_failures;

#define CHECK(cond, name)                                                     \
    do {                                                                      \
        g_checks++;                                                           \
        if (!(cond)) {                                                        \
            g_failures++;                                                     \
            fprintf(stderr, "FAIL: %s\n", (name));                            \
        }                                                                     \
    } while (0)

#define CHECK_EQ(actual, expected, name)                                      \
    do {                                                                      \
        int a_ = (int)(actual);                                               \
        int e_ = (int)(expected);                                             \
        g_checks++;                                                           \
        if (a_ != e_) {                                                       \
            g_failures++;                                                     \
            fprintf(stderr, "FAIL: %s (got %d, expected %d)\n",               \
                    (name), a_, e_);                                          \
        }                                                                     \
    } while (0)

/* True iff every byte in [from,to) equals v.  Used for canary preservation. */
static int all_eq(const void *p, size_t from, size_t to, unsigned char v) {
    const unsigned char *b = (const unsigned char *)p;
    size_t i;
    for (i = from; i < to; ++i)
        if (b[i] != v) return 0;
    return 1;
}

static void quiet_handler(const char *msg, void *ptr, int err) {
    (void)msg; (void)ptr; (void)err;
}

/* ------------------------------------------------------------------------- */
/* A. Literal safeclib "checked" (_chk) entry points, explicit object sizes. */
/* ------------------------------------------------------------------------- */
static void test_chk_entries(void) {
    char d[16];
    char s[8];
    char u[8];

    memcpy(s, "abc", 4);

    /* null dest / null src / zero length */
    memset(d, 0xCC, sizeof d);
    CHECK_EQ(cplus_sc__memcpy_s_chk(NULL, 4, s, 4, 8, 8), SC_ESNULLP,
             "chk memcpy null dest refused (400)");
    CHECK_EQ(cplus_sc__memcpy_s_chk(d, 4, NULL, 4, sizeof d, 8), SC_ESNULLP,
             "chk memcpy null src refused (400)");
    CHECK_EQ(cplus_sc__memcpy_s_chk(d, 0, s, 4, sizeof d, 8), SC_ESZEROL,
             "chk memcpy zero dmax refused (401)");
    CHECK_EQ(cplus_sc__memcpy_s_chk(d, sizeof d, s, 0, sizeof d, 8), 0,
             "chk memcpy zero length is a defined no-op (0)");

    /* destination exactly fit / one-byte-too-small + canary */
    memset(d, 0xCC, sizeof d);
    CHECK_EQ(cplus_sc__memcpy_s_chk(d, 4, s, 4, 4, 8), 0,
             "chk memcpy exact fit accepted (0)");
    CHECK(memcmp(d, "abc", 4) == 0, "chk memcpy exact fit content correct");
    CHECK(all_eq(d, 4, sizeof d, 0xCC), "chk memcpy exact fit guards intact");

    memset(d, 0xCC, sizeof d);
    CHECK_EQ(cplus_sc__memcpy_s_chk(d, 3, s, 4, 3, 8), SC_ESNOSPC,
             "chk memcpy one-byte-too-small refused (406)");
    CHECK(all_eq(d, 3, sizeof d, 0xCC),
          "chk memcpy refusal preserves canary beyond dmax");

    /* SIZE_MAX n */
    CHECK_EQ(cplus_sc__memcpy_s_chk(d, sizeof d, s, SIZE_MAX, sizeof d, 8),
             SC_ESLEMAX, "chk memcpy SIZE_MAX length refused (403)");

    /* overlapping copy refused with canary intact */
    memset(d, 0xCC, sizeof d);
    memcpy(d, "abcdefgh", 8);
    CHECK_EQ(cplus_sc__memcpy_s_chk(d + 1, 8, d, 8, sizeof d - 1, 8),
             SC_ESOVRLP, "chk memcpy overlap refused (404)");
    CHECK(all_eq(d, 9, sizeof d, 0xCC),
          "chk memcpy overlap refusal preserves canary");

    /* strcpy: null / zero / exact fit / one-small / unterminated */
    CHECK_EQ(cplus_sc__strcpy_s_chk(NULL, 8, "abc", 8), SC_ESNULLP,
             "chk strcpy null dest refused (400)");
    CHECK_EQ(cplus_sc__strcpy_s_chk(d, 0, "abc", sizeof d), SC_ESZEROL,
             "chk strcpy zero dmax refused (401)");
    memset(d, 0xCC, sizeof d);
    CHECK_EQ(cplus_sc__strcpy_s_chk(d, 4, "abc", 4), 0,
             "chk strcpy exact fit accepted (0)");
    CHECK(strcmp(d, "abc") == 0, "chk strcpy exact fit content correct");
    CHECK(all_eq(d, 4, sizeof d, 0xCC), "chk strcpy exact fit guards intact");

    memset(d, 0xCC, sizeof d);
    CHECK_EQ(cplus_sc__strcpy_s_chk(d, 3, "abc", 3), SC_ESNOSPC,
             "chk strcpy one-byte-too-small refused (406)");
    CHECK(all_eq(d, 3, sizeof d, 0xCC),
          "chk strcpy refusal preserves canary beyond dmax");

    memset(u, 'Z', sizeof u); /* eight non-NUL bytes: unterminated within dmax */
    memset(d, 0xCC, sizeof d);
    CHECK(cplus_sc__strcpy_s_chk(d, sizeof u, u, sizeof u) != 0,
          "chk strcpy unterminated source refused");
    CHECK(all_eq(d, sizeof u, sizeof d, 0xCC),
          "chk strcpy unterminated refusal preserves canary");

    /* strcat: exact fit accepted; one-small refused */
    memset(d, 0xCC, sizeof d);
    d[0] = 'x'; d[1] = 'y'; d[2] = '\0';
    CHECK_EQ(cplus_sc__strcat_s_chk(d, 6, "abc", 6), 0,
             "chk strcat exact fit accepted (0)");
    CHECK(strcmp(d, "xyabc") == 0, "chk strcat exact fit content correct");

    memset(d, 0xCC, sizeof d);
    d[0] = 'x'; d[1] = 'y'; d[2] = '\0';
    CHECK_EQ(cplus_sc__strcat_s_chk(d, 5, "abc", 5), SC_ESNOSPC,
             "chk strcat one-byte-too-small refused (406)");
    CHECK(all_eq(d, 5, sizeof d, 0xCC),
          "chk strcat refusal preserves canary beyond dmax");
}

/* ------------------------------------------------------------------------- */
/* B. C+ checked adapters (safeclib-backed _s API).                          */
/* ------------------------------------------------------------------------- */
static void test_adapter_memory(void) {
    char d[16];
    const char src[] = "abc";
    int diff = 12345;

    /* null / zero / exact fit / one-small / SIZE_MAX */
    memset(d, 0xCC, sizeof d);
    CHECK_EQ(cplus_memcpy_s(NULL, sizeof d, src, 4), SC_ESNULLP,
             "adapter memcpy null dest refused (400)");
    CHECK_EQ(cplus_memcpy_s(d, sizeof d, NULL, 4), SC_ESNULLP,
             "adapter memcpy null src refused (400)");
    CHECK_EQ(cplus_memcpy_s(d, 0, src, 4), SC_ESZEROL,
             "adapter memcpy zero dmax refused (401)");
    CHECK_EQ(cplus_memcpy_s(d, sizeof d, src, 0), 0,
             "adapter memcpy zero length defined no-op (0)");

    /* A rejected call above defensively cleared d; restore the canary. */
    memset(d, 0xCC, sizeof d);
    CHECK_EQ(cplus_memcpy_s(d, 4, src, 4), 0,
             "adapter memcpy exact fit accepted (0)");
    CHECK(memcmp(d, "abc", 4) == 0, "adapter memcpy exact fit content correct");
    CHECK(all_eq(d, 4, sizeof d, 0xCC), "adapter memcpy exact fit guards intact");

    memset(d, 0xCC, sizeof d);
    CHECK_EQ(cplus_memcpy_s(d, 3, src, 4), SC_ESNOSPC,
             "adapter memcpy one-byte-too-small refused (406)");
    CHECK(all_eq(d, 3, sizeof d, 0xCC),
          "adapter memcpy refusal preserves canary beyond dmax");

    CHECK_EQ(cplus_memcpy_s(d, sizeof d, src, SIZE_MAX), SC_ESLEMAX,
             "adapter memcpy SIZE_MAX length refused (403)");
    CHECK_EQ(cplus_memcpy_s(d, SIZE_MAX, src, 4), SC_ESLEMAX,
             "adapter memcpy SIZE_MAX dmax refused (403)");

    /* overlap: memcpy refuses, memmove succeeds, canaries survive */
    memset(d, 0xCC, sizeof d);
    memcpy(d, "abcdefgh", 8);
    CHECK_EQ(cplus_memcpy_s(d + 1, 8, d, 8), SC_ESOVRLP,
             "adapter memcpy overlap refused (404)");
    CHECK(all_eq(d, 9, sizeof d, 0xCC),
          "adapter memcpy overlap refusal preserves canary");

    memset(d, 0xCC, sizeof d);
    memcpy(d, "abcdefgh", 8);
    CHECK_EQ(cplus_memmove_s(d + 1, 15, d, 8), 0,
             "adapter memmove overlap handled (0)");
    CHECK(memcmp(d, "aabcdefgh", 9) == 0,
          "adapter memmove overlap content correct");
    CHECK(all_eq(d, 9, sizeof d, 0xCC),
          "adapter memmove writes only within dmax");

    /* memset_s runtime checks.  NOTE safeclib checks n > dmax before dmax==0,
     * so (dmax=0, n=1) is ESNOSPC (406), while (dmax=0, n=0) is a defined
     * no-op (0).  This differs from the generic "zero length -> 401" table. */
    CHECK_EQ(cplus_memset_s(NULL, 16, 0, 1), SC_ESNULLP,
             "adapter memset null dest refused (400)");
    CHECK_EQ(cplus_memset_s(d, 0, 0, 1), SC_ESNOSPC,
             "adapter memset n>dmax with zero dmax refused (406)");
    CHECK_EQ(cplus_memset_s(d, 0, 0, 0), 0,
             "adapter memset zero dmax + zero n is a no-op (0)");
    CHECK_EQ(cplus_memset_s(d, sizeof d, 0, SIZE_MAX), SC_ESLEMAX,
             "adapter memset SIZE_MAX n refused (403)");
    memset(d, 0xCC, sizeof d);
    CHECK_EQ(cplus_memset_s(d, 4, 0x41, 4), 0,
             "adapter memset exact fit accepted (0)");
    CHECK(all_eq(d, 0, 4, 0x41) && all_eq(d, 4, sizeof d, 0xCC),
          "adapter memset bounded write + guards intact");

    /* memcmp_s reports ordering via diff; null diff is refused */
    CHECK_EQ(cplus_memcmp_s("abc", 4, "abc", 4, &diff), 0,
             "adapter memcmp equal accepted (0)");
    CHECK(diff == 0, "adapter memcmp diff reports equality");
    CHECK_EQ(cplus_memcmp_s("abc", 4, "abd", 4, &diff), 0,
             "adapter memcmp unequal is not a constraint violation (0)");
    CHECK(diff < 0, "adapter memcmp diff reports ordering");
    CHECK_EQ(cplus_memcmp_s("abc", 4, "abc", 4, NULL), SC_ESNULLP,
             "adapter memcmp null diff refused (400)");
}

static void test_adapter_strings(void) {
    char d[16];
    char u[8];

    /* strcpy_s */
    CHECK_EQ(cplus_strcpy_s(NULL, sizeof d, "abc"), SC_ESNULLP,
             "adapter strcpy null dest refused (400)");
    CHECK_EQ(cplus_strcpy_s(d, sizeof d, NULL), SC_ESNULLP,
             "adapter strcpy null src refused (400)");
    CHECK_EQ(cplus_strcpy_s(d, 0, "abc"), SC_ESZEROL,
             "adapter strcpy zero dmax refused (401)");

    memset(d, 0xCC, sizeof d);
    CHECK_EQ(cplus_strcpy_s(d, 4, "abc"), 0,
             "adapter strcpy exact fit accepted (0)");
    CHECK(strcmp(d, "abc") == 0 && all_eq(d, 4, sizeof d, 0xCC),
          "adapter strcpy exact fit content + guards intact");

    memset(d, 0xCC, sizeof d);
    CHECK_EQ(cplus_strcpy_s(d, 3, "abc"), SC_ESNOSPC,
             "adapter strcpy one-byte-too-small refused (406)");
    CHECK(all_eq(d, 3, sizeof d, 0xCC),
          "adapter strcpy refusal preserves canary");

    memset(u, 'Z', sizeof u); /* no NUL within the 8-byte source object */
    memset(d, 0xCC, sizeof d);
    CHECK(cplus_strcpy_s(d, sizeof u, u) != 0,
          "adapter strcpy unterminated source refused");
    CHECK(all_eq(d, sizeof u, sizeof d, 0xCC),
          "adapter strcpy unterminated refusal preserves canary");

    /* strcat_s */
    CHECK_EQ(cplus_strcat_s(NULL, sizeof d, "abc"), SC_ESNULLP,
             "adapter strcat null dest refused (400)");
    CHECK_EQ(cplus_strcat_s(d, sizeof d, NULL), SC_ESNULLP,
             "adapter strcat null src refused (400)");
    CHECK_EQ(cplus_strcat_s(d, 0, "abc"), SC_ESZEROL,
             "adapter strcat zero dmax refused (401)");

    memset(d, 0xCC, sizeof d); d[0] = 'x'; d[1] = 'y'; d[2] = '\0';
    CHECK_EQ(cplus_strcat_s(d, 6, "abc"), 0,
             "adapter strcat exact fit accepted (0)");
    CHECK(strcmp(d, "xyabc") == 0 && all_eq(d, 6, sizeof d, 0xCC),
          "adapter strcat exact fit content + guards intact");

    memset(d, 0xCC, sizeof d); d[0] = 'x'; d[1] = 'y'; d[2] = '\0';
    CHECK_EQ(cplus_strcat_s(d, 5, "abc"), SC_ESNOSPC,
             "adapter strcat one-byte-too-small refused (406)");
    CHECK(all_eq(d, 5, sizeof d, 0xCC),
          "adapter strcat refusal preserves canary");

    memset(d, 0xCC, sizeof d); d[0] = 'x'; d[1] = 'y'; d[2] = '\0';
    CHECK(cplus_strcat_s(d, sizeof u, u) != 0,
          "adapter strcat unterminated source refused");
    CHECK(all_eq(d, sizeof u, sizeof d, 0xCC),
          "adapter strcat unterminated refusal preserves canary");

    /* bounded truncation: strncpy_s / strncat_s copy at most slen bytes and
     * leave the result NUL-terminated (no unbounded source scan). */
    memset(d, 0xCC, sizeof d);
    CHECK_EQ(cplus_strncpy_s(d, sizeof d, "abcdef", 2), 0,
             "adapter strncpy bounded truncation accepted (0)");
    CHECK(strcmp(d, "ab") == 0, "adapter strncpy truncation result correct");
    CHECK(all_eq(d, 3, sizeof d, 0xCC),
          "adapter strncpy truncation leaves guards intact");

    memset(d, 0xCC, sizeof d);
    CHECK_EQ(cplus_strncpy_s(d, sizeof d, "abcdef", 0), 0,
             "adapter strncpy zero slen accepted (0)");
    CHECK(d[0] == '\0', "adapter strncpy zero slen yields empty string");

    CHECK_EQ(cplus_strncpy_s(d, sizeof d, "abcdef", SIZE_MAX), SC_ESLEMAX,
             "adapter strncpy SIZE_MAX slen refused (403)");

    memset(d, 0xCC, sizeof d); d[0] = 'x'; d[1] = 'y'; d[2] = '\0';
    CHECK_EQ(cplus_strncat_s(d, sizeof d, "abcdef", 2), 0,
             "adapter strncat bounded truncation accepted (0)");
    CHECK(strcmp(d, "xyab") == 0, "adapter strncat truncation result correct");
    CHECK(all_eq(d, 5, sizeof d, 0xCC),
          "adapter strncat truncation leaves guards intact");
}

/* ------------------------------------------------------------------------- */
/* C. Checked allocator: overflow, fault injection, double/invalid free.     */
/* ------------------------------------------------------------------------- */
static void test_allocator(void) {
    void *p;
    void *a;
    void *b;
    void *c;
    int stack_obj = 0;

    /* allocation overflow -> NULL + errno set */
    errno = 0;
    p = malloc_s(SIZE_MAX);
    CHECK(p == NULL, "malloc_s SIZE_MAX overflow returns NULL");
    CHECK(errno != 0, "malloc_s SIZE_MAX overflow sets errno");

    errno = 0;
    p = calloc_s(SIZE_MAX, 2);
    CHECK(p == NULL, "calloc_s count*size overflow returns NULL");
    CHECK(errno != 0, "calloc_s overflow sets errno");

    /* zero-sized allocation is defined and zero-filled (1 tracked byte) */
    p = calloc_s(2, 0);
    CHECK(p != NULL, "calloc_s zero size yields defined block");
    if (p != NULL) {
        CHECK(cplus_safe_ptr_size(p) >= 1,
              "calloc_s zero size block has tracked storage");
        CHECK(*(unsigned char *)p == 0,
              "calloc_s zero size block is zeroed");
        free_s(p);
    }

    /* allocation-failure injection */
    a = malloc_s(8);
    CHECK(a != NULL, "pre-injection allocation succeeds");
    cplus_safe_alloc_fail_after(1);
    b = malloc_s(8);
    CHECK(b != NULL, "fail_after(1): first allocation succeeds");
    errno = 0;
    c = malloc_s(8);
    CHECK(c == NULL, "fail_after(1): next allocation refused");
    CHECK(errno == ENOMEM, "fail_after(1): refusal sets ENOMEM");
    cplus_safe_alloc_fail_reset();
    c = malloc_s(8);
    CHECK(c != NULL, "fail_reset restores allocation");
    free_s(a);
    if (b != NULL) free_s(b);
    if (c != NULL) free_s(c);

    /* double free and invalid free are refused, not acted upon */
    p = malloc_s(16);
    CHECK(p != NULL, "allocation for free checks succeeds");
    if (p != NULL) {
        CHECK_EQ(free_checked_s(p), 0, "free_checked_s live block accepted (0)");
        CHECK_EQ(free_checked_s(p), EINVAL,
                 "free_checked_s double free refused (EINVAL)");
        CHECK_EQ(free_checked_s(&stack_obj), EINVAL,
                 "free_checked_s stack pointer refused (EINVAL)");
    }
    CHECK_EQ(free_checked_s(NULL), 0,
             "free_checked_s NULL is a defined no-op (0)");
}

int main(void) {
    /* IGNORE mode reports violations via return codes; install quiet handlers
     * afterwards (mode setup resets handlers to the verbose ignore handler). */
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_sc_set_mem_constraint_handler_s(quiet_handler);
    cplus_sc_set_str_constraint_handler_s(quiet_handler);

    test_chk_entries();
    test_adapter_memory();
    test_adapter_strings();
    test_allocator();

    printf("checked_runtime: %d %d\n", g_checks, g_failures);
    return g_failures != 0;
}
