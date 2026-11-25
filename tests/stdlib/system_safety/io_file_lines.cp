/* io_file_lines -- Task 3 Phase 2: line-buffer boundaries, overlong-line
 * drain verification, and EOF-before/after-data for stdlib/file.hp and
 * stdlib/io.hp.
 *
 * All fixtures are in-memory tmpfile() streams (plus controlled stdin swaps
 * for io.hp), so the test is deterministic and leaves no on-disk state.
 * Bounded: every read loop has a fixed iteration cap; the shell applies
 * `timeout 30` as an outer guard.  No variable division/modulo and no
 * pointer-parameter subscripts (use __builtin_memcpy); all out-params are
 * initialised; all locals have unique prefixes.
 *
 * Each check prints "<tag> <0|1>"; final line is
 * "IO_FILE_LINES <checks> <failures>"; exit 0 only when failures==0.
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"
#include "stdlib/io.hp"

static int ifln_checks = 0;
static int ifln_failures = 0;

static void ifln_check(const char *tag, int cond) {
    ifln_checks++;
    if (!cond)
        ifln_failures++;
    print(tag, cond ? 1 : 0);
}

/* Seed a tmpfile with literal bytes and rewind it. */
static FILE *ifln_stream(const char *bytes, size_t n) {
    FILE *ifln_f = tmpfile();
    if (ifln_f == NULL)
        return NULL;
    if (fwrite(bytes, 1, n, ifln_f) != n) {
        fclose(ifln_f);
        return NULL;
    }
    rewind(ifln_f);
    return ifln_f;
}

