/* fdiff.cp -- bounded, line-by-line unified-style diff of two files.
 *
 * NOT interactive.  Both paths come from fixed argv.  No clock, network,
 * interactive prompt, user home path or shelling out; identical argv and
 * fixture bytes yield byte-identical stdout.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/utilities/file_tools/fdiff.cp \
 *       --strict --sanitize=none -I. \
 *       -o examples/utilities/file_tools/.run/fdiff
 *
 * Usage:
 *   fdiff <a> <b>
 *
 * Each file is read through the checked file::read_line API into fixed
 * storage.  A file larger than FD_MAX_BYTES, a line longer than FD_LINE_CAP,
 * or more than FD_MAX_LINES lines is REFUSED with a bounds error before any
 * diff is printed.  The diff is index-aligned (not an LCS) and uses fixed
 * labels "a" and "b" so output never depends on the supplied path strings.
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "stdlib/file.hp"
#include "stdlib/fs.hp"

#define FD_LINE_CAP   64
#define FD_MAX_LINES  32
#define FD_MAX_BYTES  2048

static char fd_a[FD_MAX_LINES][FD_LINE_CAP];
static char fd_b[FD_MAX_LINES][FD_LINE_CAP];
static int  fd_na;
static int  fd_nb;

static const char *fd_arg(char **argv, int i) {
    const char *value = NULL;
    if (i <= 0)
        return NULL;
    __builtin_memcpy(&value, argv + i, sizeof(value));
    return value;
}

static int fd_refuse(const char *reason, int code) {
    printf("FDIFF error %s code=%d\n", reason, code);
    return 1;
}

static int fd_usage(void) {
    printf("FDIFF error usage\n");
    return 2;
}

/* Preflight: regular file, present, not larger than the byte guard. */
static int fd_preflight(const char *path, error *err) {
    long long sz;
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
    if (sz > (long long)FD_MAX_BYTES) {
        error_fail_cat(err, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                       "file-too-large");
        return 0;
    }
    return 1;
}

/* Each loader touches its own fixed global directly: passing a 2-D array as a
 * parameter would lose the provable extent required by --strict. */
static int fd_load_a(const char *path, error *err) {
    file stream;
    int n = 0;
    if (!fd_preflight(path, err))
        return 0;
    stream = file(path, "r");
    if (!stream.is_open()) {
        error_fail(err, CPLUS_EXIT_IO, "cannot-open");
        return 0;
    }
    for (;;) {
        int rc;
        if (n >= FD_MAX_LINES) {
            error_fail_cat(err, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                           "too-many-lines");
            n = -1;
            break;
        }
        rc = stream.read_line(fd_a[n], FD_LINE_CAP, err);
        if (rc == STDLIB_EOF)
            break;
        if (rc == STDLIB_FAIL) {
            n = -1; /* read_line set CPLUS_EXIT_BOUNDS for an overlong line */
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
    fd_na = n;
    return 1;
}

static int fd_load_b(const char *path, error *err) {
    file stream;
    int n = 0;
    if (!fd_preflight(path, err))
        return 0;
    stream = file(path, "r");
    if (!stream.is_open()) {
        error_fail(err, CPLUS_EXIT_IO, "cannot-open");
        return 0;
    }
    for (;;) {
        int rc;
        if (n >= FD_MAX_LINES) {
            error_fail_cat(err, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                           "too-many-lines");
            n = -1;
            break;
        }
        rc = stream.read_line(fd_b[n], FD_LINE_CAP, err);
        if (rc == STDLIB_EOF)
            break;
        if (rc == STDLIB_FAIL) {
            n = -1;
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
    fd_nb = n;
    return 1;
}

static void fd_emit(void) {
    int i;
    int max = fd_na > fd_nb ? fd_na : fd_nb;
    int changes = 0;

    printf("FDiff --- a\n");
    printf("FDiff +++ b\n");
    printf("FDiff @@ -%d +%d @@\n", fd_na, fd_nb);
    for (i = 0; i < max; i++) {
        if (i < fd_na && i < fd_nb) {
            /* Compare through the checked owned-string API, not raw strcmp. */
            string sa = string(fd_a[i]);
            string sb = string(fd_b[i]);
            int same = sa.valid() && sb.valid() && sa.equals(sb.c_str());
            sa.drop();
            sb.drop();
            if (same) {
                printf("FDiff  %s\n", fd_a[i]);
            } else {
                printf("FDiff -%s\n", fd_a[i]);
                printf("FDiff +%s\n", fd_b[i]);
                changes += 2;
            }
        } else if (i < fd_na) {
            printf("FDiff -%s\n", fd_a[i]);
            changes++;
        } else {
            printf("FDiff +%s\n", fd_b[i]);
            changes++;
        }
    }
    printf("FDiff changes %d\n", changes);
}

int main(int argc, char **argv) {
    error ferr = error();
    const char *pa;
    const char *pb;

    if (argc < 3)
        return fd_usage();
    if (argc != 3) {
        printf("FDIFF error bad-args\n");
        return 2;
    }
    pa = fd_arg(argv, 1);
    pb = fd_arg(argv, 2);
    if (pa == NULL || pb == NULL)
        return fd_refuse("null-input", CPLUS_EXIT_NULL_POINTER);

    if (!fd_load_a(pa, &ferr))
        return fd_refuse(ferr.ok() ? "read-error" : ferr.what(), ferr.code());
    if (!fd_load_b(pb, &ferr))
        return fd_refuse(ferr.ok() ? "read-error" : ferr.what(), ferr.code());

    fd_emit();
    return 0;
}
