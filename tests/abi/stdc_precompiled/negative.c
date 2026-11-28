/* Negative control: the caller-side checked layer (force-included
 * stdlib/safe/safe_c.h) must REFUSE a 64-byte copy into char[8], while the
 * SAME unchanged precompiled C library is still called successfully for valid
 * data in this very process.
 *
 * Expected behaviour:
 *   - stdout proves the saved library was called (version/mul/sum) and that a
 *     valid in-bounds copy succeeds;
 *   - memcpy(small, src, 64) is macro-mapped to cplus_memcpy_s_auto, which sees
 *     __builtin_object_size(small, 1) == 8, refuses the write, prints
 *     "[CPLUS-SAFE] memcpy_s: buffer overflow refused ... ESNOSPC 406" on
 *     stderr, and (default violation trap) terminates with exit code 70;
 *   - the trailing "UNEXPECTED" line must NOT be printed.
 */
#include <stdio.h>
#include <string.h>

extern int stdc_legacy_version(void);
extern int stdc_legacy_mul(int, int);
extern int stdc_legacy_sum(const unsigned char *, size_t);

int main(void) {
    char small[8];
    /* Exactly 64 characters (no NUL needed for an exact-size initializer). */
    const char src[64] =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

    /* The saved, unchanged precompiled library is called as usual. */
    printf("negative: lib_version=%d lib_mul=%d lib_sum=%d\n",
           stdc_legacy_version(), stdc_legacy_mul(7, 6),
           stdc_legacy_sum((const unsigned char *)"abc", 3));

    /* A valid, in-bounds copy through the checked ordinary name succeeds. */
    memcpy(small, "OK", 3);
    printf("negative: valid_copy=ok small0=%c\n", small[0]);
    fflush(stdout);

    /* 64 bytes into an 8-byte local buffer: must be refused, never performed. */
    memcpy(small, src, 64);

    /* Reaching here means the overflow was NOT refused. */
    printf("negative: UNEXPECTED overflow was not refused\n");
    return 1;
}
