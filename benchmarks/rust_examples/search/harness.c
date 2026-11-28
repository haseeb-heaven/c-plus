/* C99 harness for the checked search APIs, paired with search.rs.
 *
 * Build (see compare.sh / NOTES.md for the exact command):
 *   clang -std=c99 -Ilibc/install-include -Ithird_party/safeclib/include \
 *     harness.c build/libc/libcplus_hardened.a build/safe/libcplus_safecrt.a \
 *     -o out/harness_c
 *
 * Every linkable callee below was verified present with nm before use:
 *   cplus_sc_upstream_bsearch_s   (libcplus_hardened.a)
 *   cplus_sc_upstream_memrchr_s   (libcplus_hardened.a)
 *   cplus_v_memchr_s              (libcplus_safecrt.a)
 *   cplus_v__strchr_s_chk         (libcplus_safecrt.a)
 *   cplus_v__strrchr_s_chk        (libcplus_safecrt.a)
 *   cplus_v__strstr_s_chk         (libcplus_safecrt.a)
 * qsort_s has NO linkable symbol in any archive (only the macro in
 * third_party/safeclib/include/safe_lib.h), so it is never called here;
 * compare.sh reports it as BLOCKED with nm evidence.
 *
 * Every stdout line has the exact form:  <api> <case> <token>
 * Tokens are outcome observations only. They are NOT a claim of semantic
 * equivalence with the safe-Rust examples in search.rs; see NOTES.md.
 */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cplus_safecrt_upstream.h"
#include "cplus_vendor_mem.h"
#include "cplus_vendor_strchr.h"
#include "cplus_vendor_strstr.h"

/* mem/bsearch upstream ceiling (16384) and vendored str ceiling (4096). */
#define LIM_MEM ((size_t)CPLUS_SC_UPSTREAM_RSIZE_MAX_MEM)
#define LIM_STR ((size_t)CPLUS_VENDOR_STRCHR_RSIZE_MAX_STR)
#define BOS_UNKNOWN_S ((size_t)-1)

#define CTX_MAGIC ((unsigned)0x5A5A5A5A)
#define CANARY_I ((int)0x5A5AA5A5)
#define CANARY_C ('~') /* printable ASCII so the str buffers stay UTF-8 safe */

typedef struct {
    unsigned magic;
} ctx_t;

/* The vendored checked str functions route constraint reports through
 * safeclib's global str handler, whose default prints to stdout. Install a
 * no-op handler so stdout holds ONLY deterministic <api> <case> <token> lines.
 * errno_t == int in the pinned safeclib headers. */
typedef void (*cplus_handler_t)(const char *msg, void *ptr, int err);
extern cplus_handler_t
cplus_sc_set_mem_constraint_handler_s(cplus_handler_t handler);
extern cplus_handler_t
cplus_sc_set_str_constraint_handler_s(cplus_handler_t handler);

static void noop_handler(const char *msg, void *ptr, int err)
{
    (void)msg;
    (void)ptr;
    (void)err;
}

static void emit(const char *api, const char *case_name, const char *token)
{
    printf("%s %s %s\n", api, case_name, token);
}

/* ================================================================== */
/* bsearch_s -> cplus_sc_upstream_bsearch_s                           */
/* compar(key, element, context): C11 K.3.6.3.1 shape                 */
/* ================================================================== */
static int cmp_plain(const void *k, const void *e, void *ctx)
{
    int a, b;
    (void)ctx;
    a = *(const int *)k;
    b = *(const int *)e;
    return (a > b) - (a < b);
}

static int cmp_ctx(const void *k, const void *e, void *ctx)
{
    const ctx_t *c = (const ctx_t *)ctx;
    int a, b;
    /* A comparator that did not receive its context refuses to match. */
    if (c == NULL || c->magic != CTX_MAGIC)
        return 1;
    a = *(const int *)k;
    b = *(const int *)e;
    return (a > b) - (a < b);
}

