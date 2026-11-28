/* Deterministic per-method safety-outcome harness for the C+ checked search
 * APIs.  Build with:
 *
 *   /usr/bin/clang -std=c99 -Ilibc/install-include \
 *     -Ithird_party/safeclib/include harness.c \
 *     build/libc/libcplus_hardened.a -o harness_c
 *
 * The symbols referenced are the three prefixed exports available at the
 * measured commit (verified with nm; see NOTES.md):
 *   cplus_sc_upstream_bsearch_s                  (libcplus_hardened.a)
 *   cplus_sc_upstream_memrchr_s                  (libcplus_hardened.a)
 *   cplus_v_memchr_s                             (libcplus_safecrt.a)
 * qsort_s has no linkable symbol in any archive (only a macro in safeclib's
 * safe_lib.h), so it is reported BLOCKED by compare.sh and not exercised.
 * The two archives must both be on the link line.
 *
 * Every line of stdout has the exact form:  <method> <case> <token>
 * Tokens are outcome observations only.  See NOTES.md for the explicit
 * non-equivalence statement versus the Rust harness.
 */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "cplus_safecrt_upstream.h"
#include "cplus_vendor_mem.h"

/* The vendored memchr_s routes constraint reports through safeclib's global
 * mem/str handlers, whose default implementation prints to stdout.  Install
 * no-op handlers so stdout contains ONLY deterministic <method> <case>
 * <token> lines.  errno_t == int in the pinned safeclib headers. */
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

/* safeclib RSIZE_MAX_MEM == RSIZE_MAX_STR(4096) * 4 == 16384, the same value
 * as CPLUS_SC_UPSTREAM_RSIZE_MAX_MEM used by the two upstream APIs. */
#define LIMIT ((size_t)CPLUS_SC_UPSTREAM_RSIZE_MAX_MEM)
#define CTX_MAGIC ((unsigned)0x5A5A5A5A)
#define CANARY_I ((int)0x5A5AA5A5)
#define CANARY_C ((unsigned char)0xA5)

typedef struct {
    unsigned magic;
} ctx_t;

static void emit(const char *method, const char *case_name, const char *token)
{
    printf("%s %s %s\n", method, case_name, token);
}

/* ------------------------------------------------------------------ */
/* bsearch_s comparator trio.  C11 K.3.6.3.1 compar(key, element, ctx)  */
/* ------------------------------------------------------------------ */
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
    /* A comparator that did not receive the context must never report a
     * match; this makes context propagation observable in the output. */
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

static void run_bsearch_cases(void)
{
    int a[5] = {1, 3, 5, 7, 9};
    ctx_t good = {CTX_MAGIC};
    void *r;
    int key;

    /* found */
    key = 7;
    errno = 999;
    r = cplus_sc_upstream_bsearch_s(&key, a, 5, sizeof(a[0]), cmp_plain, NULL);
    emit("bsearch_s", "found",
         (r == &a[3] && errno == 0) ? "FOUND" : "BAD_FOUND");

    /* not-found: valid search, errno cleared, NULL result */
    key = 6;
    errno = 999;
    r = cplus_sc_upstream_bsearch_s(&key, a, 5, sizeof(a[0]), cmp_plain, NULL);
    emit("bsearch_s", "not-found",
         (r == NULL && errno == 0) ? "NOTFOUND" : "BAD_NOTFOUND");

    /* NULL arguments -> runtime-constraint refusal (ESNULLP) */
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

    /* zero count: safeclib has NO constraint for nmemb == 0; this is a clean
     * empty search (NULL, errno == 0), deliberately token EMPTY, not REFUSED.
     * See NOTES.md -- this is an honest deviation from the generic
     * "zero count -> REFUSED" expectation. */
    errno = 999;
    compar_calls = 0;
    r = cplus_sc_upstream_bsearch_s(&key, a, 0, sizeof(a[0]), cmp_counting,
                                    NULL);
    emit("bsearch_s", "zero-nmemb",
         (r == NULL && errno == 0 && compar_calls == 0) ? "EMPTY" : "BAD_EMPTY");

    /* one-over the documented RSIZE_MAX_MEM limit -> ESLEMAX, no comparator
     * call (proves the refusal happens before any element access). */
    errno = 999;
    compar_calls = 0;
    r = cplus_sc_upstream_bsearch_s(&key, a, LIMIT + 1, sizeof(a[0]),
                                    cmp_counting, NULL);
    emit("bsearch_s", "over-nmemb",
         (r == NULL && errno == CPLUS_SC_UPSTREAM_ESLEMAX && compar_calls == 0)
             ? "REFUSED"
             : "BAD_REFUSAL");

    errno = 999;
    compar_calls = 0;
    r = cplus_sc_upstream_bsearch_s(&key, a, 5, LIMIT + 1, cmp_counting, NULL);
    emit("bsearch_s", "over-size",
         (r == NULL && errno == CPLUS_SC_UPSTREAM_ESLEMAX && compar_calls == 0)
             ? "REFUSED"
             : "BAD_REFUSAL");

    /* comparator context propagation: correct context finds 7 ... */
    key = 7;
    errno = 999;
    r = cplus_sc_upstream_bsearch_s(&key, a, 5, sizeof(a[0]), cmp_ctx, &good);
    emit("bsearch_s", "context-ok",
         (r == &a[3] && errno == 0) ? "CTX_OK" : "BAD_CTX");

    /* ... a lost context makes the comparator refuse to match. */
    errno = 999;
    r = cplus_sc_upstream_bsearch_s(&key, a, 5, sizeof(a[0]), cmp_ctx, NULL);
    emit("bsearch_s", "context-lost",
         (r == NULL && errno == 0) ? "CTX_LOST" : "BAD_CTX");

    /* canary: guard ints on both sides of the searched region stay intact. */
    {
        int b[7];
        int i;
        b[0] = CANARY_I;
        b[6] = CANARY_I;
        for (i = 0; i < 5; ++i)
            b[i + 1] = a[i];
        key = 5;
        errno = 999;
        r = cplus_sc_upstream_bsearch_s(&key, b + 1, 5, sizeof(b[0]), cmp_plain,
                                        NULL);
        emit("bsearch_s", "canary",
             (b[0] == CANARY_I && b[6] == CANARY_I && r == &b[3]) ? "INTACT"
                                                                 : "BAD_CANARY");
    }
}

