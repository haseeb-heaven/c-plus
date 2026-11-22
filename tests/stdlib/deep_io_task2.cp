/* Deep boundary/adversarial regression: stdlib/io.hp Task-2 uncovered cases.
 *
 * This fixture deliberately covers ONLY the cases that are absent from
 * deep_io_1byte.cp and deep_io_token.cp (see docs/stdlib/DEEP_IO_TASK2_CONTRACT.md):
 *   1. exact-fit line at PHYSICAL EOF with no terminator        (c1_*)
 *   2. exact-fit line immediately followed by newline at EOF    (c2_*)
 *   3. CR-only terminator at the exact fgets width boundary     (c3_*)
 *   4. out-params byte-for-byte unchanged on FAIL for all four readers (c4_*)
 *   5. valid input after a rejected input for all four readers  (c5_*)
 *   6. EOF before any data for all four numeric readers         (c6_*)
 *
 * Classification:
 *   EXPECT-SUCCESS      - STDLIB_OK with an exact value and a clear error record.
 *   RECOVERABLE-FAILURE - STDLIB_FAIL with an exact code/category; the process
 *                         must NOT terminate (no cplus_fatal).
 *   CLEAN-EOF           - STDLIB_EOF (1) with a clear error record and no write
 *                         to the caller's destination.
 *   FATAL-SAFETY        - none: every condition is reported through `error`.
 *
 * WHY fmemopen(): cases 1 and 2 each require the line under test to sit at a
 * *physical* EOF with a specific trailing byte sequence ("abcd" vs "abcd\n").
 * A single linear stdin fixture cannot end in two different ways, so those two
 * cases use isolated in-memory streams installed as `stdin` via fmemopen and
 * restored afterwards. The shared fixture deep_io_task2.stdin supplies cases
 * 3-6; its natural EOF supplies case 6.
 *
 * Output: one "<tag> <0|1>" per check, then "IO_TASK2 <checks> <failures>".
 * Exit code is 0 only when failures == 0. No .expected file (file ownership is
 * restricted); exact stdout is pinned in the contract document.
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/io.hp"

/* Reuses the three variables declared in main(); introduces no new owned name
 * (C+ RAII lowering tracks owned names per function). */
#define T2_CHECK(tag, expr)    \
    do {                       \
        t2_ok = (expr);        \
        t2_checks++;           \
        if (!t2_ok)            \
            t2_failures++;     \
        print(tag, t2_ok);     \
    } while (0)

