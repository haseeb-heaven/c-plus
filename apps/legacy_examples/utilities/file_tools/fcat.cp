/* fcat.cp -- cat with bounded line output and a byte-count guard.
 *
 * NOT interactive.  The path comes from fixed argv; no clock, network,
 * interactive prompt, user home path or shelling out.  Identical argv and
 * fixture bytes yield byte-identical stdout.
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/legacy_examples/utilities/file_tools/fcat.cp \
 *       --strict --sanitize=none -I. \
 *       -o apps/legacy_examples/utilities/file_tools/.run/fcat
 *
 * Usage:
 *   fcat <path>
 *
 * The exact byte count is taken from the checked fs_size API and refused if it
 * exceeds FC_MAX_BYTES.  Contents are then read with the checked
 * file::read_line API into fixed storage: an overlong line is refused with a
 * bounds error.  All lines are validated before any is printed, so a refused
 * file never leaves a partial transcript.
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "stdlib/file.hp"
#include "stdlib/fs.hp"

#define FC_LINE_CAP  128
#define FC_MAX_LINES 64
#define FC_MAX_BYTES 2048

static char fc_lines[FC_MAX_LINES][FC_LINE_CAP];
static int  fc_nlines;
static long long fc_size;

static const char *fc_arg(char **argv, int i) {
    const char *value = NULL;
    if (i <= 0)
        return NULL;
    __builtin_memcpy(&value, argv + i, sizeof(value));
    return value;
}

static int fc_refuse(const char *reason, int code) {
    printf("FCAT error %s code=%d\n", reason, code);
    return 1;
}

static int fc_usage(void) {
    printf("FCAT error usage\n");
    return 2;
}

static int fc_load(const char *path, error *err) {
    long long sz;
    int n = 0;
    file stream;

    if (path == NULL || strlen(path) == 0) {
        error_fail(err, CPLUS_EXIT_IO, "bad-path");
        return 0;
    }
    if (!fs_exists(path)) {
        error_fail(err, CPLUS_EXIT_IO, "missing-file");
        return 0;
    }
    if (!fs_is_file(path)) {
        error_fail(err, CPLUS_EXIT_IO, "bad-path");
        return 0;
    }
    sz = fs_size(path, err);
    if (sz < 0)
        return 0;
    if (sz > (long long)FC_MAX_BYTES) {
        error_fail_cat(err, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                       "file-too-large");
        return 0;
    }

    stream = file(path, "r");
    if (!stream.is_open()) {
        error_fail(err, CPLUS_EXIT_IO, "cannot-open");
        return 0;
    }
    for (;;) {
        int rc;
        if (n >= FC_MAX_LINES) {
            error_fail_cat(err, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                           "too-many-lines");
            n = -1;
            break;
        }
        rc = stream.read_line(fc_lines[n], FC_LINE_CAP, err);
        if (rc == STDLIB_EOF)
            break;
        if (rc == STDLIB_FAIL) {
            n = -1; /* overlong line: read_line sets CPLUS_EXIT_BOUNDS */
            break;
        }
        if (rc != STDLIB_OK) {
            error_fail(err, CPLUS_EXIT_IO, "read-error");
            n = -1;
            break;
        }
        n++;
    }
    if (n >= 0 && stream.close(err) != STDLIB_OK)
        n = -1;
    stream.drop();
    if (n < 0)
        return 0;
    fc_nlines = n;
    fc_size = sz;
    return 1;
}

int main(int argc, char **argv) {
    error ferr = error();
    const char *path;
    int i;

    if (argc < 2)
        return fc_usage();
    if (argc != 2) {
        printf("FCAT error bad-args\n");
        return 2;
    }
    path = fc_arg(argv, 1);
    if (path == NULL)
        return fc_refuse("null-input", CPLUS_EXIT_NULL_POINTER);

    if (!fc_load(path, &ferr))
        return fc_refuse(ferr.ok() ? "read-error" : ferr.what(), ferr.code());

    /* Hold each bounded line in the checked owned-string API before output. */
    for (i = 0; i < fc_nlines; i++) {
        string line = string(fc_lines[i]);
        const char *text = line.valid() ? line.c_str() : fc_lines[i];
        printf("FCAT %d %s\n", i, text);
        line.drop();
    }
    printf("FCAT size %lld lines %d\n", fc_size, fc_nlines);
    return 0;
}
