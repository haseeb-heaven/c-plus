/* file_tools.cp -- deterministic file utilities with bounds-checked reads.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/expanded/cli/file_tools.cp --strict --sanitize=none -I. \
 *       -o examples/expanded/cli/.run/file_tools
 *
 * Usage:
 *   file_tools size <path>
 *   file_tools copy <src> <dst>
 *   file_tools cmp  <a> <b>
 *
 * Every read goes through the checked file::read API with an error* result.
 * A file that fits the fixed buffer exactly is accepted ("exact fit"); a file
 * one byte larger is refused with a bounds error ("one-over refusal") instead
 * of being silently truncated.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/file.hp"

#define FT_CAP 1024

static char *ft_arg(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

static void ft_error(const char *what) {
    print("FILETOOLS error", what);
}

/* Read up to `cap` bytes through checked one-byte reads, then probe one more
 * byte. Exact fit => STDLIB_EOF on the probe (accept); one more byte =>
 * STDLIB_OK (refuse). Returns 1 on success. */
static int ft_read_bounded(const char *path, char *buf, size_t cap,
                           size_t *out_len, error *err) {
    file stream = file(path, "r");
    size_t n = 0;
    int ok = 1;
    int rc;
    char one[1];

    if (!stream.is_open()) {
        error_fail(err, CPLUS_EXIT_IO, "cannot-open");
        return 0;
    }
    while (n < cap) {
        rc = stream.read(one, 1, err);
        if (rc == STDLIB_EOF)
            break;
        if (rc != STDLIB_OK) {
            ok = 0;
            break;
        }
        __builtin_memcpy(buf + n, one, 1);
        n++;
    }
    if (ok) {
        rc = stream.read(one, 1, err);
        if (rc == STDLIB_OK) {
            error_fail_cat(err, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                           "one-over");
            ok = 0;
        } else if (rc != STDLIB_EOF) {
            ok = 0;
        }
    }
    if (ok && stream.close(err) != STDLIB_OK)
        ok = 0;
    stream.drop();
    if (ok)
        *out_len = n;
    return ok;
}

int main(int argc, char **argv) {
    error ferr = error();
    char buf_a[FT_CAP];
    char buf_b[FT_CAP];
    size_t len_a = 0;
    size_t len_b = 0;
    const char *cmd;
    int rc = 0;

    if (argc < 3) {
        ft_error("usage");
        return 2;
    }
    cmd = ft_arg(argv, 1);

    if (strcmp(cmd, "size") == 0) {
        if (argc != 3) {
            ft_error("bad-args");
            return 1;
        }
        if (!ft_read_bounded(ft_arg(argv, 2), buf_a, FT_CAP, &len_a, &ferr)) {
            ft_error(ferr.what());
            return 1;
        }
        print("FILETOOLS size", (int)len_a);
    } else if (strcmp(cmd, "copy") == 0) {
        file out;
        if (argc != 4) {
            ft_error("bad-args");
            return 1;
        }
        if (!ft_read_bounded(ft_arg(argv, 2), buf_a, FT_CAP, &len_a, &ferr)) {
            ft_error(ferr.what());
            return 1;
        }
        out = file(ft_arg(argv, 3), "w");
        if (!out.is_open()) {
            error_fail(&ferr, CPLUS_EXIT_IO, "cannot-open-dst");
            ft_error(ferr.what());
            return 1;
        }
        if (out.write_bytes(buf_a, len_a, &ferr) != STDLIB_OK) {
            ft_error(ferr.what());
            rc = 1;
        } else if (out.close(&ferr) != STDLIB_OK) {
            ft_error(ferr.what());
            rc = 1;
        } else {
            print("FILETOOLS copy", (int)len_a);
        }
        out.drop();
    } else if (strcmp(cmd, "cmp") == 0) {
        int equal = 0;
        if (argc != 4) {
            ft_error("bad-args");
            return 1;
        }
        if (!ft_read_bounded(ft_arg(argv, 2), buf_a, FT_CAP, &len_a, &ferr) ||
            !ft_read_bounded(ft_arg(argv, 3), buf_b, FT_CAP, &len_b, &ferr)) {
            ft_error(ferr.what());
            return 1;
        }
        if (len_a == len_b && memcmp(buf_a, buf_b, len_a) == 0)
            equal = 1;
        print("FILETOOLS cmp", equal);
    } else {
        ft_error("unknown-command");
        rc = 1;
    }
    return rc;
}
