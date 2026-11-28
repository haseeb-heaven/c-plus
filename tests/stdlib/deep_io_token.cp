/* Deep boundary/adversarial regression: stdlib/io.hp token and numeric
 * conversion boundaries (scenarios 3 and 5).
 *
 * Classification:
 *   EXPECT-SUCCESS      - conversion must succeed with the expected value.
 *   RECOVERABLE-FAILURE - STDLIB_FAIL with an exact exit code/category; the
 *                         process must not terminate.
 *   FATAL-SAFETY        - none: every malformed/overflowing token is reported
 *                         through `error`, never through cplus_fatal.
 *
 * Prints "<tag> <0|1>" per check plus "ALL_OK <0|1>"; exits 0 only if every
 * check passes.  Exact expected stdout is in docs/stdlib/DEEP_IO_CONTRACT.md.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/io.hp"

int main(void) {
    error err = error();
    char tok[8];
    int i = 0;
    long l = 0;
    float f = 0;
    double d = 0;
    int st;
    int all = 1;
    int ok;

    /* ---- Scenario 3: overlong tokens ---------------------------------- */

    /* RECOVERABLE-FAILURE: a 70-digit token (> 64-byte internal token) must
     * be rejected as RANGE/BOUNDS rather than silently truncated. */
    st = read_int(&i, &err);
    ok = (st == STDLIB_FAIL && err.code() == CPLUS_EXIT_BOUNDS &&
          err.category() == STDLIB_CATEGORY_RANGE);
    all = all && ok;
    print("s3_overlong", ok);

    /* EXPECT-SUCCESS: the overlong token was fully drained; the next read is
     * not shifted onto its tail. */
    st = read_int(&i, &err);
    ok = (st == STDLIB_OK && i == 42);
    all = all && ok;
    print("s3_aligned", ok);

    /* Direct stdlib_io_token(): truncation is signalled by return code 2,
     * never silent.  The buffer is NUL-terminated and holds at most size-1
     * bytes; the stream is drained to the delimiter. */
    memset(tok, 'Q', sizeof(tok));
    st = stdlib_io_token(tok, sizeof(tok));
    ok = (st == 2 && strlen(tok) == 7 && tok[0] == '1' && tok[6] == '7' &&
          tok[7] == '\0');
    all = all && ok;
    print("s3_direct", ok);

    st = read_int(&i, &err);
    ok = (st == STDLIB_OK && i == 99);
    all = all && ok;
    print("s3_after_direct", ok);

    /* ---- Scenario 5: numeric edges ------------------------------------ */

    /* RECOVERABLE-FAILURE: read_int overflow, underflow and malformed text. */
    st = read_int(&i, &err);
    ok = (st == STDLIB_FAIL && err.code() == CPLUS_EXIT_BOUNDS &&
          err.category() == STDLIB_CATEGORY_RANGE);
    all = all && ok;
    print("s5_i_over", ok);

    st = read_int(&i, &err);
    ok = (st == STDLIB_FAIL && err.code() == CPLUS_EXIT_BOUNDS &&
          err.category() == STDLIB_CATEGORY_RANGE);
    all = all && ok;
    print("s5_i_under", ok);

    st = read_int(&i, &err);
    ok = (st == STDLIB_FAIL && err.code() == CPLUS_EXIT_UNSUPPORTED &&
          err.category() == STDLIB_CATEGORY_INVALID);
    all = all && ok;
    print("s5_i_bad", ok);

    /* The readers allocate nothing: deterministic allocation-failure
     * injection from error.hp must not affect a successful conversion. */
    stdlib_alloc_fail_after(0);
    st = read_int(&i, &err);
    stdlib_alloc_fail_reset();
    ok = (st == STDLIB_OK && err.code() == CPLUS_EXIT_OK && i == 5);
    all = all && ok;
    print("s5_i_alloc_fail", ok);

    /* RECOVERABLE-FAILURE: read_long overflow / underflow. */
    st = read_long(&l, &err);
    ok = (st == STDLIB_FAIL && err.code() == CPLUS_EXIT_BOUNDS &&
          err.category() == STDLIB_CATEGORY_RANGE);
    all = all && ok;
    print("s5_l_over", ok);

    st = read_long(&l, &err);
    ok = (st == STDLIB_FAIL && err.code() == CPLUS_EXIT_BOUNDS &&
          err.category() == STDLIB_CATEGORY_RANGE);
    all = all && ok;
    print("s5_l_under", ok);

    /* RECOVERABLE-FAILURE: clear overflow-to-infinity and underflow-to-zero
     * both set ERANGE in strtof/strtod and are rejected with RANGE. */
    st = read_float(&f, &err);
    ok = (st == STDLIB_FAIL && err.code() == CPLUS_EXIT_BOUNDS &&
          err.category() == STDLIB_CATEGORY_RANGE);
    all = all && ok;
    print("s5_f_under", ok);

    /* Subnormal classification is libc-dependent: accept either a finite
     * value or a RANGE failure, but never a different category or a trap. */
    st = read_float(&f, &err);
    ok = ((st == STDLIB_OK && isfinite(f)) ||
          (st == STDLIB_FAIL && err.code() == CPLUS_EXIT_BOUNDS &&
           err.category() == STDLIB_CATEGORY_RANGE));
    all = all && ok;
    print("s5_f_sub", ok);

    st = read_float(&f, &err);
    ok = (st == STDLIB_FAIL && err.code() == CPLUS_EXIT_BOUNDS &&
          err.category() == STDLIB_CATEGORY_RANGE);
    all = all && ok;
    print("s5_f_over", ok);

    st = read_float(&f, &err);
    ok = (st == STDLIB_FAIL && err.code() == CPLUS_EXIT_UNSUPPORTED &&
          err.category() == STDLIB_CATEGORY_INVALID);
    all = all && ok;
    print("s5_f_bad", ok);

    st = read_double(&d, &err);
    ok = (st == STDLIB_FAIL && err.code() == CPLUS_EXIT_BOUNDS &&
          err.category() == STDLIB_CATEGORY_RANGE);
    all = all && ok;
    print("s5_d_under", ok);

    st = read_double(&d, &err);
    ok = ((st == STDLIB_OK && isfinite(d)) ||
          (st == STDLIB_FAIL && err.code() == CPLUS_EXIT_BOUNDS &&
           err.category() == STDLIB_CATEGORY_RANGE));
    all = all && ok;
    print("s5_d_sub", ok);

    st = read_double(&d, &err);
    ok = (st == STDLIB_FAIL && err.code() == CPLUS_EXIT_BOUNDS &&
          err.category() == STDLIB_CATEGORY_RANGE);
    all = all && ok;
    print("s5_d_over", ok);

    /* Non-finite spellings (inf/nan) are rejected even without ERANGE. */
    st = read_double(&d, &err);
    ok = (st == STDLIB_FAIL && err.code() == CPLUS_EXIT_BOUNDS &&
          err.category() == STDLIB_CATEGORY_RANGE);
    all = all && ok;
    print("s5_d_inf", ok);

    st = read_double(&d, &err);
    ok = (st == STDLIB_FAIL && err.code() == CPLUS_EXIT_UNSUPPORTED &&
          err.category() == STDLIB_CATEGORY_INVALID);
    all = all && ok;
    print("s5_d_bad", ok);

    /* EXPECT-SUCCESS: EOF is a clean STDLIB_EOF with a clear error record,
     * even after a long chain of failures (no stale state). */
    st = read_int(&i, &err);
    ok = (st == STDLIB_EOF && err.code() == CPLUS_EXIT_OK &&
          err.category() == STDLIB_CATEGORY_OK);
    all = all && ok;
    print("s5_eof", ok);

    print("ALL_OK", all);
    return all ? 0 : 1;
}
