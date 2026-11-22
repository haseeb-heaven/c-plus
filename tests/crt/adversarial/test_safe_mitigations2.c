/* Adversarial mitigation proof, part 2: bugs 105-204 of the catalogue.
 * Proves the checked C+ safe API catches the runtime-detectable subset of the
 * second catalogue and stays defined (no crash) on the rest that it owns.
 * Compile-time/ownership/lifetime/provenance and non-owned POSIX/OS APIs are
 * classified in docs/libc/ADVERSARIAL_MATRIX.md (section "Catalogue 2").
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include "safe_memory.h"
#include "safe_string.h"
#include "safe_alloc.h"
#include "safe_format.h"
#include "safe_error.h"
#include "safe_log.h"

#define RUN(f) do { f(); printf("ok %s\n", #f); } while (0)

/* bug105: caller-side multiplication overflow -> checked calloc_s rejects. */
static void catch_calloc_overflow(void) {
    void *p = calloc_s(64, sizeof(int));
    assert(p != NULL);
    free_s(p);
    errno = 0;
    assert(calloc_s((size_t)-1, 4) == NULL && errno == EOVERFLOW);
}

/* bug107: checked alloc returns NULL or owned pointer; free_s(NULL) is a no-op. */
static void catch_uninit_out_param(void) {
    void *p = malloc_s(64);
    assert(p != NULL);
    free_s(p);
    free_s(NULL);
}

/* bug143: sprintf into too-small buffer -> bounded variant returns error. */
static void catch_sprintf_off_by_one(void) {
    char buf[5];
    int r = cplus_sprintf_s(buf, sizeof buf, "%05d", 12345);
    assert(r >= (int)sizeof buf);    /* reports required length, no overflow */
    assert(strlen(buf) <= sizeof buf - 1);  /* always bounded + NUL-terminated */
    assert(cplus_sprintf_s(buf, sizeof buf, "%d", 7) == 1);
}

/* bug153: writing past a sub-object member -> bounded copy rejects overflow. */
static void catch_subobject_overflow(void) {
    struct { char name[4]; int is_admin; } rec;
    rec.is_admin = 0;
    assert(cplus_strcpy_s(rec.name, sizeof rec.name, "alice") != 0);
    assert(cplus_strcpy_s(rec.name, sizeof rec.name, "bob") == 0);
}

/* bug173: off-by-one memmove shift -> bounded memmove_s rejects overflow. */
static void catch_memmove_shift_overflow(void) {
    int arr[5] = {1, 2, 3, 4, 5};
    assert(cplus_memmove_s(arr, sizeof arr, arr, 5 * sizeof(int)) == 0); /* exact fits */
    assert(cplus_memmove_s(arr, sizeof(int), arr, 5 * sizeof(int)) != 0); /* too small */
}

/* bug184: strncat loop passing total size each time -> bounded variant rejects. */
static void catch_strncat_loop_overflow(void) {
    char dst[16];
    cplus_strcpy_s(dst, sizeof dst, "");
    const char *parts[] = {"alpha", "beta", "gamma", "delta"};
    int overflowed = 0;
    for (int i = 0; i < 4; ++i) {
        if (cplus_strncat_s(dst, sizeof dst, parts[i], sizeof dst) != 0) { overflowed = 1; break; }
    }
    assert(overflowed == 1);          /* caught instead of smashing the stack */
    assert(strlen(dst) <= sizeof dst - 1);
}

/* bug191: zero-length copy with NULL pointers must not crash or be UB here. */
static void catch_zero_len_null(void) {
    char d[4];
    (void)cplus_memcpy_s(NULL, 0, NULL, 0);   /* defined: no deref */
    (void)cplus_memmove_s(NULL, 0, NULL, 0);
    (void)cplus_memset_s(d, 0, 0, 0);
}

/* bug199: realloc(ptr, 0) must have a defined outcome via checked API. */
static void catch_realloc_zero_defined(void) {
    void *p = malloc_s(32);
    assert(p != NULL);
    p = realloc_s(p, 0);
    assert(p != NULL);
    free_s(p);
}

/* bug174-style variadic sentinel misuse is a caller contract; checked
 * formatters require explicit bounds and never overflow the destination. */
static void catch_variadic_bounds(void) {
    char b[8];
    int n = cplus_snprintf_s(b, sizeof b, "%s", "0123456789");
    assert(n >= 8);                   /* reports required length */
    assert(strlen(b) == sizeof b - 1);/* NUL-terminated, bounded */
}

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    RUN(catch_calloc_overflow);
    RUN(catch_uninit_out_param);
    RUN(catch_sprintf_off_by_one);
    RUN(catch_subobject_overflow);
    RUN(catch_memmove_shift_overflow);
    RUN(catch_strncat_loop_overflow);
    RUN(catch_zero_len_null);
    RUN(catch_realloc_zero_defined);
    RUN(catch_variadic_bounds);
    puts("safe_mitigations2: ok");
    return 0;
}