static volatile int compar_calls;

static int cmp_counting(const void *k, const void *e, void *ctx)
{
    compar_calls++;
    return cmp_plain(k, e, ctx);
}

static void run_bsearch(void)
{
    int a[5] = {1, 3, 5, 7, 9};
    ctx_t good = {CTX_MAGIC};
    void *r;
    int key;

    key = 7;
    errno = 999;
    r = cplus_sc_upstream_bsearch_s(&key, a, 5, sizeof(a[0]), cmp_plain, NULL);
    emit("bsearch_s", "found",
         (r == &a[3] && errno == 0) ? "FOUND" : "BAD_FOUND");

    key = 6;
    errno = 999;
    r = cplus_sc_upstream_bsearch_s(&key, a, 5, sizeof(a[0]), cmp_plain, NULL);
    emit("bsearch_s", "not-found",
         (r == NULL && errno == 0) ? "NOTFOUND" : "BAD_NOTFOUND");

    key = 7;
    errno = 999;
    r = cplus_sc_upstream_bsearch_s(NULL, a, 5, sizeof(a[0]), cmp_plain, NULL);
    emit("bsearch_s", "null-key",
         (r == NULL && errno == CPLUS_SC_UPSTREAM_ESNULLP) ? "REFUSED"
                                                          : "BAD_REFUSAL");
    errno = 999;
    r = cplus_sc_upstream_bsearch_s(&key, NULL, 5, sizeof(a[0]), cmp_plain,
                                    NULL);
    emit("bsearch_s", "null-base",
         (r == NULL && errno == CPLUS_SC_UPSTREAM_ESNULLP) ? "REFUSED"
                                                          : "BAD_REFUSAL");
    errno = 999;
    r = cplus_sc_upstream_bsearch_s(&key, a, 5, sizeof(a[0]), NULL, NULL);
    emit("bsearch_s", "null-compar",
         (r == NULL && errno == CPLUS_SC_UPSTREAM_ESNULLP) ? "REFUSED"
                                                          : "BAD_REFUSAL");

    /* nmemb == 0 has no constraint: clean empty search, errno == 0. */
    errno = 999;
    compar_calls = 0;
    r = cplus_sc_upstream_bsearch_s(&key, a, 0, sizeof(a[0]), cmp_counting,
                                    NULL);
    emit("bsearch_s", "zero-nmemb",
         (r == NULL && errno == 0 && compar_calls == 0) ? "EMPTY"
                                                        : "BAD_EMPTY");

    /* One-over RSIZE_MAX_MEM: refused before any comparator call. */
    errno = 999;
    compar_calls = 0;
    r = cplus_sc_upstream_bsearch_s(&key, a, LIM_MEM + 1, sizeof(a[0]),
                                    cmp_counting, NULL);
    emit("bsearch_s", "over-nmemb",
         (r == NULL && errno == CPLUS_SC_UPSTREAM_ESLEMAX && compar_calls == 0)
             ? "REFUSED"
             : "BAD_REFUSAL");
    errno = 999;
    compar_calls = 0;
    r = cplus_sc_upstream_bsearch_s(&key, a, 5, LIM_MEM + 1, cmp_counting,
                                    NULL);
    emit("bsearch_s", "over-size",
         (r == NULL && errno == CPLUS_SC_UPSTREAM_ESLEMAX && compar_calls == 0)
             ? "REFUSED"
             : "BAD_REFUSAL");

    key = 7;
    errno = 999;
    r = cplus_sc_upstream_bsearch_s(&key, a, 5, sizeof(a[0]), cmp_ctx, &good);
    emit("bsearch_s", "context-ok",
         (r == &a[3] && errno == 0) ? "CTX_OK" : "BAD_CTX");
    errno = 999;
    r = cplus_sc_upstream_bsearch_s(&key, a, 5, sizeof(a[0]), cmp_ctx, NULL);
    emit("bsearch_s", "context-lost",
         (r == NULL && errno == 0) ? "CTX_LOST" : "BAD_CTX");

    {
        int b[7];
        int i;
        b[0] = CANARY_I;
        b[6] = CANARY_I;
        for (i = 0; i < 5; ++i)
            b[i + 1] = a[i];
        key = 5;
        errno = 999;
        r = cplus_sc_upstream_bsearch_s(&key, b + 1, 5, sizeof(b[0]),
                                        cmp_plain, NULL);
        emit("bsearch_s", "canary",
             (b[0] == CANARY_I && b[6] == CANARY_I && r == &b[3]) ? "INTACT"
                                                                 : "BAD_CANARY");
    }
}