int main(void) {
    error t2_error = error();
    int t2_status;
    int t2_ok;
    int t2_checks = 0;
    int t2_failures = 0;

    /* Distinctive sentinels plus immutable snapshots for byte-for-byte checks. */
    int t2_i_val = 0x12345678;
    int t2_i_snap = 0x12345678;
    long t2_l_val = 0x1122334455667788L;
    long t2_l_snap = 0x1122334455667788L;
    float t2_f_val = -2468.125f;
    float t2_f_snap = -2468.125f;
    double t2_d_val = 13579.25;
    double t2_d_snap = 13579.25;

    char t2_line[5];
    char t2_line_snap[5];
    char t2_c1_src[] = "abcd";
    char t2_c2_src[] = "abcd\n";
    FILE *t2_orig_stdin;

    /* ---- Case 1: exact fit at PHYSICAL EOF, no terminator ---------------- */
    t2_orig_stdin = stdin;
    stdin = fmemopen(t2_c1_src, 4, "r");
    memset(t2_line, 'Z', sizeof(t2_line));
    t2_status = read_line(t2_line, sizeof(t2_line), &t2_error);
    T2_CHECK("c1_exact_eof_ok",
             t2_status == STDLIB_OK && t2_error.code() == CPLUS_EXIT_OK &&
                 t2_error.category() == STDLIB_CATEGORY_OK &&
                 strcmp(t2_line, "abcd") == 0);
    t2_status = read_line(t2_line, sizeof(t2_line), &t2_error);
    T2_CHECK("c1_exact_eof_next",
             t2_status == STDLIB_EOF && t2_error.code() == CPLUS_EXIT_OK &&
                 t2_error.category() == STDLIB_CATEGORY_OK);
    fclose(stdin);
    stdin = t2_orig_stdin;

    /* ---- Case 2: exact fit then '\n' at EOF; no phantom empty line ------- */
    stdin = fmemopen(t2_c2_src, 5, "r");
    memset(t2_line, 'Z', sizeof(t2_line));
    t2_status = read_line(t2_line, sizeof(t2_line), &t2_error);
    T2_CHECK("c2_newline_ok",
             t2_status == STDLIB_OK && t2_error.code() == CPLUS_EXIT_OK &&
                 t2_error.category() == STDLIB_CATEGORY_OK &&
                 strcmp(t2_line, "abcd") == 0);
    /* Reset the destination so an empty-line write ('\0') would be visible. */
    memset(t2_line, 'Z', sizeof(t2_line));
    memset(t2_line_snap, 'Z', sizeof(t2_line_snap));
    t2_status = read_line(t2_line, sizeof(t2_line), &t2_error);
    T2_CHECK("c2_newline_next_eof",
             t2_status == STDLIB_EOF && t2_error.code() == CPLUS_EXIT_OK &&
                 t2_error.category() == STDLIB_CATEGORY_OK &&
                 memcmp(t2_line, t2_line_snap, sizeof(t2_line)) == 0);
    fclose(stdin);
    stdin = t2_orig_stdin;

    /* ---- Case 3: CR-only terminator at the fgets width boundary ---------- */
    /* Fixture: "abcd\rNEXT\n".  The peek must consume the CR (and not leak a
     * byte), leaving the stream aligned on NEXT. */
    t2_status = read_line(t2_line, sizeof(t2_line), &t2_error);
    T2_CHECK("c3_cr_boundary",
             t2_status == STDLIB_OK && t2_error.code() == CPLUS_EXIT_OK &&
                 strcmp(t2_line, "abcd") == 0);
    t2_status = read_line(t2_line, sizeof(t2_line), &t2_error);
    T2_CHECK("c3_next_aligned",
             t2_status == STDLIB_OK && t2_error.code() == CPLUS_EXIT_OK &&
                 strcmp(t2_line, "NEXT") == 0);

    /* ---- Cases 4 & 5: malformed / overflow / valid per numeric reader ---- */
    /* Fixture tokens per reader: "12xyz", an overflow token, then a valid value.
     * NOTE: the 26-digit decimal 99999999999999999999999999 (~1e26) overflows
     * int/long but is a perfectly finite IEEE-754 value for float/double, so
     * read_float/read_double correctly return STDLIB_OK for it.  The float and
     * double overflow probes therefore use "1e400" (overflows both).  See
     * docs/stdlib/DEEP_IO_TASK2_CONTRACT.md, section "Observed discrepancy". */

    /* read_int */
    t2_i_val = 0x12345678;
    t2_status = read_int(&t2_i_val, &t2_error);
    T2_CHECK("c4_i_malformed",
             t2_status == STDLIB_FAIL &&
                 t2_error.code() == CPLUS_EXIT_UNSUPPORTED &&
                 t2_error.category() == STDLIB_CATEGORY_INVALID &&
                 memcmp(&t2_i_val, &t2_i_snap, sizeof(t2_i_val)) == 0);
    t2_i_val = 0x12345678;
    t2_status = read_int(&t2_i_val, &t2_error);
    T2_CHECK("c4_i_overflow",
             t2_status == STDLIB_FAIL && t2_error.code() == CPLUS_EXIT_BOUNDS &&
                 t2_error.category() == STDLIB_CATEGORY_RANGE &&
                 memcmp(&t2_i_val, &t2_i_snap, sizeof(t2_i_val)) == 0);
    t2_i_val = 0x12345678;
    t2_status = read_int(&t2_i_val, &t2_error);
    T2_CHECK("c5_i_valid_after",
             t2_status == STDLIB_OK && t2_error.code() == CPLUS_EXIT_OK &&
                 t2_error.category() == STDLIB_CATEGORY_OK && t2_i_val == -77);

    /* read_long */
    t2_l_val = 0x1122334455667788L;
    t2_status = read_long(&t2_l_val, &t2_error);
    T2_CHECK("c4_l_malformed",
             t2_status == STDLIB_FAIL &&
                 t2_error.code() == CPLUS_EXIT_UNSUPPORTED &&
                 t2_error.category() == STDLIB_CATEGORY_INVALID &&
                 memcmp(&t2_l_val, &t2_l_snap, sizeof(t2_l_val)) == 0);
    t2_l_val = 0x1122334455667788L;
    t2_status = read_long(&t2_l_val, &t2_error);
    T2_CHECK("c4_l_overflow",
             t2_status == STDLIB_FAIL && t2_error.code() == CPLUS_EXIT_BOUNDS &&
                 t2_error.category() == STDLIB_CATEGORY_RANGE &&
                 memcmp(&t2_l_val, &t2_l_snap, sizeof(t2_l_val)) == 0);
    t2_l_val = 0x1122334455667788L;
    t2_status = read_long(&t2_l_val, &t2_error);
    T2_CHECK("c5_l_valid_after",
             t2_status == STDLIB_OK && t2_error.code() == CPLUS_EXIT_OK &&
                 t2_error.category() == STDLIB_CATEGORY_OK &&
                 t2_l_val == 1234567890123L);

    /* read_float */
    t2_f_val = -2468.125f;
    t2_status = read_float(&t2_f_val, &t2_error);
    T2_CHECK("c4_f_malformed",
             t2_status == STDLIB_FAIL &&
                 t2_error.code() == CPLUS_EXIT_UNSUPPORTED &&
                 t2_error.category() == STDLIB_CATEGORY_INVALID &&
                 memcmp(&t2_f_val, &t2_f_snap, sizeof(t2_f_val)) == 0);
    t2_f_val = -2468.125f;
    t2_status = read_float(&t2_f_val, &t2_error);
    T2_CHECK("c4_f_overflow",
             t2_status == STDLIB_FAIL && t2_error.code() == CPLUS_EXIT_BOUNDS &&
                 t2_error.category() == STDLIB_CATEGORY_RANGE &&
                 memcmp(&t2_f_val, &t2_f_snap, sizeof(t2_f_val)) == 0);
    t2_f_val = -2468.125f;
    t2_status = read_float(&t2_f_val, &t2_error);
    T2_CHECK("c5_f_valid_after",
             t2_status == STDLIB_OK && t2_error.code() == CPLUS_EXIT_OK &&
                 t2_error.category() == STDLIB_CATEGORY_OK && t2_f_val == 6.5f);

    /* read_double */
    t2_d_val = 13579.25;
    t2_status = read_double(&t2_d_val, &t2_error);
    T2_CHECK("c4_d_malformed",
             t2_status == STDLIB_FAIL &&
                 t2_error.code() == CPLUS_EXIT_UNSUPPORTED &&
                 t2_error.category() == STDLIB_CATEGORY_INVALID &&
                 memcmp(&t2_d_val, &t2_d_snap, sizeof(t2_d_val)) == 0);
    t2_d_val = 13579.25;
    t2_status = read_double(&t2_d_val, &t2_error);
    T2_CHECK("c4_d_overflow",
             t2_status == STDLIB_FAIL && t2_error.code() == CPLUS_EXIT_BOUNDS &&
                 t2_error.category() == STDLIB_CATEGORY_RANGE &&
                 memcmp(&t2_d_val, &t2_d_snap, sizeof(t2_d_val)) == 0);
    t2_d_val = 13579.25;
    t2_status = read_double(&t2_d_val, &t2_error);
    T2_CHECK("c5_d_valid_after",
             t2_status == STDLIB_OK && t2_error.code() == CPLUS_EXIT_OK &&
                 t2_error.category() == STDLIB_CATEGORY_OK && t2_d_val == -2.25);

    /* ---- Case 6: EOF before any data for each numeric reader ------------- */
    /* Fixture is exhausted after "-2.25"; every reader must return STDLIB_EOF
     * with a clear error record and leave its out-param byte-for-byte intact. */
    t2_i_val = 0x12345678;
    t2_status = read_int(&t2_i_val, &t2_error);
    T2_CHECK("c6_i_eof",
             t2_status == STDLIB_EOF && t2_error.code() == CPLUS_EXIT_OK &&
                 t2_error.category() == STDLIB_CATEGORY_OK &&
                 memcmp(&t2_i_val, &t2_i_snap, sizeof(t2_i_val)) == 0);
    t2_l_val = 0x1122334455667788L;
    t2_status = read_long(&t2_l_val, &t2_error);
    T2_CHECK("c6_l_eof",
             t2_status == STDLIB_EOF && t2_error.code() == CPLUS_EXIT_OK &&
                 t2_error.category() == STDLIB_CATEGORY_OK &&
                 memcmp(&t2_l_val, &t2_l_snap, sizeof(t2_l_val)) == 0);
    t2_f_val = -2468.125f;
    t2_status = read_float(&t2_f_val, &t2_error);
    T2_CHECK("c6_f_eof",
             t2_status == STDLIB_EOF && t2_error.code() == CPLUS_EXIT_OK &&
                 t2_error.category() == STDLIB_CATEGORY_OK &&
                 memcmp(&t2_f_val, &t2_f_snap, sizeof(t2_f_val)) == 0);
    t2_d_val = 13579.25;
    t2_status = read_double(&t2_d_val, &t2_error);
    T2_CHECK("c6_d_eof",
             t2_status == STDLIB_EOF && t2_error.code() == CPLUS_EXIT_OK &&
                 t2_error.category() == STDLIB_CATEGORY_OK &&
                 memcmp(&t2_d_val, &t2_d_snap, sizeof(t2_d_val)) == 0);

    print("IO_TASK2", t2_checks, t2_failures);
    return t2_failures ? 1 : 0;
}
