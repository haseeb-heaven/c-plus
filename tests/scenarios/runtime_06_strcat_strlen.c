/* Runtime scenarios 06: ordinary strcat / strncat / strlen / strnlen.
 *
 * Standalone C99.  The ORDINARY C spellings are used throughout: including
 * stdlib/safe/safe_c.h remaps strcat/strncat (and malloc/free/realloc/strcpy)
 * onto the capacity-checked wrappers without changing call sites.  strlen and
 * strnlen remain the host functions; the checked strnlen_s entry point is
 * exercised explicitly where noted.
 *
 * No scenario invokes raw strlen on an unterminated buffer: the only
 * unterminated buffers here are read with the bounded strnlen/strnlen_s API.
 * The SafeCRT violation trap is disabled so refusals are observable as NULL /
 * errno results instead of terminating the process.
 *
 * Observed contract of a refused append (ESNOSPC -> errno ERANGE): safeclib
 * clears the destination (dest[0] == '\0'), it does NOT keep the old prefix.
 * Those assertions below document the actual behaviour rather than assuming
 * the destination is untouched.
 */
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L

#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_runtime.h"
#include "stdlib/safe/safe_log.h"
#include "stdlib/safe/safe_stats.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* File-scope destination for the global-buffer scenario, with a canary. */
static char g_sc06_dest[8] = "abc";
static char g_sc06_canary[4] = "XYZ";

/* ---- strcat (15 scenarios) --------------------------------------------- */

/* 1. Exact fit: prefix + suffix + NUL exactly fills the destination. */
static void sc_06_strcat_exact_fit(void) {
    char d[8] = "abc";
    char *r = strcat(d, "defg"); /* 3 + 4 + 1 == 8 */
    assert(r == d);
    assert(strcmp(d, "abcdefg") == 0);
    assert(strlen(d) == 7);
}

/* 2. One byte too long: refused with ERANGE, destination is cleared. */
static void sc_06_strcat_one_too_long_refused(void) {
    char d[8] = "abcd";
    errno = 0;
    char *r = strcat(d, "efgh"); /* 4 + 4 + 1 == 9 > 8 */
    assert(r == NULL);
    assert(errno == ERANGE);
    assert(d[0] == '\0');
}

/* 3. Empty destination: append behaves like a checked copy. */
static void sc_06_strcat_empty_dest(void) {
    char d[8] = "";
    char *r = strcat(d, "xyz");
    assert(r == d);
    assert(strcmp(d, "xyz") == 0);
}

/* 4. Empty source: destination is unchanged and the pointer is returned. */
static void sc_06_strcat_empty_src(void) {
    char d[8] = "abc";
    char *r = strcat(d, "");
    assert(r == d);
    assert(strcmp(d, "abc") == 0);
}

/* 5. NULL destination: EINVAL, no crash (violation trap is off). */
static void sc_06_strcat_null_dest(void) {
    const char src[] = "x";
    errno = 0;
    char *r = strcat(NULL, src);
    assert(r == NULL);
    assert(errno == EINVAL);
}

/* 6. NULL source: EINVAL and the destination keeps its old contents. */
static void sc_06_strcat_null_src(void) {
    char d[8] = "ab";
    errno = 0;
    char *r = strcat(d, NULL);
    assert(r == NULL);
    assert(errno == EINVAL);
    assert(strcmp(d, "ab") == 0);
}

/* 7. Stack destination: __builtin_object_size proves the capacity. */
static void sc_06_strcat_stack_dest(void) {
    char d[12] = "stack:";
    char *r = strcat(d, "ok");
    assert(r == d);
    assert(strcmp(d, "stack:ok") == 0);
}

/* 8. Global destination: object size known, adjacent global canary intact. */
static void sc_06_strcat_global_dest(void) {
    strcpy(g_sc06_dest, "abc");
    char *r = strcat(g_sc06_dest, "defg");
    assert(r == g_sc06_dest);
    assert(strcmp(g_sc06_dest, "abcdefg") == 0);
    assert(strcmp(g_sc06_canary, "XYZ") == 0);
}

/* 9. Heap destination: tracked allocation size bounds the append. */
static void sc_06_strcat_heap_dest(void) {
    char *p = (char *)malloc(16);
    assert(p != NULL);
    strcpy(p, "heap:");
    assert(strcat(p, "ok") == p);
    assert(strcmp(p, "heap:ok") == 0);
    free(p);
}

/* 10. Struct member destination: canary members before/after are untouched
 *     on both the successful and the refused append. */
static void sc_06_strcat_struct_member_canary(void) {
    struct {
        char pre[4];
        char d[8];
        char post[4];
    } s;
    strcpy(s.pre, "pre");
    strcpy(s.post, "pos");
    strcpy(s.d, "ab");

    assert(strcat(s.d, "cdef") == s.d);
    assert(strcmp(s.d, "abcdef") == 0);
    assert(strcmp(s.pre, "pre") == 0);
    assert(strcmp(s.post, "pos") == 0);

    strcpy(s.d, "abcd");
    errno = 0;
    assert(strcat(s.d, "efgh") == NULL);
    assert(errno == ERANGE);
    assert(s.d[0] == '\0');
    assert(strcmp(s.pre, "pre") == 0);
    assert(strcmp(s.post, "pos") == 0);
}