/* ================================================================== */
/* memrchr_s -> cplus_sc_upstream_memrchr_s (result via *resultp)     */
/* ================================================================== */
static void run_memrchr(void)
{
    unsigned char hay[6] = {'a', 'b', 'X', 'a', 'b', 'Y'};
    void *sentinel = (void *)(uintptr_t)0x1;
    void *result = sentinel;
    int rc;

    result = sentinel;
    rc = cplus_sc_upstream_memrchr_s(hay, sizeof(hay), 'X', &result);
    emit("memrchr_s", "found",
         (rc == CPLUS_SC_UPSTREAM_EOK && result == &hay[2]) ? "FOUND"
                                                            : "BAD_FOUND");

    result = sentinel;
    rc = cplus_sc_upstream_memrchr_s(hay, sizeof(hay), 'Z', &result);
    emit("memrchr_s", "not-found",
         (rc == CPLUS_SC_UPSTREAM_ESNOTFND && result == NULL) ? "NOTFOUND"
                                                             : "BAD_NOTFOUND");

    result = sentinel;
    rc = cplus_sc_upstream_memrchr_s(hay, sizeof(hay), 'a', &result);
    emit("memrchr_s", "last-occurrence",
         (rc == CPLUS_SC_UPSTREAM_EOK && result == &hay[3]) ? "IDX3"
                                                            : "BAD_INDEX");

    result = sentinel;
    rc = cplus_sc_upstream_memrchr_s(hay, sizeof(hay), 'X', NULL);
    emit("memrchr_s", "null-resultp",
         (rc == CPLUS_SC_UPSTREAM_ESNULLP) ? "REFUSED" : "BAD_REFUSAL");

    result = sentinel;
    rc = cplus_sc_upstream_memrchr_s(NULL, sizeof(hay), 'X', &result);
    emit("memrchr_s", "null-dest",
         (rc == CPLUS_SC_UPSTREAM_ESNULLP && result == NULL) ? "REFUSED"
                                                            : "BAD_REFUSAL");

    result = sentinel;
    rc = cplus_sc_upstream_memrchr_s(hay, 0, 'X', &result);
    emit("memrchr_s", "zero-dmax",
         (rc == CPLUS_SC_UPSTREAM_ESZEROL && result == NULL) ? "REFUSED"
                                                            : "BAD_REFUSAL");

    result = sentinel;
    rc = cplus_sc_upstream_memrchr_s(hay, LIM_MEM + 1, 'X', &result);
    emit("memrchr_s", "over-dmax",
         (rc == CPLUS_SC_UPSTREAM_ESLEMAX && result == NULL) ? "REFUSED"
                                                            : "BAD_REFUSAL");

    result = sentinel;
    rc = cplus_sc_upstream_memrchr_s(hay, sizeof(hay), 256, &result);
    emit("memrchr_s", "ch-over",
         (rc == CPLUS_SC_UPSTREAM_ESLEMAX && result == NULL) ? "REFUSED"
                                                            : "BAD_REFUSAL");

    {
        unsigned char b[8];
        int i;
        b[0] = (unsigned char)CANARY_C;
        b[7] = (unsigned char)CANARY_C;
        for (i = 0; i < 6; ++i)
            b[i + 1] = hay[i];
        result = sentinel;
        rc = cplus_sc_upstream_memrchr_s(b + 1, 6, 'X', &result);
        emit("memrchr_s", "canary",
             (b[0] == (unsigned char)CANARY_C &&
              b[7] == (unsigned char)CANARY_C &&
              rc == CPLUS_SC_UPSTREAM_EOK && result == &b[3]) ? "INTACT"
                                                             : "BAD_CANARY");
    }
}

