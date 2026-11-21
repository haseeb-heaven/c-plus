/* Adversarial mitigation proof, part 3: runtime-catchable subset of the second
 * catalogue (bugs 105-204), driven through the checked C+ safe API.
 *
 * Each scenario proves that, for a bug pattern that is deterministic and runtime-
 * observable, the checked API returns a defined error (or safely succeeds)
 * instead of invoking undefined behaviour.  Patterns that need provenance/
 * lifetime/compile-time reasoning are classified in docs/libc/ADVERSARIAL_MATRIX.md
 * (section "Catalogue 2") and are intentionally not claimed here.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "safe_memory.h"
#include "safe_string.h"
#include "safe_alloc.h"
#include "safe_format.h"
#include "safe_error.h"
#include "safe_log.h"

#define RUN(f) do { f(); printf("ok %s\n", #f); } while (0)

/* bug105: caller-side size product overflow -> checked calloc_s rejects. */
static void catch_calloc_overflow(void) {
    void *p = NULL;
    assert(calloc_s(&p, (size_t)-1, 4) == CPLUS_SAFE_EOVERFLOW);
    assert(p == NULL);
    assert(calloc_s(&p, SIZE_MAX / 4 + 1, 4) == CPLUS_SAFE_EOVERFLOW);
    assert(p == NULL);
    assert(calloc_s(&p, 2, SIZE_MAX / 2 + 1) == CPLUS_SAFE_EOVERFLOW);
    assert(p == NULL);
    /* representable product is not overflow: it reports OOM instead */
    assert(calloc_s(&p, SIZE_MAX, 1) == CPLUS_SAFE_ENOMEM);
    assert(p == NULL);
}

/* bug107: a failing/missing allocation must leave a defined handle, never stale. */
static void catch_uninit_out_param(void) {
    void *p = (void *)0xdeadbeef;
    assert(malloc_s(&p, (size_t)-1) == CPLUS_SAFE_ENOMEM);
    assert(p == NULL);
    p = (void *)0xdeadbeef;
    assert(calloc_s(&p, (size_t)-1, 8) == CPLUS_SAFE_EOVERFLOW);
    assert(p == NULL);
    assert(malloc_s(NULL, 8) == CPLUS_SAFE_EINVAL);
    assert(calloc_s(NULL, 1, 1) == CPLUS_SAFE_EINVAL);
    assert(malloc_s(&p, 0) == CPLUS_SAFE_OK);       /* zero -> 1 byte, defined */
    assert(p != NULL);
    free_s(&p);
    assert(p == NULL);
    free_s(NULL);                                   /* no crash */
}

/* bug143: sprintf into a too-small buffer -> bounded variant, no overflow. */
static void catch_sprintf_flood(void) {
    char buf[5];
    memset(buf, 'Z', sizeof buf);
    int r = cplus_sprintf_s(buf, sizeof buf, "%s", "0123456789");
    assert(r == 10);                                /* reports required length */
    assert((int)strlen(buf) == 4);                  /* bounded + NUL-terminated */
    assert(buf[4] == '\0');
    assert(cplus_sprintf_s(buf, sizeof buf, "%d", 7) == 1);
    assert(cplus_snprintf_s(NULL, 8, "%s", "x") == -1);   /* invalid dest/dmax */
    assert(cplus_snprintf_s(buf, sizeof buf, NULL) == -1);
}

/* bug153: writing past a struct member -> bounded copy rejects overflow. */
static void catch_subobject_overflow(void) {
    struct { char name[4]; unsigned guard; } rec;
    rec.guard = 0xA5A5A5A5u;
    assert(cplus_strcpy_s(rec.name, sizeof rec.name, "alice") != 0);
    assert(rec.guard == 0xA5A5A5A5u);
    assert(cplus_strcpy_s(rec.name, sizeof rec.name, "bob") == 0);
    assert(strcmp(rec.name, "bob") == 0);
    assert(rec.guard == 0xA5A5A5A5u);
}

/* bug173: off-by-one memmove shift -> bounded memmove_s rejects overflow. */
static void catch_memmove_shift(void) {
    int arr[5] = { 1, 2, 3, 4, 5 };
    assert(cplus_memmove_s(arr, sizeof arr, arr, sizeof arr) == 0); /* exact */
    for (int i = 0; i < 5; ++i) assert(arr[i] == i + 1);
    assert(cplus_memmove_s(arr, sizeof(int), arr, sizeof arr) != 0); /* too small */
    assert(cplus_memmove_s(arr, sizeof arr - 1, arr, sizeof arr) != 0);
}

