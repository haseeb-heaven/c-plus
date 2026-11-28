/* Deep contract tests for file::read_line (stdlib/file.hp).
 *
 * Contract under test (mirrors the hardened stdlib/io.hp::read_line):
 *   - a one-byte buffer still consumes exactly one line (no silent stall);
 *   - an overlong line is drained and reported as STDLIB_FAIL / BOUNDS
 *     instead of being silently truncated;
 *   - after an overlong line the stream stays aligned to the next line;
 *   - EOF is reported once and does not set the error record;
 *   - an oversized length is a BOUNDS error, not a generic invalid argument;
 *   - CRLF and exact-fit lines are handled correctly.
 *
 * Every check is in-process and deterministic.  The program returns 0 only
 * when all checks pass; a bounded loop prevents a hung read_line from hanging
 * the test process (the shell harness also applies `timeout`).
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"

int main(void) {
    error err = error();
    int failures = 0;
    int st;

    /* ---- SUCCESS: ordinary lines through a borrowed stdin --------------
     * Fixture: "alpha\n", a 36-char line, "beta\n".  buf[8] holds 6 chars
     * plus a newline and a NUL, so "alpha\n" fits but the long line does not. */
    file in = file_borrow(stdin);
    char buf[8];

    st = in.read_line(buf, sizeof(buf), &err);
    if (st != STDLIB_OK || strcmp(buf, "alpha") != 0 || !err.ok()) {
        failures++;
        print("FAIL normal-line", st, buf, err.code());
    } else {
        print("ok normal-line", st, buf);
    }

    st = in.read_line(buf, sizeof(buf), &err);
    if (st != STDLIB_FAIL || err.category() != STDLIB_CATEGORY_BOUNDS ||
        buf[0] != '\0') {
        failures++;
        print("FAIL overlong", st, err.category(), buf, err.code());
    } else {
        print("ok overlong", st, err.category(), buf);
    }

    /* proves the remainder of the long line was drained, not left behind */
    st = in.read_line(buf, sizeof(buf), &err);
    if (st != STDLIB_OK || strcmp(buf, "beta") != 0) {
        failures++;
        print("FAIL aligned", st, buf);
    } else {
        print("ok aligned", st, buf);
    }

    st = in.read_line(buf, sizeof(buf), &err);
    if (st != STDLIB_EOF || err.failed_now()) {
        failures++;
        print("FAIL eof", st, err.failed_now());
    } else {
        print("ok eof", st);
    }

    /* ---- RECOVERABLE: one-byte buffer must consume input ---------------- */
    {
        FILE *raw = tmpfile();
        if (raw != NULL) {
            fputs("x\nyy\n", raw);
            rewind(raw);
            file tiny = file_borrow(raw);
            char one[1];
            int calls = 0;
            int bounds = 0;
            int k;
            for (k = 0; k < 8; k++) {
                st = tiny.read_line(one, 1, &err);
                if (st == STDLIB_EOF)
                    break;
                calls++;
                if (st == STDLIB_FAIL &&
                    err.category() == STDLIB_CATEGORY_BOUNDS &&
                    one[0] == '\0')
                    bounds++;
            }
            if (calls != 2 || bounds != 2 || st != STDLIB_EOF) {
                failures++;
                print("FAIL one-byte-progress", calls, bounds, st);
            } else {
                print("ok one-byte-progress", calls, bounds, st);
            }
            /* a borrowed stream is never closed by the wrapper */
            tiny.drop();
            if (fclose(raw) != 0) {
                failures++;
                print("FAIL borrowed-close", 1);
            } else {
                print("ok borrowed-close", 1);
            }
        } else {
            failures++;
            print("FAIL tmpfile-unavailable", 1);
        }
    }

    /* ---- overlong line at physical EOF (no trailing newline) ------------ */
    {
        FILE *r2 = tmpfile();
        if (r2 != NULL) {
            fputs("0123456789", r2);
            rewind(r2);
            file f2 = file_borrow(r2);
            char b2[8]; /* 6 chars + newline + NUL; ten chars cannot fit */
            st = f2.read_line(b2, sizeof(b2), &err);
            if (st != STDLIB_FAIL ||
                err.category() != STDLIB_CATEGORY_BOUNDS || b2[0] != '\0') {
                failures++;
                print("FAIL overlong-eof", st, err.category(), b2);
            } else {
                print("ok overlong-eof", st, err.category(), b2);
            }
            st = f2.read_line(b2, sizeof(b2), &err);
            if (st != STDLIB_EOF) {
                failures++;
                print("FAIL after-overlong-eof", st);
            } else {
                print("ok after-overlong-eof", st);
            }
            fclose(r2);
        } else {
            failures++;
            print("FAIL tmpfile2-unavailable", 1);
        }
    }

    /* ---- exact-fit line: (size-2) chars plus newline, no truncation ----- */
    {
        FILE *r3 = tmpfile();
        if (r3 != NULL) {
            fputs("123456\n", r3);
            rewind(r3);
            file f3 = file_borrow(r3);
            char b3[8];
            st = f3.read_line(b3, sizeof(b3), &err);
            if (st != STDLIB_OK || strcmp(b3, "123456") != 0) {
                failures++;
                print("FAIL exact-fit", st, b3);
            } else {
                print("ok exact-fit", st, b3);
            }
            fclose(r3);
        } else {
            failures++;
            print("FAIL tmpfile3-unavailable", 1);
        }
    }

    /* ---- CRLF is stripped as a single line terminator ------------------ */
    {
        FILE *r4 = tmpfile();
        if (r4 != NULL) {
            fputs("crlf\r\n", r4);
            rewind(r4);
            file f4 = file_borrow(r4);
            char b4[16];
            st = f4.read_line(b4, sizeof(b4), &err);
            if (st != STDLIB_OK || strcmp(b4, "crlf") != 0) {
                failures++;
                print("FAIL crlf", st, b4);
            } else {
                print("ok crlf", st, b4);
            }
            fclose(r4);
        } else {
            failures++;
            print("FAIL tmpfile4-unavailable", 1);
        }
    }

    /* ---- invalid arguments and null handle remain recoverable ---------- */
    {
        file dead = file(NULL, NULL);
        char b5[4];

        st = dead.read_line(b5, sizeof(b5), &err);
        if (st != STDLIB_FAIL || err.category() != STDLIB_CATEGORY_IO) {
            failures++;
            print("FAIL null-handle", st, err.category());
        } else {
            print("ok null-handle", st, err.category());
        }

        st = dead.read_line(NULL, 4, &err);
        if (st != STDLIB_FAIL || err.code() != CPLUS_EXIT_UNSUPPORTED) {
            failures++;
            print("FAIL null-buffer", st, err.code());
        } else {
            print("ok null-buffer", st, err.code());
        }

        st = dead.read_line(b5, 0, &err);
        if (st != STDLIB_FAIL) {
            failures++;
            print("FAIL zero-length", st);
        } else {
            print("ok zero-length", st);
        }

        /* length above INT_MAX cannot be passed to fgets: BOUNDS */
        st = dead.read_line(b5, ((size_t)1 << 31), &err);
        if (st != STDLIB_FAIL || err.category() != STDLIB_CATEGORY_BOUNDS) {
            failures++;
            print("FAIL oversized-length", st, err.category());
        } else {
            print("ok oversized-length", st, err.category());
        }
    }

    print("deep-file-lines", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
