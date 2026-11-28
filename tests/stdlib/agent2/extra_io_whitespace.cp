/* extra_io_whitespace — agent 2: remaining UNTESTED io.hp behaviour paths.
 *
 * Focus (the gaps NOT already pinned by deep_io_1byte / deep_io_token /
 * deep_io_task2), all driven from ONE deterministic .stdin fixture plus a few
 * isolated fmemopen()/tmpfile() streams where a fresh EOF is required:
 *
 *   A. empty stdin and immediate EOF for EVERY reader (read_line, read_int,
 *      read_long, read_float, read_double, direct stdlib_io_token)
 *   B. whitespace-only input (token readers -> EOF; read_line preserves spaces)
 *   C. leading / trailing whitespace around a token, explicit '+', negative,
 *      INT_MIN / INT_MAX and one below / above, LONG_MIN / LONG_MAX, decimal
 *      into an integer parser, numeric token with trailing characters
 *   D. very long numeric token: exact 63/64-byte token-buffer boundary and a
 *      200-digit token, with stream alignment verified after each
 *   E. repeated reads after malformed input (fail, fail, ok, fail, ok)
 *   F. repeated reads after a truncated LINE — exact bytes returned next,
 *      including the single leftover whitespace byte after a token read and
 *      a truncated line that ends at PHYSICAL EOF with no terminator
 *   G. two-byte buffer boundary (1-char, empty, overlong, CR terminator)
 *   H. multiple consecutive empty lines (line reader and token reader)
 *
 * Every check prints "<tag> <0|1>".  The final line is
 * "EXTRA_IO_WHITESPACE <checks> <failures>".  Exit status is 0 only when
 * failures == 0.  No headers and no existing tests were modified; there is
 * deliberately no .expected file (file ownership is restricted) — the exact
 * stdout is recorded in extra_io_whitespace_NOTES.md.
 */
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/io.hp"

#define EW_CHK(tag, expr)        \
    do {                         \
        ew_ok = (expr);          \
        ew_checks++;             \
        if (!ew_ok)              \
            ew_failures++;       \
        print(tag, ew_ok);       \
    } while (0)

/* Snapshot compare for byte-for-byte "unchanged on failure" assertions. */
#define EW_SAME_int(v, s)   (memcmp(&(v), &(s), sizeof(v)) == 0)
#define EW_SAME_long(v, s)  (memcmp(&(v), &(s), sizeof(v)) == 0)
#define EW_SAME_float(v, s) (memcmp(&(v), &(s), sizeof(v)) == 0)
#define EW_SAME_double(v, s) (memcmp(&(v), &(s), sizeof(v)) == 0)