/* bug184: strncat loop passing total size each time -> bounded variant rejects. */
static void catch_strncat_loop(void) {
    char dst[16];
    assert(cplus_strcpy_s(dst, sizeof dst, "") == 0);
    const char *parts[] = { "alpha", "beta", "gamma", "delta" };
    int overflowed = 0;
    for (int i = 0; i < 4; ++i) {
        if (cplus_strncat_s(dst, sizeof dst, parts[i], sizeof dst) != 0) { overflowed = 1; break; }
        assert(strlen(dst) <= sizeof dst - 1);
    }
    assert(overflowed == 1);                        /* caught instead of smashing */
}

/* bug191: zero-length copy with NULL pointers must not dereference anything. */
static void catch_zero_len_null(void) {
    char d[4];
    assert(cplus_memcpy_s(NULL, 0, NULL, 0) == 0);
    assert(cplus_memmove_s(NULL, 0, NULL, 0) == 0);
    assert(cplus_memset_s(d, 0, 0, 0) == 0);
    /* non-zero count with NULL is still a caught violation */
    assert(cplus_memcpy_s(NULL, 0, NULL, 1) != 0);
    assert(cplus_memset_s(NULL, 0, 0, 0) != 0);
}

/* bug199: realloc(ptr, 0) must have a defined outcome via the checked API. */
static void catch_realloc_zero(void) {
    void *p = NULL;
    assert(malloc_s(&p, 32) == CPLUS_SAFE_OK);
    assert(realloc_s(&p, 0) == CPLUS_SAFE_OK);
    assert(p != NULL);                              /* defined, still owned */
    free_s(&p);
    assert(p == NULL);
}

/* bug032: realloc failure must not lose the original pointer (no leak/UAF). */
static void catch_realloc_failure_preserves(void) {
    void *p = NULL;
    assert(malloc_s(&p, 32) == CPLUS_SAFE_OK);
    memset(p, 0xAB, 32);
    void *const old = p;
    assert(realloc_s(&p, (size_t)-1) == CPLUS_SAFE_ENOMEM);
    assert(p == old);
    assert(((unsigned char *)p)[0] == 0xAB);
    free_s(&p);
}

/* no checked memory op may ever write past the caller-declared bound. */
static void catch_no_write_past_bound(void) {
    unsigned char guard[16];
    memset(guard, 0x5A, sizeof guard);
    assert(cplus_memcpy_s(guard, 4, "abcdefgh", 8) != 0);
    for (int i = 4; i < 16; ++i) assert(guard[i] == 0x5A);
    memset(guard, 0x5A, sizeof guard);
    assert(cplus_memset_s(guard, 4, 0x22, 8) != 0);
    for (int i = 4; i < 16; ++i) assert(guard[i] == 0x5A);
}

/* argument-validation sweep shared by the checked memory/string entry points. */
static void catch_arg_validation(void) {
    char d[8];
    assert(cplus_strcpy_s(NULL, sizeof d, "x") != 0);
    assert(cplus_strcpy_s(d, 0, "x") != 0);
    assert(cplus_strcpy_s(d, 1, "x") != 0);
    assert(cplus_strncat_s(NULL, sizeof d, "x", 1) != 0);
    assert(cplus_memcpy_s(NULL, sizeof d, "x", 1) != 0);
    assert(cplus_memmove_s(d, sizeof d, NULL, 1) != 0);
    assert(cplus_memset_s(d, 4, 'A', 8) != 0);
}

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    RUN(catch_calloc_overflow);
    RUN(catch_uninit_out_param);
    RUN(catch_sprintf_flood);
    RUN(catch_subobject_overflow);
    RUN(catch_memmove_shift);
    RUN(catch_strncat_loop);
    RUN(catch_zero_len_null);
    RUN(catch_realloc_zero);
    RUN(catch_realloc_failure_preserves);
    RUN(catch_no_write_past_bound);
    RUN(catch_arg_validation);
    puts("catalogue2_runtime_catch: ok");
    return 0;
}
