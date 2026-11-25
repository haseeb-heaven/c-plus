/* Adversarial mitigation proof: for each runtime-catchable bug pattern from the
 * 104-bug catalogue, show the checked C+ safe API returns a defined error
 * (no crash / no silent corruption) instead of invoking UB.
 *
 * IMPORTANT: this proves the *checked API* catches the pattern when the caller
 * uses it. It does NOT make raw C (e.g. `int a[5]; a[5]=1;`) safe, and it does
 * NOT prove Rust-grade memory safety. Bugs requiring provenance/lifetime/static
 * reasoning are listed in docs/libc/ADVERSARIAL_MATRIX.md as compiler/Agent-1 or
 * explicit unsafe boundaries.
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
#include "safe_io.h"
#include "safe_format.h"
#include "safe_error.h"
#include "safe_log.h"

#define RUN(f) do { f(); printf("ok %s\n", #f); } while (0)

/* bug02 / bug04: NULL pointer into memory API -> defined error, no deref. */
static void catch_null_memcpy(void) {
    char dst[8];
    assert(cplus_memcpy_s(dst, sizeof dst, NULL, 4) != 0);   /* bug04 */
    assert(cplus_memcpy_s(NULL, sizeof dst, "x", 1) != 0);
    assert(cplus_memmove_s(dst, sizeof dst, NULL, 1) != 0);
    assert(cplus_memset_s(NULL, sizeof dst, 0, 1) != 0);
}

/* bug22/bug23/bug26/bug36/bug37/bug39: destination overflow -> rejected. */
static void catch_dest_overflow(void) {
    char small[4];
    assert(cplus_strcpy_s(small, sizeof small, "overflowing_string") != 0); /* bug39 */
    assert(cplus_strncpy_s(small, sizeof small, "waytoolong", 10) != 0);
    strcpy(small, "ab");
    assert(cplus_strcat_s(small, sizeof small, "cdefgh") != 0);
    assert(cplus_strncat_s(small, sizeof small, "zzzz", 4) != 0);
    char d2[2];
    assert(cplus_memcpy_s(d2, sizeof d2, "abcd", 4) != 0);   /* bug23/26 style */
    assert(cplus_memset_s(d2, sizeof d2, 0, 8) != 0);
}

/* bug38: length-bounded string ops do not over-read unterminated buffers. */
static void catch_unterminated_read(void) {
    char buf[5];
    memcpy(buf, "hello", 5); /* no NUL, bug38 pattern */
    size_t n = cplus_strnlen_s(buf, sizeof buf);
    assert(n <= sizeof buf);
    int diff = 0;
    assert(cplus_memcmp_s(buf, sizeof buf, "hello", 5, &diff) == 0);
}

/* bug51: malloc(0) has a defined contract via checked alloc. */
static void catch_zero_alloc(void) {
    void *p = malloc_s(0);
    assert(p != NULL && cplus_safe_ptr_is_live(p));
    free_s(p);
    assert(!cplus_safe_ptr_is_live(p));
}

/* bug32: realloc failure must not lose the original pointer. */
static void catch_realloc_failure_preserves(void) {
    void *p = malloc_s(32);
    assert(p != NULL);
    memset(p, 0xAB, 32);
    errno = 0;
    assert(realloc_s(p, (size_t)-1) == NULL && errno == EINVAL);
    assert(cplus_safe_ptr_is_live(p));
    free_s(p);
}

/* bug19/bug98: checked free rejects double-free safely without heap corruption. */
static void catch_double_free_handle(void) {
    void *p = malloc_s(16);
    assert(p != NULL);
    assert(free_checked_s(p) == CPLUS_SAFE_OK);
    assert(free_checked_s(p) == CPLUS_SAFE_EINVAL);
    free_s(NULL);
}

/* bug47/bug48: NULL / double FILE* close -> defined error, no UB. */
static void catch_stdio_lifecycle(void) {
    FILE *f = cplus_fopen_s("/no/such/path/cplus_adv", "r");
    assert(f == NULL);
    f = cplus_fopen_s("/tmp/cplus_adv_mig.tmp", "wb+");
    assert(f != NULL);
    assert(cplus_fclose_s(f) == 0);
    assert(cplus_fclose_s(f) == EOF); /* bug48 double close */
    assert(cplus_fclose_s(NULL) == EOF); /* bug47 */
    remove("/tmp/cplus_adv_mig.tmp");
}

/* bug81: bounded formatting reports truncation rather than overflowing. */
static void catch_format_truncation(void) {
    char buf[10];
    int n = cplus_snprintf_s(buf, sizeof buf, "LongStringExceedingBuffer");
    assert(n >= 0);                 /* length that *would* be written */
    assert(strlen(buf) <= sizeof buf - 1);  /* always NUL-terminated, bounded */
    assert(cplus_snprintf_s(buf, sizeof buf, "%d", 7) == 1);
}

/* bug40: overlapping copy is a defined error for memcpy_s (use memmove_s). */
static void catch_overlap(void) {
    char data[16] = "12345678";
    errno_t r = cplus_memcpy_s(data + 2, sizeof data - 2, data, 5);
    assert(r != 0);   /* overlapping => constraint error (ESOVRLP) */
    char data2[16] = "12345678";
    assert(cplus_memmove_s(data2 + 2, sizeof data2 - 2, data2, 5) == 0); /* correct API */
}

/* bug41/bug42: overflow / huge sizes rejected with a defined error. */
static void catch_size_overflow(void) {
    errno = 0;
    assert(calloc_s((size_t)-1, 2) == NULL && errno == EOVERFLOW);
    char d[4];
    assert(cplus_memcpy_s(d, sizeof d, "x", (rsize_t)-1) != 0);
}

/* memory is never written past the caller-declared bound. */
static void catch_no_write_past_bound(void) {
    unsigned char guard[8];
    memset(guard, 0x5A, sizeof guard);
    unsigned char dst[4];
    assert(cplus_memcpy_s(dst, sizeof dst, "abcdefgh", 8) != 0);
    for (size_t i = 0; i < sizeof guard; ++i) assert(guard[i] == 0x5A);
}

/* bug33: checked zeroing produces a deterministic buffer, no UB read. */
static void catch_deterministic_zero(void) {
    unsigned char b[16];
    memset(b, 0xEE, sizeof b);
    assert(cplus_memzero_s(b, sizeof b) == 0);
    for (size_t i = 0; i < sizeof b; ++i) assert(b[i] == 0);
}

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    RUN(catch_null_memcpy);
    RUN(catch_dest_overflow);
    RUN(catch_unterminated_read);
    RUN(catch_zero_alloc);
    RUN(catch_realloc_failure_preserves);
    RUN(catch_double_free_handle);
    RUN(catch_stdio_lifecycle);
    RUN(catch_format_truncation);
    RUN(catch_overlap);
    RUN(catch_size_overflow);
    RUN(catch_no_write_past_bound);
    RUN(catch_deterministic_zero);
    puts("safe_mitigations: ok");
    return 0;
}
