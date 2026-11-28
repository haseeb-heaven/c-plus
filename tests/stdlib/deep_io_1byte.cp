/* Deep boundary/adversarial regression: stdlib/io.hp byte-level and
 * buffer-size boundaries (scenarios 1, 2 and 4).
 *
 * Classification of every check below:
 *   EXPECT-SUCCESS      - operation must succeed with a defined value.
 *   RECOVERABLE-FAILURE - STDLIB_FAIL with an exact exit code/category; the
 *                         process must NOT terminate (no cplus_fatal).
 *   FATAL-SAFETY        - none in this file: every case is recoverable.  The
 *                         fatal-safety class is exercised where a violation
 *                         would be a runtime trap; io.hp reports all of these
 *                         conditions through `error` instead (see contract).
 *
 * The program prints "<tag> <0|1>" per check and a final "ALL_OK <0|1>".
 * It exits 0 only when all checks pass, so the harness gate is the exit code
 * (this suite intentionally ships no .expected file for these tests; the
 * exact expected stdout is recorded in docs/stdlib/DEEP_IO_CONTRACT.md).
 */
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/io.hp"

int main(void) {
    error err = error();
    char b[17];   /* capacity = 16 characters */
    char tiny[2]; /* 1-byte destination + a one-past sentinel */
    int st;
    int all = 1;
    int ok;

    /* ---- Scenario 1: read_line() into a 1-byte destination ------------- */

    /* RECOVERABLE-FAILURE: non-empty line is refused with code 101 (BOUNDS)
     * and category BOUNDS; only the NUL terminator is written inside the
     * destination, the one-past sentinel is untouched, and the line is
     * drained so the stream stays aligned. */
    tiny[0] = 'X';
    tiny[1] = 'Y';
    st = read_line(tiny, 1, &err);
    ok = (st == STDLIB_FAIL && err.code() == CPLUS_EXIT_BOUNDS &&
          err.category() == STDLIB_CATEGORY_BOUNDS && tiny[0] == '\0' &&
          tiny[1] == 'Y');
    all = all && ok;
    print("s1_nonempty", ok);

    /* EXPECT-SUCCESS (documented backward compatibility): an empty line fits
     * in a 1-byte destination and yields an empty string. */
    tiny[0] = 'X';
    tiny[1] = 'Y';
    st = read_line(tiny, 1, &err);
    ok = (st == STDLIB_OK && err.code() == CPLUS_EXIT_OK &&
          err.category() == STDLIB_CATEGORY_OK && tiny[0] == '\0' &&
          tiny[1] == 'Y');
    all = all && ok;
    print("s1_empty", ok);

    /* RECOVERABLE-FAILURE: null destination and zero size are refused before
     * any stream access (category INVALID). */
    st = read_line(NULL, 1, &err);
    ok = (st == STDLIB_FAIL && err.code() == CPLUS_EXIT_IO &&
          err.category() == STDLIB_CATEGORY_INVALID);
    all = all && ok;
    print("s1_null", ok);

    st = read_line(tiny, 0, &err);
    ok = (st == STDLIB_FAIL && err.code() == CPLUS_EXIT_IO &&
          err.category() == STDLIB_CATEGORY_INVALID);
    all = all && ok;
    print("s1_zero", ok);

    /* ---- Scenario 2: buffer size > INT_MAX ----------------------------- */

    /* RECOVERABLE-FAILURE: the request is rejected with code 101 (BOUNDS)
     * BEFORE fgets() is called; the destination is untouched. */
    b[0] = 'Z';
    st = read_line(b, (size_t)INT_MAX + 1u, &err);
    ok = (st == STDLIB_FAIL && err.code() == CPLUS_EXIT_BOUNDS &&
          err.category() == STDLIB_CATEGORY_BOUNDS && b[0] == 'Z');
    all = all && ok;
    print("s2_reject", ok);

    /* EXPECT-SUCCESS: because nothing was consumed, the next read sees the
     * real first line. */
    st = read_line(b, sizeof(b), &err);
    ok = (st == STDLIB_OK && strcmp(b, "first") == 0);
    all = all && ok;
    print("s2_aligned", ok);

    /* ---- Scenario 4: exact-fit / overlong, followed by another read ----- */

    /* EXPECT-SUCCESS: 16 chars + newline into a 17-byte buffer is an exact
     * fit.  Regression: this used to be misreported as "too long". */
    st = read_line(b, sizeof(b), &err);
    ok = (st == STDLIB_OK && err.code() == CPLUS_EXIT_OK &&
          strcmp(b, "0123456789abcdef") == 0);
    all = all && ok;
    print("s4_exact", ok);

    st = read_line(b, sizeof(b), &err);
    ok = (st == STDLIB_OK && strcmp(b, "after_exact") == 0);
    all = all && ok;
    print("s4_after_exact", ok);

    /* RECOVERABLE-FAILURE: 17 chars into a 17-byte buffer is genuinely too
     * long; it fails with BOUNDS and the remainder is drained. */
    st = read_line(b, sizeof(b), &err);
    ok = (st == STDLIB_FAIL && err.code() == CPLUS_EXIT_BOUNDS &&
          err.category() == STDLIB_CATEGORY_BOUNDS);
    all = all && ok;
    print("s4_over", ok);

    st = read_line(b, sizeof(b), &err);
    ok = (st == STDLIB_OK && strcmp(b, "after_over") == 0);
    all = all && ok;
    print("s4_after_over", ok);

    /* EXPECT-SUCCESS: CRLF line of 15 chars; CR lands exactly at the fgets
     * width boundary.  Regression: the leftover LF used to produce an extra
     * empty line on the following read. */
    st = read_line(b, sizeof(b), &err);
    ok = (st == STDLIB_OK && strcmp(b, "0123456789abcde") == 0);
    all = all && ok;
    print("s4_crlf", ok);

    st = read_line(b, sizeof(b), &err);
    ok = (st == STDLIB_OK && strcmp(b, "after_crlf") == 0);
    all = all && ok;
    print("s4_after_crlf", ok);

    /* EXPECT-SUCCESS then EOF with a clear error record: no stale state. */
    st = read_line(b, sizeof(b), &err);
    ok = (st == STDLIB_OK && strcmp(b, "last") == 0);
    all = all && ok;
    print("s4_last", ok);

    st = read_line(b, sizeof(b), &err);
    ok = (st == STDLIB_EOF && err.code() == CPLUS_EXIT_OK &&
          err.category() == STDLIB_CATEGORY_OK);
    all = all && ok;
    print("s4_eof", ok);

    /* EXPECT-SUCCESS (scenario 1 at end of stream): a 1-byte destination at
     * EOF returns STDLIB_EOF with a clear error record and writes nothing. */
    tiny[0] = 'X';
    tiny[1] = 'Y';
    st = read_line(tiny, 1, &err);
    ok = (st == STDLIB_EOF && err.code() == CPLUS_EXIT_OK && tiny[0] == 'X' &&
          tiny[1] == 'Y');
    all = all && ok;
    print("s1_eof", ok);

    print("ALL_OK", all);
    return all ? 0 : 1;
}