/* ------------------------------------------------------------------ */
/* memrchr_s: result is returned through *resultp                      */
/* ------------------------------------------------------------------ */
static void run_memrchr_cases(void)
{
    unsigned char hay[6] = {'a', 'b', 'X', 'a', 'b', 'Y'};
    void *sentinel = (void *)(uintptr_t)0x1;
    void *result = sentinel;
    int rc;

    /* found / not-found / last-occurrence */
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

    /* NULL resultp / NULL dest -> ESNULLP */
    result = sentinel;
    rc = cplus_sc_upstream_memrchr_s(hay, sizeof(hay), 'X', NULL);
    emit("memrchr_s", "null-resultp",
         (rc == CPLUS_SC_UPSTREAM_ESNULLP) ? "REFUSED" : "BAD_REFUSAL");

    result = sentinel;
    rc = cplus_sc_upstream_memrchr_s(NULL, sizeof(hay), 'X', &result);
    emit("memrchr_s", "null-dest",
         (rc == CPLUS_SC_UPSTREAM_ESNULLP && result == NULL) ? "REFUSED"
                                                            : "BAD_REFUSAL");

    /* zero and one-over dmax -> ESZEROL / ESLEMAX */
    result = sentinel;
    rc = cplus_sc_upstream_memrchr_s(hay, 0, 'X', &result);
    emit("memrchr_s", "zero-dmax",
         (rc == CPLUS_SC_UPSTREAM_ESZEROL && result == NULL) ? "REFUSED"
                                                            : "BAD_REFUSAL");

    result = sentinel;
    rc = cplus_sc_upstream_memrchr_s(hay, LIMIT + 1, 'X', &result);
    emit("memrchr_s", "over-dmax",
         (rc == CPLUS_SC_UPSTREAM_ESLEMAX && result == NULL) ? "REFUSED"
                                                            : "BAD_REFUSAL");

    /* ch > UCHAR_MAX -> ESLEMAX */
    result = sentinel;
    rc = cplus_sc_upstream_memrchr_s(hay, sizeof(hay), 256, &result);
    emit("memrchr_s", "ch-over",
         (rc == CPLUS_SC_UPSTREAM_ESLEMAX && result == NULL) ? "REFUSED"
                                                            : "BAD_REFUSAL");

    /* canary: guard bytes before/after the scanned region stay intact. */
    {
        unsigned char b[8];
        int i;
        b[0] = CANARY_C;
        b[7] = CANARY_C;
        for (i = 0; i < 6; ++i)
            b[i + 1] = hay[i];
        result = sentinel;
        rc = cplus_sc_upstream_memrchr_s(b + 1, 6, 'X', &result);
        emit("memrchr_s", "canary",
             (b[0] == CANARY_C && b[7] == CANARY_C &&
              rc == CPLUS_SC_UPSTREAM_EOK && result == &b[3])
                 ? "INTACT"
                 : "BAD_CANARY");
    }
}

/* ------------------------------------------------------------------ */
/* memchr_s (vendored safeclib): first occurrence, result via *resultp   */
/* ------------------------------------------------------------------ */
static void run_memchr_cases(void)
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
    rc = cplus_v_memchr_s(hay, LIMIT + 1, 'X', &result);
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
        b[0] = CANARY_C;
        b[7] = CANARY_C;
        for (i = 0; i < 6; ++i)
            b[i + 1] = hay[i];
        result = sentinel;
        rc = cplus_v_memchr_s(b + 1, 6, 'X', &result);
        emit("memchr_s", "canary",
             (b[0] == CANARY_C && b[7] == CANARY_C &&
              rc == CPLUS_SC_UPSTREAM_EOK && result == &b[3])
                 ? "INTACT"
                 : "BAD_CANARY");
    }
}

int main(void)
{
    cplus_sc_set_mem_constraint_handler_s(noop_handler);
    cplus_sc_set_str_constraint_handler_s(noop_handler);
    run_bsearch_cases();
    run_memrchr_cases();
    run_memchr_cases();
    return 0;
}
