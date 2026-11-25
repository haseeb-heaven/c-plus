/* thead.cp -- bounded "first N lines" utility.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/utilities/text_tools/thead.cp --strict \
 *     --sanitize=none -I. -o examples/utilities/text_tools/.run/thead
 *
 * Usage:
 *   thead <path> <N>
 *
 * Safety contract:
 *   - N is parsed with strtol and must be an integer in [1, TH_MAX_N];
 *     negative, zero, oversized, truncated and non-numeric values are REFUSED.
 *   - Lines are read through file::read_line into a bounded local array; an
 *     overlong line raises a bounds error that is REFUSED.
 *   - Each emitted line is wrapped in a checked string before printing.
 *
 * Output (exact bytes, rc in parentheses):
 *   THEAD <line-index> <line>  (0) one line per emitted line (0-based)
 *   THEAD lines <n>            (0) always printed on success
 *   THEAD error usage          (2)
 *   THEAD refuse N             (1) invalid or out-of-range N
 *   THEAD error cannot-open    (1)
 *   THEAD error resource       (1)
 *   THEAD refuse line-too-long (1)
 *   THEAD error read           (1)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "stdlib/file.hp"

#define TH_LINE_CAP 256
#define TH_MAX_N 1000

static char *arg(char **p, size_t i) {
    char *value;
    __builtin_memcpy(&value, p + i, sizeof(value));
    return value;
}

/* Bounded decimal parser: no strtol (the strict checker cannot model the
 * out-parameter it writes), no unbounded accumulation, no library locale. */
static int parse_n(const char *text, int *out) {
    int i = 0;
    int neg = 0;
    int v = 0;
    char ch;
    if (text == NULL)
        return 0;
    __builtin_memcpy(&ch, text, 1);
    if (ch == '-') {
        neg = 1;
        i++;
    } else if (ch == '+') {
        i++;
    }
    for (;;) {
        __builtin_memcpy(&ch, text + i, 1);
        if (ch < '0' || ch > '9')
            break;
        if (v > TH_MAX_N)
            return 0; /* early bound keeps v * 10 + 9 far below INT_MAX */
        v = v * 10 + (ch - '0');
        i++;
    }
    __builtin_memcpy(&ch, text + i, 1);
    if (ch != '\0')
        return 0;
    if (neg)
        v = -v;
    if (v < 1 || v > TH_MAX_N)
        return 0;
    *out = v;
    return 1;
}

int main(int argc, char **argv) {
    error err = error();
    const char *path;
    int want = 0;
    int printed = 0;
    int rc = 0;

    if (argc != 3) {
        print("THEAD error usage");
        return 2;
    }
    path = arg(argv, 1);
    if (!parse_n(arg(argv, 2), &want)) {
        print("THEAD refuse N");
        return 1;
    }
    {
        string path_s = string(path);
        if (!path_s.valid()) {
            print("THEAD error resource");
            return 1;
        }
        path_s.drop();
    }

    {
        file stream = file(path, "r");
        int rr;
        if (!stream.is_open()) {
            print("THEAD error cannot-open");
            return 1;
        }
        while (printed < want) {
            char line[TH_LINE_CAP];
            rr = stream.read_line(line, sizeof(line), &err);
            if (rr == STDLIB_EOF)
                break;
            if (rr != STDLIB_OK) {
                if (err.category() == STDLIB_CATEGORY_BOUNDS)
                    print("THEAD refuse line-too-long");
                else
                    print("THEAD error read");
                rc = 1;
                break;
            }
            {
                string ls = string(line);
                if (!ls.valid()) {
                    print("THEAD error resource");
                    rc = 1;
                    ls.drop();
                    break;
                }
                print("THEAD", printed, ls.c_str());
                ls.drop();
            }
            printed++;
        }
        if (rc == 0)
            print("THEAD lines", printed);
        stream.drop();
    }
    return rc;
}
