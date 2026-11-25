/* tgrep.cp -- bounded substring / regex-lite line search.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/utilities/text_tools/tgrep.cp --strict \
 *     --sanitize=none -I. -o examples/utilities/text_tools/.run/tgrep
 *
 * Usage:
 *   tgrep <path> <pattern>
 *
 * The pattern is a fixed argv string.  With no metacharacters it is a plain
 * substring search performed by the checked string::find API.  The two
 * metacharacters '.' (any single byte) and '*' (zero or more of the preceding
 * byte) enable a small regex-lite match, searched unanchored on every line.
 *
 * Safety contract:
 *   - Pattern is copied into a bounded local array; TG_PAT_CAP or more bytes
 *     are REFUSED before any further work.
 *   - Lines are read through file::read_line into a bounded local array.  A
 *     line that does not fit yields a bounds error which is REFUSED.
 *   - No pointer indexing outside local arrays with known extents.
 *
 * Output (exact bytes, rc in parentheses):
 *   TGREP <index> <line>       (0) one line per match (index is 0-based)
 *   TGREP matches <n>          (0) always printed on success
 *   TGREP error usage          (2)
 *   TGREP error cannot-open    (1)
 *   TGREP refuse pattern       (1)
 *   TGREP refuse line-too-long (1)
 *   TGREP error read           (1)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "stdlib/file.hp"

#define TG_LINE_CAP 256
#define TG_PAT_CAP 64

/* Read one byte from a borrowed buffer.  __builtin_memcpy keeps the strict
 * FFI extent checker satisfied (no subscript on a pointer parameter). */
static char byte_at(const char *p, int i) {
    char c;
    __builtin_memcpy(&c, p + i, 1);
    return c;
}

/* Full wildcard match of pattern[0..pn) against text[0..sn). */
static int glob_full(const char *pat, int pn, const char *text, int sn) {
    int pi = 0;
    int si = 0;
    int star = -1;
    int mark = 0;
    while (si < sn) {
        char pc = pi < pn ? byte_at(pat, pi) : '\0';
        if (pi < pn && (pc == '.' || pc == byte_at(text, si))) {
            pi++;
            si++;
        } else if (pi < pn && pc == '*') {
            star = pi;
            pi++;
            mark = si;
        } else if (star >= 0) {
            pi = star + 1;
            si = ++mark;
        } else {
            return 0;
        }
    }
    while (pi < pn && byte_at(pat, pi) == '*')
        pi++;
    return pi == pn;
}

/* Unanchored search: try every start offset, including the empty suffix. */
static int glob_search(const char *pat, int pn, const char *line, int ln) {
    int start;
    for (start = 0; start <= ln; start++)
        if (glob_full(pat, pn, line + start, ln - start))
            return 1;
    return 0;
}

static char *arg(char **p, size_t i) {
    char *value;
    __builtin_memcpy(&value, p + i, sizeof(value));
    return value;
}

int main(int argc, char **argv) {
    error err = error();
    const char *path;
    const char *pat_src;
    char pat[TG_PAT_CAP];
    int pn = 0;
    int has_meta = 0;
    int idx = 0;
    int matches = 0;
    int rc = 0;
    int i;

    if (argc != 3) {
        print("TGREP error usage");
        return 2;
    }
    path = arg(argv, 1);
    pat_src = arg(argv, 2);

    /* Bounded copy of the pattern; the NUL terminator is read only after the
     * length bound has been checked on every preceding byte. */
    for (;;) {
        char ch;
        __builtin_memcpy(&ch, pat_src + pn, 1);
        if (ch == '\0')
            break;
        if (pn >= TG_PAT_CAP - 1) {
            print("TGREP refuse pattern");
            return 1;
        }
        pat[pn] = ch;
        pn++;
    }
    pat[pn] = '\0';
    for (i = 0; i < pn; i++)
        if (pat[i] == '.' || pat[i] == '*')
            has_meta = 1;

    {
        string path_s = string(path);
        if (!path_s.valid()) {
            print("TGREP error resource");
            return 1;
        }
        path_s.drop();
    }

    {
        file stream = file(path, "r");
        int rr;
        if (!stream.is_open()) {
            print("TGREP error cannot-open");
            return 1;
        }
        for (;;) {
            char line[TG_LINE_CAP];
            int ln;
            int hit = 0;
            rr = stream.read_line(line, sizeof(line), &err);
            if (rr == STDLIB_EOF)
                break;
            if (rr != STDLIB_OK) {
                if (err.category() == STDLIB_CATEGORY_BOUNDS)
                    print("TGREP refuse line-too-long");
                else
                    print("TGREP error read");
                rc = 1;
                break;
            }
            ln = (int)strlen(line);
            if (has_meta) {
                hit = glob_search(pat, pn, line, ln);
            } else {
                string ls = string(line);
                if (!ls.valid()) {
                    print("TGREP error resource");
                    rc = 1;
                    ls.drop();
                    break;
                }
                if (ls.find(pat) >= 0)
                    hit = 1;
                ls.drop();
            }
            if (hit) {
                print("TGREP", idx, line);
                matches++;
            }
            idx++;
        }
        if (rc == 0)
            print("TGREP matches", matches);
        stream.drop();
    }
    return rc;
}