/* 11. Repeated in-bounds appends accumulate without drifting out of range. */
static void sc_06_strcat_repeated_in_bounds(void) {
    char d[32] = "";
    int i;
    for (i = 0; i < 8; ++i) {
        assert(strcat(d, "ab") == d);
    }
    assert(strlen(d) == 16);
    assert(strcmp(d, "abababababababab") == 0);
}

/* 12. Append after free: tracked UAF is refused with EFAULT. */
static void sc_06_strcat_after_free(void) {
    char *p = (char *)malloc(16);
    assert(p != NULL);
    strcpy(p, "ab");
    free(p);
    assert(cplus_safe_ptr_was_freed(p) == 1);
    errno = 0;
    assert(strcat(p, "c") == NULL);
    assert(errno == EFAULT);
}

/* 13. Append after realloc: the grown capacity (not the old one) is used. */
static void sc_06_strcat_after_realloc(void) {
    char *p = (char *)malloc(8);
    assert(p != NULL);
    strcpy(p, "ab");
    p = (char *)realloc(p, 16);
    assert(p != NULL);
    assert(cplus_safe_ptr_size(p) == 16);
    assert(strcat(p, "cdefgh") == p);
    assert(strcmp(p, "abcdefgh") == 0);
    free(p);
}

/* 14. Self-concatenation (dest == src): overlap is refused with EINVAL. */
static void sc_06_strcat_self_concat(void) {
    char d[16] = "abc";
    errno = 0;
    assert(strcat(d, d) == NULL);
    assert(errno == EINVAL);
    assert(d[0] == '\0');
}

/* 15. Strict unknown-capacity mode: a stack append with proven capacity
 *     still works, and an oversized append is still refused with ERANGE. */
static void sc_06_strcat_strict_mode(void) {
    char d[8] = "abc";
    cplus_safe_set_unknown_capacity_mode(1);
    assert(strcat(d, "defg") == d);
    assert(strcmp(d, "abcdefg") == 0);

    strcpy(d, "abcd");
    errno = 0;
    assert(strcat(d, "efgh") == NULL);
    assert(errno == ERANGE);
    assert(d[0] == '\0');
    cplus_safe_set_unknown_capacity_mode(0);
}

/* ---- strncat (4 scenarios) --------------------------------------------- */

/* 16. n smaller than src: exactly n characters are appended. */
static void sc_06_strncat_n_smaller_than_src(void) {
    char d[8] = "ab";
    char *r = strncat(d, "cdefgh", 2);
    assert(r == d);
    assert(strcmp(d, "abcd") == 0);
}

/* 17. n larger than remaining capacity: refused with ERANGE. */
static void sc_06_strncat_n_larger_than_capacity_refused(void) {
    char d[8] = "abcd";
    errno = 0;
    char *r = strncat(d, "efghij", 6); /* 4 + 6 + 1 == 11 > 8 */
    assert(r == NULL);
    assert(errno == ERANGE);
    assert(d[0] == '\0');
}

/* 18. n == 0: no-op that still returns the destination pointer. */
static void sc_06_strncat_n_zero(void) {
    char d[8] = "ab";
    errno = 0;
    char *r = strncat(d, "cde", 0);
    assert(r == d);
    assert(strcmp(d, "ab") == 0);
    assert(errno == 0);
}

/* 19. Source shorter than n: append stops at the source NUL, not at n. */
static void sc_06_strncat_source_shorter_than_n(void) {
    char d[8] = "";
    char *r = strncat(d, "xyz", 100);
    assert(r == d);
    assert(strcmp(d, "xyz") == 0);
}

/* ---- strlen / strnlen (11 scenarios) ----------------------------------- */

/* 20. Normal NUL-terminated string. */
static void sc_06_strlen_normal(void) {
    assert(strlen("hello") == 5);
    assert(strlen("abcdefghij") == 10);
}

/* 21. Empty string. */
static void sc_06_strlen_empty(void) {
    assert(strlen("") == 0);
}

/* 22. Embedded NUL terminates the counted length. */
static void sc_06_strlen_embedded_nul(void) {
    char s[] = {'a', 'b', '\0', 'c', 'd', '\0'};
    assert(strlen(s) == 2);
}

/* 23. High-bit bytes are ordinary non-NUL bytes. */
static void sc_06_strlen_high_bit_bytes(void) {
    char s[] = {(char)0x80, (char)0xFF, 'a', '\0'};
    assert(strlen(s) == 3);
}

/* 24. Long string (512 characters) is counted exactly. */
static void sc_06_strlen_long_string(void) {
    char s[513];
    size_t i;
    for (i = 0; i < 512; ++i) {
        s[i] = 'a';
    }
    s[512] = '\0';
    assert(strlen(s) == 512);
}