int main(void) {
    error ew_err = error();
    int ew_st;
    int ew_ok;
    int ew_checks = 0;
    int ew_failures = 0;

    /* Immutable sentinels / snapshots for byte-for-byte checks. */
    int ew_i = 0x12345678;
    int ew_i_snap = 0x12345678;
    long ew_l = 0x1122334455667788L;
    long ew_l_snap = 0x1122334455667788L;
    float ew_f = -2468.125f;
    float ew_f_snap = -2468.125f;
    double ew_d = 13579.25;
    double ew_d_snap = 13579.25;

    char ew_line[16];
    char ew_line_snap[16];
    char ew_two[7];
    char ew_two_snap[7];
    char ew_tok[8];
    char ew_tok_snap[8];
    char ew_ws_all[] = " \t \n\v\f  \n";
    char ew_ws_short[] = " \t \n";
    char ew_trunc_src[] = "abcdef";
    FILE *ew_saved;

    memset(ew_line_snap, 'Z', sizeof(ew_line_snap));
    memset(ew_two_snap, 'Z', sizeof(ew_two_snap));
    memset(ew_tok_snap, 'Z', sizeof(ew_tok_snap));

    /* ==================================================================== *
     * C. token whitespace / signs / integer boundaries / malformed text     *
     * ==================================================================== */

    /* Leading whitespace (space, tab, newline) before the token "7". */
    ew_i = ew_i_snap;
    ew_st = read_int(&ew_i, &ew_err);
    EW_CHK("c_lead_ws",
           ew_st == STDLIB_OK && ew_err.code() == CPLUS_EXIT_OK &&
               ew_err.category() == STDLIB_CATEGORY_OK && ew_i == 7);

    /* Explicit plus sign. */
    ew_i = ew_i_snap;
    ew_st = read_int(&ew_i, &ew_err);
    EW_CHK("c_plus",
           ew_st == STDLIB_OK && ew_i == 42);

    /* Negative integer. */
    ew_i = ew_i_snap;
    ew_st = read_int(&ew_i, &ew_err);
    EW_CHK("c_neg",
           ew_st == STDLIB_OK && ew_i == -17);

    /* Trailing whitespace after the token "88" (spaces + tab + newline). */
    ew_i = ew_i_snap;
    ew_st = read_int(&ew_i, &ew_err);
    EW_CHK("c_trail_ws",
           ew_st == STDLIB_OK && ew_err.code() == CPLUS_EXIT_OK && ew_i == 88);

    /* INT_MIN and INT_MAX are valid boundary values. */
    ew_i = ew_i_snap;
    ew_st = read_int(&ew_i, &ew_err);
    EW_CHK("c_i_min",
           ew_st == STDLIB_OK && ew_err.code() == CPLUS_EXIT_OK &&
               ew_i == INT_MIN);

    ew_i = ew_i_snap;
    ew_st = read_int(&ew_i, &ew_err);
    EW_CHK("c_i_max",
           ew_st == STDLIB_OK && ew_err.code() == CPLUS_EXIT_OK &&
               ew_i == INT_MAX);

    /* One above INT_MAX / one below INT_MIN: RANGE, out-param unchanged. */
    ew_i = ew_i_snap;
    ew_st = read_int(&ew_i, &ew_err);
    EW_CHK("c_i_max_1",
           ew_st == STDLIB_FAIL && ew_err.code() == CPLUS_EXIT_BOUNDS &&
               ew_err.category() == STDLIB_CATEGORY_RANGE &&
               EW_SAME_int(ew_i, ew_i_snap));

    ew_i = ew_i_snap;
    ew_st = read_int(&ew_i, &ew_err);
    EW_CHK("c_i_min_1",
           ew_st == STDLIB_FAIL && ew_err.code() == CPLUS_EXIT_BOUNDS &&
               ew_err.category() == STDLIB_CATEGORY_RANGE &&
               EW_SAME_int(ew_i, ew_i_snap));

    /* LONG_MIN / LONG_MAX valid boundaries for read_long. */
    ew_l = ew_l_snap;
    ew_st = read_long(&ew_l, &ew_err);
    EW_CHK("c_l_min",
           ew_st == STDLIB_OK && ew_err.code() == CPLUS_EXIT_OK &&
               ew_l == LONG_MIN);

    ew_l = ew_l_snap;
    ew_st = read_long(&ew_l, &ew_err);
    EW_CHK("c_l_max",
           ew_st == STDLIB_OK && ew_err.code() == CPLUS_EXIT_OK &&
               ew_l == LONG_MAX);

    /* Decimal text into an integer parser: INVALID, out-param unchanged. */
    ew_i = ew_i_snap;
    ew_st = read_int(&ew_i, &ew_err);
    EW_CHK("c_dec_i",
           ew_st == STDLIB_FAIL && ew_err.code() == CPLUS_EXIT_UNSUPPORTED &&
               ew_err.category() == STDLIB_CATEGORY_INVALID &&
               EW_SAME_int(ew_i, ew_i_snap));

    /* Numeric token with trailing characters: INVALID, out unchanged. */
    ew_i = ew_i_snap;
    ew_st = read_int(&ew_i, &ew_err);
    EW_CHK("c_trail_chars",
           ew_st == STDLIB_FAIL && ew_err.code() == CPLUS_EXIT_UNSUPPORTED &&
               ew_err.category() == STDLIB_CATEGORY_INVALID &&
               EW_SAME_int(ew_i, ew_i_snap));

    /* ==================================================================== *
     * D. very long numeric token: 63/64-byte token-buffer boundary          *
     * ==================================================================== */

    /* 61 leading zeros + "42" = 63 chars: exactly fits the 63-byte payload
     * of the 64-byte internal token buffer -> parsed as 42. */
    ew_i = ew_i_snap;
    ew_st = read_int(&ew_i, &ew_err);
    EW_CHK("d_63_ok",
           ew_st == STDLIB_OK && ew_err.code() == CPLUS_EXIT_OK && ew_i == 42);

    /* 62 leading zeros + "42" = 64 chars: one past the payload -> truncated
     * (stdlib_io_token returns 2) -> RANGE, out-param unchanged. */
    ew_i = ew_i_snap;
    ew_st = read_int(&ew_i, &ew_err);
    EW_CHK("d_64_fail",
           ew_st == STDLIB_FAIL && ew_err.code() == CPLUS_EXIT_BOUNDS &&
               ew_err.category() == STDLIB_CATEGORY_RANGE &&
               EW_SAME_int(ew_i, ew_i_snap));

    /* The truncated token was fully drained; next read is not shifted. */
    ew_i = ew_i_snap;
    ew_st = read_int(&ew_i, &ew_err);
    EW_CHK("d_aligned",
           ew_st == STDLIB_OK && ew_err.code() == CPLUS_EXIT_OK && ew_i == 5);

    /* Truly huge (200-digit) token: RANGE, out unchanged, stream aligned. */
    ew_i = ew_i_snap;
    ew_st = read_int(&ew_i, &ew_err);
    EW_CHK("d_huge",
           ew_st == STDLIB_FAIL && ew_err.code() == CPLUS_EXIT_BOUNDS &&
               ew_err.category() == STDLIB_CATEGORY_RANGE &&
               EW_SAME_int(ew_i, ew_i_snap));

    ew_i = ew_i_snap;
    ew_st = read_int(&ew_i, &ew_err);
    EW_CHK("d_huge_next",
           ew_st == STDLIB_OK && ew_err.code() == CPLUS_EXIT_OK && ew_i == 9);

    /* ==================================================================== *
     * E. repeated reads after malformed input                               *
     * ==================================================================== */

    ew_i = ew_i_snap;
    ew_st = read_int(&ew_i, &ew_err);
    EW_CHK("e_bad1",
           ew_st == STDLIB_FAIL && ew_err.code() == CPLUS_EXIT_UNSUPPORTED &&
               ew_err.category() == STDLIB_CATEGORY_INVALID &&
               EW_SAME_int(ew_i, ew_i_snap));

    ew_i = ew_i_snap;
    ew_st = read_int(&ew_i, &ew_err);
    EW_CHK("e_bad2",
           ew_st == STDLIB_FAIL && ew_err.code() == CPLUS_EXIT_UNSUPPORTED &&
               ew_err.category() == STDLIB_CATEGORY_INVALID &&
               EW_SAME_int(ew_i, ew_i_snap));

    ew_i = ew_i_snap;
    ew_st = read_int(&ew_i, &ew_err);
    EW_CHK("e_good",
           ew_st == STDLIB_OK && ew_err.code() == CPLUS_EXIT_OK &&
               ew_err.category() == STDLIB_CATEGORY_OK && ew_i == 123);

    ew_i = ew_i_snap;
    ew_st = read_int(&ew_i, &ew_err);
    EW_CHK("e_bad3",
           ew_st == STDLIB_FAIL && ew_err.code() == CPLUS_EXIT_UNSUPPORTED &&
               ew_err.category() == STDLIB_CATEGORY_INVALID &&
               EW_SAME_int(ew_i, ew_i_snap));

    ew_i = ew_i_snap;
    ew_st = read_int(&ew_i, &ew_err);
    EW_CHK("e_good2",
           ew_st == STDLIB_OK && ew_err.code() == CPLUS_EXIT_OK &&
               ew_err.category() == STDLIB_CATEGORY_OK && ew_i == -8);

    /* ==================================================================== *
     * F. repeated reads after a truncated LINE — exact next bytes           *
     * ==================================================================== */

    /* Overlong "abcdefgh" into 6-byte buffer (capacity 5): BOUNDS.  The only
     * documented write is NUL at byte 0; the one-past sentinel is untouched. */
    memset(ew_two, 'Z', sizeof(ew_two));
    ew_st = read_line(ew_two, 6, &ew_err);
    EW_CHK("f_trunc1",
           ew_st == STDLIB_FAIL && ew_err.code() == CPLUS_EXIT_BOUNDS &&
               ew_err.category() == STDLIB_CATEGORY_BOUNDS &&
               ew_two[0] == '\0' && ew_two[6] == 'Z');

    /* Exactly which bytes are returned next: the whole truncated line was
     * drained, so the next read sees "NEXTLINE". */
    memset(ew_line, 'Z', sizeof(ew_line));
    ew_st = read_line(ew_line, sizeof(ew_line), &ew_err);
    EW_CHK("f_next1",
           ew_st == STDLIB_OK && ew_err.code() == CPLUS_EXIT_OK &&
               strcmp(ew_line, "NEXTLINE") == 0);

    /* Second truncation, then a numeric reader on "  42  ". */
    memset(ew_two, 'Z', sizeof(ew_two));
    ew_st = read_line(ew_two, 6, &ew_err);
    EW_CHK("f_trunc2",
           ew_st == STDLIB_FAIL && ew_err.code() == CPLUS_EXIT_BOUNDS &&
               ew_two[0] == '\0' && ew_two[6] == 'Z');

    ew_i = ew_i_snap;
    ew_st = read_int(&ew_i, &ew_err);
    EW_CHK("f_next_token",
           ew_st == STDLIB_OK && ew_err.code() == CPLUS_EXIT_OK && ew_i == 42);

    /* stdlib_io_token consumes exactly ONE trailing whitespace byte, so the
     * next read_line sees the leftover " \n" and returns a single space —
     * NOT "AFTER2".  This pins the exact stream position after a token read. */
    memset(ew_line, 'Z', sizeof(ew_line));
    ew_st = read_line(ew_line, sizeof(ew_line), &ew_err);
    EW_CHK("f_next_bytes",
           ew_st == STDLIB_OK && ew_err.code() == CPLUS_EXIT_OK &&
               strcmp(ew_line, " ") == 0);

    /* Only now does "AFTER2" become the next line. */
    memset(ew_line, 'Z', sizeof(ew_line));
    ew_st = read_line(ew_line, sizeof(ew_line), &ew_err);
    EW_CHK("f_after2",
           ew_st == STDLIB_OK && ew_err.code() == CPLUS_EXIT_OK &&
               strcmp(ew_line, "AFTER2") == 0);

    /* Third truncation, then an empty line, then TAIL. */
    memset(ew_two, 'Z', sizeof(ew_two));
    ew_st = read_line(ew_two, 6, &ew_err);
    EW_CHK("f_trunc3",
           ew_st == STDLIB_FAIL && ew_err.code() == CPLUS_EXIT_BOUNDS &&
               ew_two[0] == '\0' && ew_two[6] == 'Z');

    memset(ew_line, 'Z', sizeof(ew_line));
    ew_st = read_line(ew_line, sizeof(ew_line), &ew_err);
    EW_CHK("f_empty_after",
           ew_st == STDLIB_OK && ew_err.code() == CPLUS_EXIT_OK &&
               strcmp(ew_line, "") == 0);

    memset(ew_line, 'Z', sizeof(ew_line));
    ew_st = read_line(ew_line, sizeof(ew_line), &ew_err);
    EW_CHK("f_tail_after",
           ew_st == STDLIB_OK && ew_err.code() == CPLUS_EXIT_OK &&
               strcmp(ew_line, "TAIL") == 0);

    /* ==================================================================== *
     * G. two-byte buffer boundary                                           *
     * ==================================================================== */

    memset(ew_two, 'Z', sizeof(ew_two));
    ew_st = read_line(ew_two, 2, &ew_err);
    EW_CHK("g_one_char",
           ew_st == STDLIB_OK && ew_err.code() == CPLUS_EXIT_OK &&
               strcmp(ew_two, "z") == 0 && ew_two[6] == 'Z');

    memset(ew_two, 'Z', sizeof(ew_two));
    ew_st = read_line(ew_two, 2, &ew_err);
    EW_CHK("g_empty",
           ew_st == STDLIB_OK && ew_err.code() == CPLUS_EXIT_OK &&
               strcmp(ew_two, "") == 0 && ew_two[6] == 'Z');

    memset(ew_two, 'Z', sizeof(ew_two));
    ew_st = read_line(ew_two, 2, &ew_err);
    EW_CHK("g_over",
           ew_st == STDLIB_FAIL && ew_err.code() == CPLUS_EXIT_BOUNDS &&
               ew_err.category() == STDLIB_CATEGORY_BOUNDS &&
               ew_two[0] == '\0' && ew_two[6] == 'Z');

    memset(ew_line, 'Z', sizeof(ew_line));
    ew_st = read_line(ew_line, sizeof(ew_line), &ew_err);
    EW_CHK("g_next",
           ew_st == STDLIB_OK && ew_err.code() == CPLUS_EXIT_OK &&
               strcmp(ew_line, "OK2") == 0);

    /* CR terminator at the exact width boundary: consumed with its LF. */
    memset(ew_two, 'Z', sizeof(ew_two));
    ew_st = read_line(ew_two, 2, &ew_err);
    EW_CHK("g_cr",
           ew_st == STDLIB_OK && ew_err.code() == CPLUS_EXIT_OK &&
               strcmp(ew_two, "w") == 0 && ew_two[6] == 'Z');

    memset(ew_line, 'Z', sizeof(ew_line));
    ew_st = read_line(ew_line, sizeof(ew_line), &ew_err);
    EW_CHK("g_cr_next",
           ew_st == STDLIB_OK && ew_err.code() == CPLUS_EXIT_OK &&
               strcmp(ew_line, "AFTERCR") == 0);

    /* ==================================================================== *
     * H. multiple consecutive empty lines                                   *
     * ==================================================================== */

    memset(ew_line, 'Z', sizeof(ew_line));
    ew_st = read_line(ew_line, sizeof(ew_line), &ew_err);
    EW_CHK("h_empty1",
           ew_st == STDLIB_OK && ew_err.code() == CPLUS_EXIT_OK &&
               strcmp(ew_line, "") == 0);

    memset(ew_line, 'Z', sizeof(ew_line));
    ew_st = read_line(ew_line, sizeof(ew_line), &ew_err);
    EW_CHK("h_empty2",
           ew_st == STDLIB_OK && ew_err.code() == CPLUS_EXIT_OK &&
               strcmp(ew_line, "") == 0);

    memset(ew_line, 'Z', sizeof(ew_line));
    ew_st = read_line(ew_line, sizeof(ew_line), &ew_err);
    EW_CHK("h_empty3",
           ew_st == STDLIB_OK && ew_err.code() == CPLUS_EXIT_OK &&
               strcmp(ew_line, "") == 0);

    memset(ew_line, 'Z', sizeof(ew_line));
    ew_st = read_line(ew_line, sizeof(ew_line), &ew_err);
    EW_CHK("h_empty4",
           ew_st == STDLIB_OK && ew_err.code() == CPLUS_EXIT_OK &&
               strcmp(ew_line, "") == 0);

    memset(ew_line, 'Z', sizeof(ew_line));
    ew_st = read_line(ew_line, sizeof(ew_line), &ew_err);
    EW_CHK("h_end",
           ew_st == STDLIB_OK && ew_err.code() == CPLUS_EXIT_OK &&
               strcmp(ew_line, "END") == 0);

    /* A token reader skips two empty lines and still returns 77. */
    ew_i = ew_i_snap;
    ew_st = read_int(&ew_i, &ew_err);
    EW_CHK("h_token_after_empties",
           ew_st == STDLIB_OK && ew_err.code() == CPLUS_EXIT_OK && ew_i == 77);

    /* Natural EOF of the shared fixture: clean EOF, out-param unchanged. */
    memset(ew_line, 'Z', sizeof(ew_line));
    ew_st = read_line(ew_line, sizeof(ew_line), &ew_err);
    EW_CHK("h_eof_line",
           ew_st == STDLIB_EOF && ew_err.code() == CPLUS_EXIT_OK &&
               ew_err.category() == STDLIB_CATEGORY_OK &&
               memcmp(ew_line, ew_line_snap, sizeof(ew_line)) == 0);

    ew_i = ew_i_snap;
    ew_st = read_int(&ew_i, &ew_err);
    EW_CHK("h_eof_int",
           ew_st == STDLIB_EOF && ew_err.code() == CPLUS_EXIT_OK &&
               ew_err.category() == STDLIB_CATEGORY_OK &&
               EW_SAME_int(ew_i, ew_i_snap));

    /* ==================================================================== *
     * A. empty stdin and immediate EOF for EVERY reader                     *
     *    Each reader gets a FRESH empty stream (tmpfile), so "immediate EOF" *
     *    is not an artefact of a previously consumed stream.                *
     * ==================================================================== */

    ew_saved = stdin;
    stdin = tmpfile();
    memset(ew_line, 'Z', sizeof(ew_line));
    ew_st = read_line(ew_line, sizeof(ew_line), &ew_err);
    EW_CHK("a_line_eof",
           ew_st == STDLIB_EOF && ew_err.code() == CPLUS_EXIT_OK &&
               ew_err.category() == STDLIB_CATEGORY_OK &&
               memcmp(ew_line, ew_line_snap, sizeof(ew_line)) == 0);
    fclose(stdin);
    stdin = ew_saved;

    ew_saved = stdin;
    stdin = tmpfile();
    ew_i = ew_i_snap;
    ew_st = read_int(&ew_i, &ew_err);
    EW_CHK("a_int_eof",
           ew_st == STDLIB_EOF && ew_err.code() == CPLUS_EXIT_OK &&
               ew_err.category() == STDLIB_CATEGORY_OK &&
               EW_SAME_int(ew_i, ew_i_snap));
    fclose(stdin);
    stdin = ew_saved;

    ew_saved = stdin;
    stdin = tmpfile();
    ew_l = ew_l_snap;
    ew_st = read_long(&ew_l, &ew_err);
    EW_CHK("a_long_eof",
           ew_st == STDLIB_EOF && ew_err.code() == CPLUS_EXIT_OK &&
               ew_err.category() == STDLIB_CATEGORY_OK &&
               EW_SAME_long(ew_l, ew_l_snap));
    fclose(stdin);
    stdin = ew_saved;

    ew_saved = stdin;
    stdin = tmpfile();
    ew_f = ew_f_snap;
    ew_st = read_float(&ew_f, &ew_err);
    EW_CHK("a_float_eof",
           ew_st == STDLIB_EOF && ew_err.code() == CPLUS_EXIT_OK &&
               ew_err.category() == STDLIB_CATEGORY_OK &&
               EW_SAME_float(ew_f, ew_f_snap));
    fclose(stdin);
    stdin = ew_saved;

    ew_saved = stdin;
    stdin = tmpfile();
    ew_d = ew_d_snap;
    ew_st = read_double(&ew_d, &ew_err);
    EW_CHK("a_double_eof",
           ew_st == STDLIB_EOF && ew_err.code() == CPLUS_EXIT_OK &&
               ew_err.category() == STDLIB_CATEGORY_OK &&
               EW_SAME_double(ew_d, ew_d_snap));
    fclose(stdin);
    stdin = ew_saved;

    /* Direct token reader on an empty stream: returns 1 and writes nothing. */
    ew_saved = stdin;
    stdin = tmpfile();
    memset(ew_tok, 'Z', sizeof(ew_tok));
    ew_st = stdlib_io_token(ew_tok, sizeof(ew_tok));
    EW_CHK("a_token_eof",
           ew_st == 1 && memcmp(ew_tok, ew_tok_snap, sizeof(ew_tok)) == 0);
    fclose(stdin);
    stdin = ew_saved;

    /* ==================================================================== *
     * B. whitespace-only input                                              *
     * ==================================================================== */

    /* Token readers skip all whitespace, hit EOF, and leave out unchanged. */
    ew_saved = stdin;
    stdin = fmemopen(ew_ws_all, 9, "r");
    ew_i = ew_i_snap;
    ew_st = read_int(&ew_i, &ew_err);
    EW_CHK("b_int_ws_eof",
           ew_st == STDLIB_EOF && ew_err.code() == CPLUS_EXIT_OK &&
               ew_err.category() == STDLIB_CATEGORY_OK &&
               EW_SAME_int(ew_i, ew_i_snap));
    fclose(stdin);
    stdin = ew_saved;

    ew_saved = stdin;
    stdin = fmemopen(ew_ws_short, 4, "r");
    memset(ew_tok, 'Z', sizeof(ew_tok));
    ew_st = stdlib_io_token(ew_tok, sizeof(ew_tok));
    EW_CHK("b_token_ws_eof",
           ew_st == 1 && memcmp(ew_tok, ew_tok_snap, sizeof(ew_tok)) == 0);
    fclose(stdin);
    stdin = ew_saved;

    /* read_line on a whitespace-only LINE strips only CR/LF: " \t \n" -> " \t ". */
    ew_saved = stdin;
    stdin = fmemopen(ew_ws_short, 4, "r");
    memset(ew_line, 'Z', sizeof(ew_line));
    ew_st = read_line(ew_line, sizeof(ew_line), &ew_err);
    EW_CHK("b_line_ws",
           ew_st == STDLIB_OK && ew_err.code() == CPLUS_EXIT_OK &&
               strcmp(ew_line, " \t ") == 0);
    memset(ew_line, 'Z', sizeof(ew_line));
    ew_st = read_line(ew_line, sizeof(ew_line), &ew_err);
    EW_CHK("b_line_ws_next_eof",
           ew_st == STDLIB_EOF && ew_err.code() == CPLUS_EXIT_OK &&
               ew_err.category() == STDLIB_CATEGORY_OK &&
               memcmp(ew_line, ew_line_snap, sizeof(ew_line)) == 0);
    fclose(stdin);
    stdin = ew_saved;

    /* ==================================================================== *
     * F2. truncated LINE at PHYSICAL EOF (no terminator)                    *
     * ==================================================================== */

    /* "abcdef" (6 bytes, no newline) into a 6-byte buffer: fgets stops at 5
     * bytes with one byte still in the stream, the peek sees 'f' and drains
     * to physical EOF -> BOUNDS.  The next read is a clean EOF. */
    ew_saved = stdin;
    stdin = fmemopen(ew_trunc_src, 6, "r");
    memset(ew_two, 'Z', sizeof(ew_two));
    ew_st = read_line(ew_two, 6, &ew_err);
    EW_CHK("f_eof_trunc",
           ew_st == STDLIB_FAIL && ew_err.code() == CPLUS_EXIT_BOUNDS &&
               ew_err.category() == STDLIB_CATEGORY_BOUNDS &&
               ew_two[0] == '\0' && ew_two[6] == 'Z');
    memset(ew_line, 'Z', sizeof(ew_line));
    ew_st = read_line(ew_line, sizeof(ew_line), &ew_err);
    EW_CHK("f_eof_next",
           ew_st == STDLIB_EOF && ew_err.code() == CPLUS_EXIT_OK &&
               ew_err.category() == STDLIB_CATEGORY_OK &&
               memcmp(ew_line, ew_line_snap, sizeof(ew_line)) == 0);
    fclose(stdin);
    stdin = ew_saved;

    print("EXTRA_IO_WHITESPACE", ew_checks, ew_failures);
    return ew_failures == 0 ? 0 : 1;
}
