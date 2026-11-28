/* F-053: bounded read_line over a deterministic stdin fixture containing
 * empty, whitespace, exact-fit, overlong, CRLF and unterminated-EOF lines.
 * The six expected results are asserted on every one of 200 passes (1200
 * read_line calls). */
#include "f100_io_common.h"

int main(void) {
    char path[512], line[F100_LINE_BUF];
    unsigned pass;
    size_t n;
    int i;

    f100_init();
    f100_tmp_path(path, sizeof path, "readline");
    f100_write_line_fixture(path);
    if (freopen(path, "rb", stdin) == NULL) {
        F100_FAIL("cannot attach stdin fixture");
        (void)unlink(path);
        return f100_report("F-053");
    }
    for (pass = 0; pass < 200; ++pass) {
        rewind(stdin);

        n = f100_read_line(line, sizeof line);       /* 1: empty */
        F100_OP();
        if (n != 1 || line[0] != '\n') F100_FAIL("case 1 empty line mismatch");

        n = f100_read_line(line, sizeof line);       /* 2: whitespace + CRLF */
        F100_OP();
        if (n != 6 || line[4] != '\r' || line[5] != '\n')
            F100_FAIL("case 2 whitespace/CRLF mismatch");

        n = f100_read_line(line, sizeof line);       /* 3: exact fit */
        F100_OP();
        /* 31 payload bytes fill the 32-byte buffer; the trailing '\n' is
         * drained by the reader, so the returned length is 31. */
        if (n != F100_LINE_BUF - 1 || line[F100_LINE_BUF - 2] != 'e')
            F100_FAIL("case 3 exact-fit length mismatch");
        for (i = 0; i < (int)n; ++i)
            if (line[i] != 'e') { F100_FAIL("case 3 exact-fit bytes"); break; }

        n = f100_read_line(line, sizeof line);       /* 4: overlong + drain */
        F100_OP();
        if (n != F100_LINE_BUF - 1) F100_FAIL("case 4 overlong length mismatch");
        for (i = 0; i < (int)n; ++i)
            if (line[i] != 'o' && line[i] != ' ') {
                F100_FAIL("case 4 overlong bytes"); break;
            }

        n = f100_read_line(line, sizeof line);       /* 5: CRLF after drain */
        F100_OP();
        if (n != 5 || memcmp(line, "abc\r\n", 5) != 0)
            F100_FAIL("case 5 CRLF mismatch (drain failed?)");

        n = f100_read_line(line, sizeof line);       /* 6: EOF, no newline */
        F100_OP();
        if (n != 15 || memcmp(line, "tail-no-newline", 15) != 0)
            F100_FAIL("case 6 EOF line mismatch");
    }
    (void)unlink(path);
    return f100_report("F-053");
}