/* ================================================================== */
/* memchr_s -> cplus_v_memchr_s (first occurrence, result via *resultp)*/
/* ================================================================== */
static void run_memchr(void)
{
    unsigned char hay[6] = {'a', 'b', 'X', 'a', 'b', 'Y'};
    void *sentinel = (void *)(uintptr_t)0x1;
    void *result = sentinel;
    int rc;

    result = sentinel;
    rc = cplus_v_memchr_s(hay, sizeof(hay), 'X', &result);
    emit("memchr_s", "found",
         (rc == CPLUS_SC_UPSTREAM_EOK && result == &hay[2]) ? "FOUND"
                                                            : "BAD_FOUND");

    result = sentinel;
    rc = cplus_v_memchr_s(hay, sizeof(hay), 'Z', &result);
    emit("memchr_s", "not-found",
         (rc == CPLUS_SC_UPSTREAM_ESNOTFND && result == NULL) ? "NOTFOUND"
                                                             : "BAD_NOTFOUND");

    result = sentinel;
    rc = cplus_v_memchr_s(hay, sizeof(hay), 'a', &result);
    emit("memchr_s", "first-occurrence",
         (rc == CPLUS_SC_UPSTREAM_EOK && result == &hay[0]) ? "IDX0"
                                                            : "BAD_INDEX");

    result = sentinel;
    rc = cplus_v_memchr_s(hay, sizeof(hay), 'X', NULL);
    emit("memchr_s", "null-resultp",
         (rc == CPLUS_SC_UPSTREAM_ESNULLP) ? "REFUSED" : "BAD_REFUSAL");

    result = sentinel;
    rc = cplus_v_memchr_s(NULL, sizeof(hay), 'X', &result);
    emit("memchr_s", "null-dest",
         (rc == CPLUS_SC_UPSTREAM_ESNULLP && result == NULL) ? "REFUSED"
                                                            : "BAD_REFUSAL");

    result = sentinel;
    rc = cplus_v_memchr_s(hay, 0, 'X', &result);
    emit("memchr_s", "zero-dmax",
         (rc == CPLUS_SC_UPSTREAM_ESZEROL && result == NULL) ? "REFUSED"
                                                            : "BAD_REFUSAL");

    result = sentinel;
    rc = cplus_v_memchr_s(hay, LIM_MEM + 1, 'X', &result);
    emit("memchr_s", "over-dmax",
         (rc == CPLUS_SC_UPSTREAM_ESLEMAX && result == NULL) ? "REFUSED"
                                                            : "BAD_REFUSAL");

    result = sentinel;
    rc = cplus_v_memchr_s(hay, sizeof(hay), 256, &result);
    emit("memchr_s", "ch-over",
         (rc == CPLUS_SC_UPSTREAM_ESLEMAX && result == NULL) ? "REFUSED"
                                                            : "BAD_REFUSAL");

    {
        unsigned char b[8];
        int i;
        b[0] = (unsigned char)CANARY_C;
        b[7] = (unsigned char)CANARY_C;
        for (i = 0; i < 6; ++i)
            b[i + 1] = hay[i];
        result = sentinel;
        rc = cplus_v_memchr_s(b + 1, 6, 'X', &result);
        emit("memchr_s", "canary",
             (b[0] == (unsigned char)CANARY_C &&
              b[7] == (unsigned char)CANARY_C &&
              rc == CPLUS_SC_UPSTREAM_EOK && result == &b[3]) ? "INTACT"
                                                             : "BAD_CANARY");
    }
}

