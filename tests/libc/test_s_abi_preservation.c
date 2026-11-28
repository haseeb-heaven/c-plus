/*
 * Issue #189 regression: preserve the C ABI for feasible _s methods and keep
 * ABI-incompatible Annex K _s variants additive / opt-in.
 *
 * Compile-time proof: standard C symbols are assigned to exactly typed
 * function pointers while the C+ headers are included.  Any changed
 * parameter count, return type or linkage is a hard compile error here.
 *
 * Runtime proof: the ABI-preserving libc allocators (cplus_malloc/calloc/
 * realloc/free, the plain-C-shape counterparts of malloc_s etc.) work with
 * standard C signatures, and the additive cplus_sc_upstream_*_s symbols are
 * callable without ever redefining bsearch/memrchr in the host CRT.
 *
 * The companion shell suite (run_s_abi_regression.sh) inspects the archive
 * symbol table to prove that no unprefixed Annex K _s symbol leaks out.
 *
 * Built by tests/libc/run_libc_tests.sh like every other test_*.c file, or
 * standalone:
 *   clang -O2 -Wall -Wextra -pedantic -std=c99 \
 *     -Ilibc -Ilibc/include -Ilibc/install-include \
 *     tests/libc/test_s_abi_preservation.c \
 *     build/libc/libcplus_hardened.a -lm -pthread -o /tmp/s_abi_test
 *   timeout 60 /tmp/s_abi_test
 *
 * Prints exactly:  s_abi_preservation: <checks> <failures>
 */
#define CPLUS_LIBC_SAFE_DISABLE 1 /* declarations only: no malloc/memcpy macros */
#include "cplus_libc.h"
#include "cplus_libc_safe.h"
#include "cplus_safecrt_upstream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++checks;                                                              \
        if (!(cond)) {                                                         \
            ++failures;                                                        \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
        }                                                                      \
    } while (0)

/* ---- standard C ABI is unchanged ----------------------------------------- */
static void test_standard_c_abi(void) {
    void *(*const p_malloc)(size_t) = malloc;
    void (*const p_free)(void *) = free;
    void *(*const p_calloc)(size_t, size_t) = calloc;
    void *(*const p_realloc)(void *, size_t) = realloc;
    void *(*const p_memcpy)(void *, const void *, size_t) = memcpy;
    void *(*const p_memmove)(void *, const void *, size_t) = memmove;
    void *(*const p_memset)(void *, int, size_t) = memset;
    char *(*const p_strcpy)(char *, const char *) = strcpy;
    char *(*const p_strncpy)(char *, const char *, size_t) = strncpy;
    char *(*const p_strcat)(char *, const char *) = strcat;
    char *(*const p_strncat)(char *, const char *, size_t) = strncat;
    FILE *(*const p_fopen)(const char *, const char *) = fopen;
    int (*const p_fclose)(FILE *) = fclose;
    void (*const p_qsort)(void *, size_t, size_t,
                          int (*)(const void *, const void *)) = qsort;
    void *(*const p_bsearch)(const void *, const void *, size_t, size_t,
                             int (*)(const void *, const void *)) = bsearch;
    int (*const p_snprintf)(char *, size_t, const char *, ...) = snprintf;
    char *(*const p_getenv)(const char *) = getenv;
    char *(*const p_strerror)(int) = strerror;

    CHECK(p_malloc != NULL);
    CHECK(p_free != NULL);
    CHECK(p_calloc != NULL);
    CHECK(p_realloc != NULL);
    CHECK(p_memcpy != NULL);
    CHECK(p_memmove != NULL);
    CHECK(p_memset != NULL);
    CHECK(p_strcpy != NULL);
    CHECK(p_strncpy != NULL);
    CHECK(p_strcat != NULL);
    CHECK(p_strncat != NULL);
    CHECK(p_fopen != NULL);
    CHECK(p_fclose != NULL);
    CHECK(p_qsort != NULL);
    CHECK(p_bsearch != NULL);
    CHECK(p_snprintf != NULL);
    CHECK(p_getenv != NULL);
    CHECK(p_strerror != NULL);

    /* Exercise one of each shape so the pointers are not dead code. */
    {
        char dst[16];
        char buf[32];
        CHECK(p_strcpy(dst, "abi") == dst);
        CHECK(strcmp(dst, "abi") == 0);
        CHECK(p_snprintf(buf, sizeof buf, "%s-%d", dst, 1) == 5);
        CHECK(strcmp(buf, "abi-1") == 0);
    }
}

/* ---- ABI-preserving checked allocators keep plain-C signatures ----------- */
static void test_libc_abi_preserving_allocators(void) {
    void *(*const cp_malloc)(size_t) = cplus_malloc;
    void (*const cp_free)(void *) = cplus_free;
    void *(*const cp_calloc)(size_t, size_t) = cplus_calloc;
    void *(*const cp_realloc)(void *, size_t) = cplus_realloc;
    void *p;

    CHECK(cp_malloc != NULL);
    CHECK(cp_free != NULL);
    CHECK(cp_calloc != NULL);
    CHECK(cp_realloc != NULL);

    p = cp_malloc(32u);
    CHECK(p != NULL);
    if (p != NULL) {
        memset(p, 0x5Au, 32u);
        p = cp_realloc(p, 64u);
        CHECK(p != NULL);
        if (p != NULL) cp_free(p);
    }
    p = cp_calloc(4u, 8u);
    CHECK(p != NULL);
    if (p != NULL) cp_free(p);
}

/* ---- Annex K incompatible variants are additive, namespaced symbols ----- */
static int cmp_int_ctx(const void *a, const void *b, void *ctx) {
    int x = *(const int *)a;
    int y = *(const int *)b;
    (void)ctx;
    return (x > y) - (x < y);
}

static void test_additive_s_api(void) {
    void *(*const p_bsearch_s)(
        const void *, const void *, size_t, size_t,
        int (*)(const void *, const void *, void *), void *) =
        cplus_sc_upstream_bsearch_s;
    int (*const p_memrchr_s)(const void *, size_t, int, void **) =
        cplus_sc_upstream_memrchr_s;
    int data[4] = {1, 3, 5, 7};
    int key = 5;
    unsigned char bytes[4] = {1u, 2u, 1u, 2u};
    void *found = NULL;

    CHECK(p_bsearch_s != NULL);
    CHECK(p_memrchr_s != NULL);
    CHECK(p_bsearch_s(&key, data, 4u, sizeof(int), cmp_int_ctx, NULL) ==
          &data[2]);
    CHECK(p_memrchr_s(bytes, 4u, 1, &found) == CPLUS_SC_UPSTREAM_EOK);
    CHECK(found == &bytes[2]);
}

int main(void) {
    test_standard_c_abi();
    test_libc_abi_preserving_allocators();
    test_additive_s_api();
    printf("s_abi_preservation: %d %d\n", checks, failures);
    return failures ? 1 : 0;
}