/* 25. Unterminated bounded buffer: only strnlen / strnlen_s may read it. */
static void sc_06_strnlen_unterminated_bounded(void) {
    char b[8];
    size_t i;
    for (i = 0; i < 8; ++i) {
        b[i] = 'x';
    }
    assert(strnlen(b, 8) == 8);
    assert(cplus_strnlen_s(b, 8) == 8);
}

/* 26. n == 0 yields 0 for both bounded APIs. */
static void sc_06_strnlen_n_zero(void) {
    assert(strnlen("abc", 0) == 0);
    assert(cplus_strnlen_s("abc", 0) == 0);
}

/* 27. n exactly equals the string length. */
static void sc_06_strnlen_n_exact(void) {
    char s[] = "abc";
    assert(strnlen(s, 3) == 3);
    assert(cplus_strnlen_s(s, 3) == 3);
}

/* 28. n larger than the buffer is safe: both APIs stop at the in-buffer NUL. */
static void sc_06_strnlen_n_larger_than_buffer(void) {
    char b[4] = {'a', 'b', 'c', '\0'};
    assert(strnlen(b, 100) == 3);
    assert(cplus_strnlen_s(b, 100) == 3);
}

/* 29. NULL handling via the checked API: defined 0 result, no raw call. */
static void sc_06_strnlen_null_checked_api(void) {
    errno = 0;
    assert(cplus_strnlen_s(NULL, 8) == 0);
    assert(cplus_strnlen_s("abc", 8) == 3);
}

/* 30. Logging / telemetry parity: the ordinary strcat spelling and the
 *     explicit cplus_strcat_s call each log one TRACE "enter" and record one
 *     stat; a refused append adds an ERROR log and a failure stat. */
static void sc_06_logging_parity(void) {
    char d1[16] = "ab";
    char d2[16] = "ab";
    char d3[8] = "abcd";
    char line[256];
    int enters = 0;
    int errors = 0;
    cplus_safe_stat st;
    FILE *cap = tmpfile();
    int saved;

    assert(cap != NULL);
    saved = dup(STDERR_FILENO);
    assert(saved >= 0);

    cplus_safe_stats_reset();
    fflush(stderr);
    assert(dup2(fileno(cap), STDERR_FILENO) >= 0);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_TRACE);

    assert(strcat(d1, "c") == d1);                     /* ordinary spelling */
    assert(cplus_strcat_s(d2, sizeof d2, "c") == 0);   /* checked spelling  */
    errno = 0;
    assert(strcat(d3, "efgh") == NULL);                 /* refusal -> ERROR  */
    assert(errno == ERANGE);

    fflush(stderr);
    assert(dup2(saved, STDERR_FILENO) >= 0);
    close(saved);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);

    assert(strcmp(d1, "abc") == 0);
    assert(strcmp(d2, "abc") == 0);
    assert(cplus_safe_stats_get("strcat_s", &st) == 1);
    assert(st.calls == 3 && st.ok == 2 && st.fail == 1);

    rewind(cap);
    while (fgets(line, sizeof line, cap) != NULL) {
        if (strstr(line, "[SAFECRT][5] strcat_s: enter") != NULL) {
            ++enters;
        }
        if (strstr(line, "[SAFECRT][1] strcat_s: constraint/error") != NULL) {
            ++errors;
        }
    }
    fclose(cap);
    assert(enters == 3);
    assert(errors == 1);
}

typedef void (*sc06_fn)(void);

static const sc06_fn sc06_all[] = {
    sc_06_strcat_exact_fit,
    sc_06_strcat_one_too_long_refused,
    sc_06_strcat_empty_dest,
    sc_06_strcat_empty_src,
    sc_06_strcat_null_dest,
    sc_06_strcat_null_src,
    sc_06_strcat_stack_dest,
    sc_06_strcat_global_dest,
    sc_06_strcat_heap_dest,
    sc_06_strcat_struct_member_canary,
    sc_06_strcat_repeated_in_bounds,
    sc_06_strcat_after_free,
    sc_06_strcat_after_realloc,
    sc_06_strcat_self_concat,
    sc_06_strcat_strict_mode,
    sc_06_strncat_n_smaller_than_src,
    sc_06_strncat_n_larger_than_capacity_refused,
    sc_06_strncat_n_zero,
    sc_06_strncat_source_shorter_than_n,
    sc_06_strlen_normal,
    sc_06_strlen_empty,
    sc_06_strlen_embedded_nul,
    sc_06_strlen_high_bit_bytes,
    sc_06_strlen_long_string,
    sc_06_strnlen_unterminated_bounded,
    sc_06_strnlen_n_zero,
    sc_06_strnlen_n_exact,
    sc_06_strnlen_n_larger_than_buffer,
    sc_06_strnlen_null_checked_api,
    sc_06_logging_parity
};

int main(void) {
    size_t i, n = sizeof sc06_all / sizeof sc06_all[0];

    cplus_runtime_set_violation_trap(0);
    setvbuf(stdout, NULL, _IONBF, 0);

    for (i = 0; i < n; ++i) {
        sc06_all[i]();
    }

    printf("runtime_strcat_strlen: %lu scenarios ok\n", (unsigned long)n);
    return 0;
}