/* ================================================================== */
/* strchr_s -> cplus_v__strchr_s_chk (BOS_UNKNOWN forces runtime check)*/
/* ================================================================== */
static void run_strchr(void)
{
    char hay[] = "abXabY"; /* length 6 */
    const size_t n = strlen(hay);
    char *sentinel = (char *)(uintptr_t)0x1;
    char *result = sentinel;
    int rc;

    result = sentinel;
    rc = cplus_v__strchr_s_chk(hay, n, 'X', &result, BOS_UNKNOWN_S);
    emit("strchr_s", "found",
         (rc == CPLUS_VENDOR_STRCHR_EOK && result == &hay[2]) ? "FOUND"
                                                              : "BAD_FOUND");

    result = sentinel;
    rc = cplus_v__strchr_s_chk(hay, n, 'Z', &result, BOS_UNKNOWN_S);
    emit("strchr_s", "not-found",
         (rc == CPLUS_VENDOR_STRCHR_ESNOTFND && result == NULL) ? "NOTFOUND"
                                                               : "BAD_NOTFOUND");

    result = sentinel;
    rc = cplus_v__strchr_s_chk(NULL, n, 'X', &result, BOS_UNKNOWN_S);
    emit("strchr_s", "null-dest",
         (rc == CPLUS_VENDOR_STRCHR_ESNULLP && result == NULL) ? "REFUSED"
                                                              : "BAD_REFUSAL");

    rc = cplus_v__strchr_s_chk(hay, n, 'X', NULL, BOS_UNKNOWN_S);
    emit("strchr_s", "null-resultp",
         (rc == CPLUS_VENDOR_STRCHR_ESNULLP) ? "REFUSED" : "BAD_REFUSAL");

    result = sentinel;
    rc = cplus_v__strchr_s_chk(hay, 0, 'X', &result, BOS_UNKNOWN_S);
    emit("strchr_s", "zero-dmax",
         (rc == CPLUS_VENDOR_STRCHR_ESZEROL && result == NULL) ? "REFUSED"
                                                              : "BAD_REFUSAL");

    result = sentinel;
    rc = cplus_v__strchr_s_chk(hay, LIM_STR + 1, 'X', &result, BOS_UNKNOWN_S);
    emit("strchr_s", "over-dmax",
         (rc == CPLUS_VENDOR_STRCHR_ESLEMAX && result == NULL) ? "REFUSED"
                                                              : "BAD_REFUSAL");

    result = sentinel;
    rc = cplus_v__strchr_s_chk(hay, n, 256, &result, BOS_UNKNOWN_S);
    emit("strchr_s", "ch-over",
         (rc == CPLUS_VENDOR_STRCHR_ESLEMAX && result == NULL) ? "REFUSED"
                                                              : "BAD_REFUSAL");

    {
        char b[9];
        int i;
        b[0] = CANARY_C;
        b[7] = CANARY_C;
        b[8] = '\0';
        for (i = 0; i < 6; ++i)
            b[i + 1] = hay[i];
        result = sentinel;
        rc = cplus_v__strchr_s_chk(b + 1, 6, 'X', &result, BOS_UNKNOWN_S);
        emit("strchr_s", "canary",
             (b[0] == CANARY_C && b[7] == CANARY_C &&
              rc == CPLUS_VENDOR_STRCHR_EOK && result == &b[3]) ? "INTACT"
                                                               : "BAD_CANARY");
    }
}

