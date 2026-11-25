/* ttr.cp -- bounded character transliteration with fixed from/to sets.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/utilities/text_tools/ttr.cp --strict \
 *     --sanitize=none -I. -o examples/utilities/text_tools/.run/ttr
 *
 * Usage:
 *   ttr <path> <from> <to>
 *
 * <from> and <to> must have the same length, strictly less than TR_SET_CAP.
 * Every occurrence of from[k] in a line is replaced by to[k] (first match
 * wins).  The mapping is copied into bounded local arrays; input lines are
 * read with bounded file::read_line, and each transformed line is assembled
 * with the checked string::append API.
 *
 * Output (exact bytes, rc in parentheses):
 *   TTR <line>           (0) one line per input line (possibly transformed)
 *   TTR lines <n>        (0) always printed on success
 *   TTR error usage      (2)
 *   TTR refuse sets      (1) unequal lengths or TR_SET_CAP or more bytes
 *   TTR error resource   (1)
 *   TTR error cannot-open (1)
 *   TTR refuse line-too-long (1)
 *   TTR error read       (1)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "stdlib/file.hp"

#define TR_LINE_CAP 256
#define TR_SET_CAP 64

static char *arg(char **p, size_t i) {
    char *value;
    __builtin_memcpy(&value, p + i, sizeof(value));
    return value;
}

int main(int argc, char **argv) {
    error err = error();
    const char *path;
    const char *from;
    const char *to;
    char from_set[TR_SET_CAP];
    char to_set[TR_SET_CAP];
    int set_len = 0;
    int lines = 0;
    int rc = 0;
    int i;

    if (argc != 4) {
        print("TTR error usage");
        return 2;
    }
    path = arg(argv, 1);
    from = arg(argv, 2);
    to = arg(argv, 3);

    for (;;) {
        char ch;
        __builtin_memcpy(&ch, from + set_len, 1);
        if (ch == '\0')
            break;
        if (set_len >= TR_SET_CAP - 1) {
            print("TTR refuse sets");
            return 1;
        }
        from_set[set_len] = ch;
        set_len++;
    }
    for (i = 0; i < set_len; i++) {
        char ch;
        __builtin_memcpy(&ch, to + i, 1);
        if (ch == '\0') {
            print("TTR refuse sets");
            return 1;
        }
        to_set[i] = ch;
    }
    {
        char ch;
        __builtin_memcpy(&ch, to + set_len, 1);
        if (ch != '\0') {
            print("TTR refuse sets");
            return 1;
        }
    }

    {
        string path_s = string(path);
        if (!path_s.valid()) {
            print("TTR error resource");
            return 1;
        }
        path_s.drop();
    }

    {
        file stream = file(path, "r");
        int rr;
        if (!stream.is_open()) {
            print("TTR error cannot-open");
            return 1;
        }
        for (;;) {
            char line[TR_LINE_CAP];
            int ln;
            int j;
            string out = string("");
            rr = stream.read_line(line, sizeof(line), &err);
            if (rr == STDLIB_EOF) {
                out.drop();
                break;
            }
            if (rr != STDLIB_OK) {
                out.drop();
                if (err.category() == STDLIB_CATEGORY_BOUNDS)
                    print("TTR refuse line-too-long");
                else
                    print("TTR error read");
                rc = 1;
                break;
            }
            if (!out.valid()) {
                out.drop();
                print("TTR error resource");
                rc = 1;
                break;
            }
            ln = (int)strlen(line);
            for (j = 0; j < ln; j++) {
                char mapped = line[j];
                int k;
                char cbuf[2];
                for (k = 0; k < set_len; k++)
                    if (from_set[k] == mapped) {
                        mapped = to_set[k];
                        break;
                    }
                cbuf[0] = mapped;
                cbuf[1] = '\0';
                if (out.append(cbuf) != 0) {
                    out.drop();
                    print("TTR error resource");
                    rc = 1;
                    break;
                }
            }
            if (rc != 0)
                break;
            print("TTR", out.c_str());
            out.drop();
            lines++;
        }
        if (rc == 0)
            print("TTR lines", lines);
        stream.drop();
    }
    return rc;
}
