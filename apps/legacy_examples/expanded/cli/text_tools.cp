/* text_tools.cp -- deterministic text utilities built on the C+ stdlib.
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/legacy_examples/expanded/cli/text_tools.cp --strict --sanitize=none -I. \
 *       -o apps/legacy_examples/expanded/cli/.run/text_tools
 *
 * Usage:
 *   text_tools <path> words
 *   text_tools <path> lines
 *   text_tools <path> rev
 *   text_tools <path> find <needle>
 *
 * The input file path is a fixed argv argument; reading uses the checked
 * file::read_line API with an error* out-parameter. Lines are held in bounded
 * storage and their lengths in a stdlib vector read back with at_checked.
 * Substring search uses the checked string::find API.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"
#include "stdlib/file.hp"

#define TT_MAX_LINES 128
#define TT_LINE_CAP  256

static char tt_lines[TT_MAX_LINES][TT_LINE_CAP];
static int  tt_nlines;

static char *tt_arg(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

static void tt_error(const char *what) {
    print("TEXT error", what);
}

/* Read the whole file through checked file::read_line; report every failure
 * through err. Returns 1 on success. */
static int tt_read(const char *path, vector *lens, error *err) {
    file stream = file(path, "r");
    int ok = 1;
    if (!stream.is_open()) {
        error_fail(err, CPLUS_EXIT_IO, "cannot-open");
        return 0;
    }
    tt_nlines = 0;
    for (;;) {
        int rr;
        if (tt_nlines >= TT_MAX_LINES) {
            error_fail_cat(err, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                           "too-many-lines");
            ok = 0;
            break;
        }
        rr = stream.read_line(tt_lines[tt_nlines], TT_LINE_CAP, err);
        if (rr == STDLIB_EOF)
            break;
        if (rr != STDLIB_OK) {
            ok = 0;
            break;
        }
        if (lens->push_back((int)strlen(tt_lines[tt_nlines])) != 0) {
            error_fail(err, CPLUS_EXIT_RESOURCE, "alloc");
            ok = 0;
            break;
        }
        tt_nlines++;
    }
    if (ok && stream.close(err) != STDLIB_OK)
        ok = 0;
    stream.drop();
    return ok;
}

static int tt_words(void) {
    int total = 0;
    int i;
    for (i = 0; i < tt_nlines; i++) {
        int in_word = 0;
        size_t j;
        size_t n = strlen(tt_lines[i]);
        for (j = 0; j < n; j++) {
            unsigned char c = (unsigned char)tt_lines[i][j];
            if (isspace(c)) {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                total++;
            }
        }
    }
    return total;
}

static int tt_find(const char *needle, int *hits) {
    int i;
    int found = 0;
    for (i = 0; i < tt_nlines; i++) {
        string line = string(tt_lines[i]);
        long at;
        if (!line.valid()) {
            line.drop();
            continue;
        }
        at = line.find(needle);
        if (at >= 0) {
            print("TEXT find", i, at, tt_lines[i]);
            found++;
        }
        line.drop();
    }
    *hits = found;
    return found;
}

int main(int argc, char **argv) {
    vector tt_lens = vector();
    error terr = error();
    const char *path;
    const char *cmd;
    int rc = 0;

    if (argc < 3) {
        tt_error("usage");
        return 2;
    }
    path = tt_arg(argv, 1);
    cmd = tt_arg(argv, 2);

    if (!tt_read(path, &tt_lens, &terr)) {
        tt_error(terr.what());
        tt_lens.drop();
        return 1;
    }

    if (strcmp(cmd, "words") == 0) {
        if (argc != 3) {
            tt_error("bad-args");
            rc = 1;
        } else {
            print("TEXT words", tt_words());
        }
    } else if (strcmp(cmd, "lines") == 0) {
        if (argc != 3) {
            tt_error("bad-args");
            rc = 1;
        } else {
            int check = 0;
            int i;
            for (i = 0; i < tt_nlines; i++) {
                int len = 0;
                if (tt_lens.at_checked((size_t)i, &len) == 0 &&
                    len == (int)strlen(tt_lines[i]))
                    check++;
            }
            print("TEXT lines", tt_nlines, check);
        }
    } else if (strcmp(cmd, "rev") == 0) {
        if (argc != 3) {
            tt_error("bad-args");
            rc = 1;
        } else {
            int i;
            for (i = tt_nlines - 1; i >= 0; i--) {
                int len = 0;
                if (tt_lens.at_checked((size_t)i, &len) != 0)
                    continue;
                print("TEXT rev", i, tt_lines[i]);
            }
        }
    } else if (strcmp(cmd, "find") == 0) {
        int hits = 0;
        if (argc != 4) {
            tt_error("bad-args");
            rc = 1;
        } else {
            tt_find(tt_arg(argv, 3), &hits);
            print("TEXT find hits", hits);
        }
    } else {
        tt_error("unknown-command");
        rc = 1;
    }

    tt_lens.drop();
    return rc;
}