/* ================================================================== */
/* strrchr_s -> cplus_v__strrchr_s_chk                                */
/* ================================================================== */
static void run_strrchr(void)
{
    char hay[] = "abXabY"; /* length 6: 'a' first@0 last@3 */
    const size_t n = strlen(hay);
    char *sentinel = (char *)(uintptr_t)0x1;
    char *result = sentinel;
    int rc;

    result = sentinel;
    rc = cplus_v__strrchr_s_chk(hay, n, 'X', &result, BOS_UNKNOWN_S);
    emit("strrchr_s", "found",
         (rc == CPLUS_VENDOR_STRCHR_EOK && result == &hay[2]) ? "FOUND"
                                                              : "BAD_FOUND");

    result = sentinel;
    rc = cplus_v__strrchr_s_chk(hay, n, 'Z', &result, BOS_UNKNOWN_S);
    emit("strrchr_s", "not-found",
         (rc == CPLUS_VENDOR_STRCHR_ESNOTFND && result == NULL) ? "NOTFOUND"
                                                               : "BAD_NOTFOUND");

    result = sentinel;
    rc = cplus_v__strrchr_s_chk(hay, n, 'a', &result, BOS_UNKNOWN_S);
    emit("strrchr_s", "last-occurrence",
         (rc == CPLUS_VENDOR_STRCHR_EOK && result == &hay[3]) ? "IDX3"
                                                              : "BAD_INDEX");

    result = sentinel;
    rc = cplus_v__strrchr_s_chk(NULL, n, 'X', &result, BOS_UNKNOWN_S);
    emit("strrchr_s", "null-dest",
         (rc == CPLUS_VENDOR_STRCHR_ESNULLP && result == NULL) ? "REFUSED"
                                                              : "BAD_REFUSAL");

    rc = cplus_v__strrchr_s_chk(hay, n, 'X', NULL, BOS_UNKNOWN_S);
    emit("strrchr_s", "null-resultp",
         (rc == CPLUS_VENDOR_STRCHR_ESNULLP) ? "REFUSED" : "BAD_REFUSAL");

    result = sentinel;
    rc = cplus_v__strrchr_s_chk(hay, 0, 'X', &result, BOS_UNKNOWN_S);
    emit("strrchr_s", "zero-dmax",
         (rc == CPLUS_VENDOR_STRCHR_ESZEROL && result == NULL) ? "REFUSED"
                                                              : "BAD_REFUSAL");

    result = sentinel;
    rc = cplus_v__strrchr_s_chk(hay, LIM_STR + 1, 'X', &result, BOS_UNKNOWN_S);
    emit("strrchr_s", "over-dmax",
         (rc == CPLUS_VENDOR_STRCHR_ESLEMAX && result == NULL) ? "REFUSED"
                                                              : "BAD_REFUSAL");

    result = sentinel;
    rc = cplus_v__strrchr_s_chk(hay, n, 256, &result, BOS_UNKNOWN_S);
    emit("strrchr_s", "ch-over",
         (rc == CPLUS_VENDOR_STRCHR_ESLEMAX && result == NULL) ? "REFUSED"
                                                              : "BAD_REFUSAL");

    {
        char b[9];
        int i;
        b[0] = CANARY_C;
        b[7] = CANARY_C;
        b[8] = '\0';
        for (i = 0; i < 6; ++i)
            b[i + 1] = hay[i];
        result = sentinel;
        rc = cplus_v__strrchr_s_chk(b + 1, 6, 'a', &result, BOS_UNKNOWN_S);
        emit("strrchr_s", "canary",
             (b[0] == CANARY_C && b[7] == CANARY_C &&
              rc == CPLUS_VENDOR_STRCHR_EOK && result == &b[4])
                 ? "INTACT"
                 : "BAD_CANARY");
    }
}