int main(void) {
    error ifln_err = error();
    char ifln_buf[16];
    int ifln_st = 0;
    FILE *ifln_raw;
    FILE *ifln_saved;
    file *ifln_unused;

    /* ---- 1. exact fit: (size-2) chars + LF, newline is the peeked byte --- */
    ifln_raw = ifln_stream("1234567\n", 8);
    ifln_check("exact-lf-open", ifln_raw != NULL);
    if (ifln_raw != NULL) {
        file ifln_f1 = file_borrow(ifln_raw);
        memset(ifln_buf, 0x5A, sizeof(ifln_buf));
        ifln_st = ifln_f1.read_line(ifln_buf, 8, &ifln_err);
        ifln_check("exact-lf-line",
                   ifln_st == STDLIB_OK && strcmp(ifln_buf, "1234567") == 0 &&
                       ifln_err.ok());
        ifln_st = ifln_f1.read_line(ifln_buf, sizeof(ifln_buf), &ifln_err);
        ifln_check("exact-lf-eof", ifln_st == STDLIB_EOF && ifln_err.ok());
        fclose(ifln_raw);
    }

    /* ---- 2. exact fit at physical EOF with no terminator ---------------- */
    ifln_raw = ifln_stream("1234567", 7);
    ifln_check("exact-eof-open", ifln_raw != NULL);
    if (ifln_raw != NULL) {
        file ifln_f2 = file_borrow(ifln_raw);
        memset(ifln_buf, 0x5A, sizeof(ifln_buf));
        ifln_st = ifln_f2.read_line(ifln_buf, 8, &ifln_err);
        ifln_check("exact-eof-line",
                   ifln_st == STDLIB_OK && strcmp(ifln_buf, "1234567") == 0);
        ifln_st = ifln_f2.read_line(ifln_buf, sizeof(ifln_buf), &ifln_err);
        ifln_check("exact-eof-after", ifln_st == STDLIB_EOF && ifln_err.ok());
        fclose(ifln_raw);
    }

    /* ---- 3. overlong LF line: FAIL/BOUNDS, buf cleared, stream drained --- */
    ifln_raw = ifln_stream("ABCDEFGHIJ\nNEXT\n", 16);
    ifln_check("overlong-open", ifln_raw != NULL);
    if (ifln_raw != NULL) {
        file ifln_f3 = file_borrow(ifln_raw);
        memset(ifln_buf, 0x5A, sizeof(ifln_buf));
        ifln_st = ifln_f3.read_line(ifln_buf, 8, &ifln_err);
        ifln_check("overlong-lf-status",
                   ifln_st == STDLIB_FAIL &&
                       ifln_err.category() == STDLIB_CATEGORY_BOUNDS &&
                       ifln_buf[0] == '\0');
        /* drain proof: the very next line must be exactly NEXT */
        memset(ifln_buf, 0x5A, sizeof(ifln_buf));
        ifln_st = ifln_f3.read_line(ifln_buf, sizeof(ifln_buf), &ifln_err);
        ifln_check("overlong-drain-next",
                   ifln_st == STDLIB_OK && strcmp(ifln_buf, "NEXT") == 0);
        ifln_st = ifln_f3.read_line(ifln_buf, sizeof(ifln_buf), &ifln_err);
        ifln_check("overlong-drain-eof", ifln_st == STDLIB_EOF && ifln_err.ok());
        fclose(ifln_raw);
    }

    /* ---- 4. overlong line terminated by physical EOF -------------------- */
    ifln_raw = ifln_stream("ABCDEFGHIJ", 10);
    ifln_check("overlong-eof-open", ifln_raw != NULL);
    if (ifln_raw != NULL) {
        file ifln_f4 = file_borrow(ifln_raw);
        memset(ifln_buf, 0x5A, sizeof(ifln_buf));
        ifln_st = ifln_f4.read_line(ifln_buf, 8, &ifln_err);
        ifln_check("overlong-eof-status",
                   ifln_st == STDLIB_FAIL &&
                       ifln_err.category() == STDLIB_CATEGORY_BOUNDS &&
                       ifln_buf[0] == '\0');
        ifln_st = ifln_f4.read_line(ifln_buf, sizeof(ifln_buf), &ifln_err);
        ifln_check("overlong-eof-after", ifln_st == STDLIB_EOF && ifln_err.ok());
        fclose(ifln_raw);
    }

    /* ---- 5. CRLF strips as ONE terminator when it fits normally --------- */
    ifln_raw = ifln_stream("abcde\r\n", 7);
    ifln_check("crlf-open", ifln_raw != NULL);
    if (ifln_raw != NULL) {
        file ifln_f5 = file_borrow(ifln_raw);
        memset(ifln_buf, 0x5A, sizeof(ifln_buf));
        ifln_st = ifln_f5.read_line(ifln_buf, sizeof(ifln_buf), &ifln_err);
        ifln_check("crlf-normal",
                   ifln_st == STDLIB_OK && strcmp(ifln_buf, "abcde") == 0);
        fclose(ifln_raw);
    }

    /* ---- 6. CRLF at the exact buffer boundary -------------------------
     * buf[8] holds at most 7 chars.  Content "123456\r\n" fills fgets with
     * "123456\r" and leaves "\n" as the next byte.  The wrapper must consume
     * that LF as part of the single CRLF terminator; otherwise the following
     * read sees a spurious leading newline. */
    ifln_raw = ifln_stream("123456\r\nXYZ\n", 12);
    ifln_check("crlf-edge-open", ifln_raw != NULL);
    if (ifln_raw != NULL) {
        file ifln_f6 = file_borrow(ifln_raw);
        memset(ifln_buf, 0x5A, sizeof(ifln_buf));
        ifln_st = ifln_f6.read_line(ifln_buf, 8, &ifln_err);
        ifln_check("crlf-edge-first",
                   ifln_st == STDLIB_OK && strcmp(ifln_buf, "123456") == 0);
        memset(ifln_buf, 0x5A, sizeof(ifln_buf));
        ifln_st = ifln_f6.read_line(ifln_buf, sizeof(ifln_buf), &ifln_err);
        ifln_check("crlf-edge-next-no-leading-lf",
                   ifln_st == STDLIB_OK && strcmp(ifln_buf, "XYZ") == 0);
        ifln_st = ifln_f6.read_line(ifln_buf, sizeof(ifln_buf), &ifln_err);
        ifln_check("crlf-edge-eof", ifln_st == STDLIB_EOF && ifln_err.ok());
        fclose(ifln_raw);
    }

    /* ---- 7. one-byte buffer still consumes exactly one line ------------- */
    ifln_raw = ifln_stream("x\nyy\n", 5);
    ifln_check("onebyte-open", ifln_raw != NULL);
    if (ifln_raw != NULL) {
        file ifln_f7 = file_borrow(ifln_raw);
        char ifln_one[1];
        int ifln_calls = 0;
        int ifln_bounds = 0;
        int ifln_k;
        for (ifln_k = 0; ifln_k < 8; ifln_k++) {
            ifln_st = ifln_f7.read_line(ifln_one, 1, &ifln_err);
            if (ifln_st == STDLIB_EOF)
                break;
            ifln_calls++;
            if (ifln_st == STDLIB_FAIL &&
                ifln_err.category() == STDLIB_CATEGORY_BOUNDS &&
                ifln_one[0] == '\0')
                ifln_bounds++;
        }
        ifln_check("onebyte-progress",
                   ifln_calls == 2 && ifln_bounds == 2 &&
                       ifln_st == STDLIB_EOF);
        fclose(ifln_raw);
    }

    /* ---- 8. EOF before data / after data (file.hp read and read_line) --- */
    ifln_raw = ifln_stream("abc", 3);
    ifln_check("eofseq-open", ifln_raw != NULL);
    if (ifln_raw != NULL) {
        file ifln_f8 = file_borrow(ifln_raw);
        memset(ifln_buf, 0x5A, sizeof(ifln_buf));
        ifln_st = ifln_f8.read_line(ifln_buf, sizeof(ifln_buf), &ifln_err);
        ifln_check("eof-after-data-line",
                   ifln_st == STDLIB_OK && strcmp(ifln_buf, "abc") == 0);
        ifln_st = ifln_f8.read(ifln_buf, sizeof(ifln_buf), &ifln_err);
        ifln_check("eof-after-data-read", ifln_st == STDLIB_EOF && ifln_err.ok());
        ifln_st = ifln_f8.read_line(ifln_buf, sizeof(ifln_buf), &ifln_err);
        ifln_check("eof-repeat-line", ifln_st == STDLIB_EOF && ifln_err.ok());
        fclose(ifln_raw);
    }
    ifln_raw = ifln_stream("", 0);
    ifln_check("eofbefore-open", ifln_raw != NULL);
    if (ifln_raw != NULL) {
        file ifln_f9 = file_borrow(ifln_raw);
        ifln_st = ifln_f9.read_line(ifln_buf, sizeof(ifln_buf), &ifln_err);
        ifln_check("eof-before-data", ifln_st == STDLIB_EOF && ifln_err.ok());
        fclose(ifln_raw);
    }

    /* ---- 9. io.hp::read_line over a swapped stdin ----------------------- */
    ifln_saved = stdin;
    ifln_raw = ifln_stream("1234567\nTOOLONG9999\nTAIL\n", 25);
    ifln_check("iostream-open", ifln_raw != NULL);
    if (ifln_raw != NULL) {
        stdin = ifln_raw;
        memset(ifln_buf, 0x5A, sizeof(ifln_buf));
        ifln_st = read_line(ifln_buf, 8, &ifln_err);
        ifln_check("iostream-exact",
                   ifln_st == STDLIB_OK && strcmp(ifln_buf, "1234567") == 0);
        memset(ifln_buf, 0x5A, sizeof(ifln_buf));
        ifln_st = read_line(ifln_buf, 8, &ifln_err);
        ifln_check("iostream-overlong",
                   ifln_st == STDLIB_FAIL &&
                       ifln_err.category() == STDLIB_CATEGORY_BOUNDS);
        memset(ifln_buf, 0x5A, sizeof(ifln_buf));
        ifln_st = read_line(ifln_buf, sizeof(ifln_buf), &ifln_err);
        ifln_check("iostream-drain",
                   ifln_st == STDLIB_OK && strcmp(ifln_buf, "TAIL") == 0);
        ifln_st = read_line(ifln_buf, sizeof(ifln_buf), &ifln_err);
        ifln_check("iostream-eof", ifln_st == STDLIB_EOF && ifln_err.ok());
        stdin = ifln_saved;
        fclose(ifln_raw);
    } else {
        stdin = ifln_saved;
    }

    /* ---- 10. io.hp token truncation drains to the delimiter ------------- */
    ifln_saved = stdin;
    ifln_raw = ifln_stream("abcdefghijk Z\n", 14);
    ifln_check("token-open", ifln_raw != NULL);
    if (ifln_raw != NULL) {
        char ifln_tok[8];
        int ifln_tokrc;
        stdin = ifln_raw;
        memset(ifln_tok, 0x5A, sizeof(ifln_tok));
        ifln_tokrc = stdlib_io_token(ifln_tok, sizeof(ifln_tok));
        ifln_check("token-truncated",
                   ifln_tokrc == 2 && strcmp(ifln_tok, "abcdefg") == 0);
        memset(ifln_tok, 0x5A, sizeof(ifln_tok));
        ifln_tokrc = stdlib_io_token(ifln_tok, sizeof(ifln_tok));
        ifln_check("token-drained",
                   ifln_tokrc == 0 && strcmp(ifln_tok, "Z") == 0);
        ifln_tokrc = stdlib_io_token(ifln_tok, sizeof(ifln_tok));
        ifln_check("token-eof", ifln_tokrc == 1);
        stdin = ifln_saved;
        fclose(ifln_raw);
    } else {
        stdin = ifln_saved;
    }

    ifln_unused = NULL; /* keeps the file* type referenced without a read */
    (void)ifln_unused;
    print("IO_FILE_LINES", ifln_checks, ifln_failures);
    return ifln_failures == 0 ? 0 : 1;
}
