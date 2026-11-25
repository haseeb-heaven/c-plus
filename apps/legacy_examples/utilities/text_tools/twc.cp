/* twc.cp -- bounded, deterministic wc-like word/line/byte counter.
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/legacy_examples/utilities/text_tools/twc.cp --strict \
 *     --sanitize=none -I. -o apps/legacy_examples/utilities/text_tools/.run/twc
 *
 * Usage:
 *   twc <path>
 *
 * Safety contract (every bound is a compile-time constant):
 *   - The file is streamed one byte at a time through file::read with an
 *     error* out-parameter; nothing is indexed from an unbounded pointer.
 *   - At most TWC_MAX_BYTES bytes are accepted.  The moment a further byte
 *     exists the tool prints a refusal and exits non-zero; no counter wraps.
 *   - The path is first handed to the checked string constructor, so an
 *     allocation failure is reported instead of being dereferenced.
 *
 * Output (stdout, exact bytes, rc shown in parentheses):
 *   TWC <lines> <words> <chars>   (0)
 *   TWC error usage               (2)  argc != 2
 *   TWC error cannot-open         (1)
 *   TWC error resource            (1)
 *   TWC error read                (1)
 *   TWC error close               (1)
 *   TWC refuse oversize           (1)  file has more than TWC_MAX_BYTES bytes
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "stdlib/file.hp"

#define TWC_MAX_BYTES 65536

static char *arg(char **p, size_t i) {
    char *value;
    __builtin_memcpy(&value, p + i, sizeof(value));
    return value;
}

int main(int argc, char **argv) {
    error err = error();
    const char *path;
    char one[1];
    int total = 0;
    int lines = 0;
    int words = 0;
    int inword = 0;
    int last = '\n';
    int rc = 0;

    if (argc != 2) {
        print("TWC error usage");
        return 2;
    }
    path = arg(argv, 1);

    /* Checked-string validation of the argv path. */
    {
        string path_s = string(path);
        if (!path_s.valid()) {
            print("TWC error resource");
            return 1;
        }
        path_s.drop();
    }

    {
        file stream = file(path, "r");
        int rr;
        if (!stream.is_open()) {
            print("TWC error cannot-open");
            return 1;
        }
        for (;;) {
            if (total >= TWC_MAX_BYTES) {
                /* One extra byte proves the input is oversized. */
                rr = stream.read(one, 1, &err);
                if (rr == STDLIB_EOF)
                    break;
                if (rr != STDLIB_OK) {
                    print("TWC error read");
                    rc = 1;
                    break;
                }
                print("TWC refuse oversize");
                rc = 1;
                break;
            }
            rr = stream.read(one, 1, &err);
            if (rr == STDLIB_EOF)
                break;
            if (rr != STDLIB_OK) {
                print("TWC error read");
                rc = 1;
                break;
            }
            {
                unsigned char c = (unsigned char)one[0];
                total++;
                if (c == '\n')
                    lines++;
                if (isspace(c)) {
                    inword = 0;
                } else if (!inword) {
                    words++;
                    inword = 1;
                }
                last = (int)c;
            }
        }
        if (stream.close(&err) != STDLIB_OK && rc == 0) {
            print("TWC error close");
            rc = 1;
        }
        if (rc == 0) {
            if (total > 0 && last != '\n')
                lines++; /* unterminated final line */
            print("TWC", lines, words, total);
        }
        stream.drop(); /* close() already cleared the handle: safe no-op */
    }
    return rc;
}