/* ================================================================== */
/* strstr_s -> cplus_v__strstr_s_chk                                  */
/* ================================================================== */
static void run_strstr(void)
{
    char hay[] = "abXcdY"; /* length 6; "Xc" @2 */
    char sub[] = "Xc";
    const size_t n = strlen(hay);
    const size_t slen = strlen(sub);
    char *sentinel = (char *)(uintptr_t)0x1;
    char *result = sentinel;
    int rc;

    result = sentinel;
    rc = cplus_v__strstr_s_chk(hay, n, sub, slen, &result, BOS_UNKNOWN_S,
                               BOS_UNKNOWN_S);
    emit("strstr_s", "found",
         (rc == CPLUS_V_STRSTR_EOK && result == &hay[2]) ? "FOUND"
                                                         : "BAD_FOUND");

    {
        char miss[] = "ZZ";
        result = sentinel;
        rc = cplus_v__strstr_s_chk(hay, n, miss, strlen(miss), &result,
                                   BOS_UNKNOWN_S, BOS_UNKNOWN_S);
        emit("strstr_s", "not-found",
             (rc == CPLUS_V_STRSTR_ESNOTFND && result == NULL) ? "NOTFOUND"
                                                              : "BAD_NOTFOUND");
    }

    result = sentinel;
    rc = cplus_v__strstr_s_chk(NULL, n, sub, slen, &result, BOS_UNKNOWN_S,
                               BOS_UNKNOWN_S);
    emit("strstr_s", "null-dest",
         (rc == CPLUS_V_STRSTR_ESNULLP && result == NULL) ? "REFUSED"
                                                          : "BAD_REFUSAL");

    result = sentinel;
    rc = cplus_v__strstr_s_chk(hay, n, NULL, slen, &result, BOS_UNKNOWN_S,
                               BOS_UNKNOWN_S);
    emit("strstr_s", "null-src",
         (rc == CPLUS_V_STRSTR_ESNULLP && result == NULL) ? "REFUSED"
                                                          : "BAD_REFUSAL");

    rc = cplus_v__strstr_s_chk(hay, n, sub, slen, NULL, BOS_UNKNOWN_S,
                               BOS_UNKNOWN_S);
    emit("strstr_s", "null-resultp",
         (rc == CPLUS_V_STRSTR_ESNULLP) ? "REFUSED" : "BAD_REFUSAL");

    result = sentinel;
    rc = cplus_v__strstr_s_chk(hay, 0, sub, slen, &result, BOS_UNKNOWN_S,
                               BOS_UNKNOWN_S);
    emit("strstr_s", "zero-dmax",
         (rc == CPLUS_V_STRSTR_ESZEROL && result == NULL) ? "REFUSED"
                                                          : "BAD_REFUSAL");

    result = sentinel;
    rc = cplus_v__strstr_s_chk(hay, LIM_STR + 1, sub, slen, &result,
                               BOS_UNKNOWN_S, BOS_UNKNOWN_S);
    emit("strstr_s", "over-dmax",
         (rc == CPLUS_V_STRSTR_ESLEMAX && result == NULL) ? "REFUSED"
                                                          : "BAD_REFUSAL");

    result = sentinel;
    rc = cplus_v__strstr_s_chk(hay, n, sub, 0, &result, BOS_UNKNOWN_S,
                               BOS_UNKNOWN_S);
    emit("strstr_s", "zero-slen",
         (rc == CPLUS_V_STRSTR_ESZEROL && result == NULL) ? "REFUSED"
                                                          : "BAD_REFUSAL");

    {
        char b[9];
        int i;
        b[0] = CANARY_C;
        b[7] = CANARY_C;
        b[8] = '\0';
        for (i = 0; i < 6; ++i)
            b[i + 1] = hay[i];
        result = sentinel;
        rc = cplus_v__strstr_s_chk(b + 1, 6, sub, slen, &result, BOS_UNKNOWN_S,
                                   BOS_UNKNOWN_S);
        emit("strstr_s", "canary",
             (b[0] == CANARY_C && b[7] == CANARY_C &&
              rc == CPLUS_V_STRSTR_EOK && result == &b[3]) ? "INTACT"
                                                           : "BAD_CANARY");
    }
}

int main(void)
{
    cplus_sc_set_mem_constraint_handler_s(noop_handler);
    cplus_sc_set_str_constraint_handler_s(noop_handler);

    run_bsearch();
    run_memrchr();
    run_memchr();
    run_strchr();
    run_strrchr();
    run_strstr();
    return 0;
}
