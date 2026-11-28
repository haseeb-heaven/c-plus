/* fsize.cp -- deterministic size listing of fixed paths inside a directory.
 *
 * NOT interactive.  The directory and every relative path come from fixed
 * argv; no clock, network, interactive prompt, user home path or shelling out.
 * No timestamps are read or printed, so identical argv and fixture bytes give
 * byte-identical stdout.
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/legacy_examples/utilities/file_tools/fsize.cp \
 *       --strict --sanitize=none -I. \
 *       -o apps/legacy_examples/utilities/file_tools/.run/fsize
 *
 * Usage:
 *   fsize <dir> <rel1> [<rel2> ...]
 *
 * "<dir>" must be an existing directory and each <rel> must name an existing
 * entry under it.  Relative paths may contain '/', which gives the listing a
 * recursive-ish reach without any directory-scanning API.  Paths longer than
 * the fixed caps, a non-directory "<dir>", a missing entry, or a bad path are
 * all REFUSED with an explicit error code before any listing is printed.
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "stdlib/file.hp"
#include "stdlib/fs.hp"

#define FS_REL_CAP    128
#define FS_PATH_CAP   192
#define FS_MAX_ENTRIES 16

static const char *fs_arg(char **argv, int i) {
    const char *value = NULL;
    if (i <= 0)
        return NULL;
    __builtin_memcpy(&value, argv + i, sizeof(value));
    return value;
}

static int fs_refuse(const char *reason, int code) {
    printf("FSIZE error %s code=%d\n", reason, code);
    return 1;
}

static int fs_usage(void) {
    printf("FSIZE error usage\n");
    return 2;
}

/* Build dir + "/" + rel with the checked owned string API, enforcing both the
 * relative-path and combined-path caps.  Returns 1 on success. */
static int fs_join(const char *dir, const char *rel, string *out, error *err) {
    size_t dlen;
    size_t rlen;
    if (dir == NULL || rel == NULL || strlen(dir) == 0 || strlen(rel) == 0) {
        error_fail(err, CPLUS_EXIT_IO, "bad-path");
        return 0;
    }
    dlen = strlen(dir);
    rlen = strlen(rel);
    if (rlen > FS_REL_CAP || dlen + 1u + rlen > FS_PATH_CAP) {
        error_fail_cat(err, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                       "path-too-long");
        return 0;
    }
    out->clear();
    if (out->append(dir) != 0 || out->append("/") != 0 ||
        out->append(rel) != 0) {
        error_fail(err, CPLUS_EXIT_RESOURCE, "alloc");
        return 0;
    }
    if (!out->valid()) {
        error_fail(err, CPLUS_EXIT_RESOURCE, "alloc");
        return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    error ferr = error();
    string full = string("");
    const char *dir;
    long long sizes[FS_MAX_ENTRIES];
    int n;
    int i;
    long long total = 0;

    if (argc < 3)
        return fs_usage();
    n = argc - 2;
    if (n > FS_MAX_ENTRIES) {
        printf("FSIZE error too-many-entries code=%d\n", CPLUS_EXIT_BOUNDS);
        full.drop();
        return 1;
    }

    dir = fs_arg(argv, 1);
    if (dir == NULL || strlen(dir) == 0) {
        fs_refuse("bad-path", CPLUS_EXIT_IO);
        full.drop();
        return 1;
    }
    if (strlen(dir) > FS_PATH_CAP) {
        fs_refuse("path-too-long", CPLUS_EXIT_BOUNDS);
        full.drop();
        return 1;
    }
    if (!fs_exists(dir)) {
        fs_refuse("missing-dir", CPLUS_EXIT_IO);
        full.drop();
        return 1;
    }
    if (!fs_is_dir(dir)) {
        fs_refuse("not-a-directory", CPLUS_EXIT_IO);
        full.drop();
        return 1;
    }

    /* Validate every entry first; no partial listing on a refused argv. */
    for (i = 0; i < n; i++) {
        const char *rel = fs_arg(argv, i + 2);
        long long sz;
        if (!fs_join(dir, rel, &full, &ferr)) {
            int code = ferr.code();
            full.drop();
            return fs_refuse(ferr.ok() ? "bad-path" : ferr.what(), code);
        }
        if (!fs_exists(full.c_str())) {
            full.drop();
            return fs_refuse("missing-entry", CPLUS_EXIT_IO);
        }
        sz = fs_size(full.c_str(), &ferr);
        if (sz < 0) {
            int code = ferr.code();
            full.drop();
            return fs_refuse(ferr.ok() ? "read-error" : ferr.what(), code);
        }
        sizes[i] = sz;
        total += sz;
    }

    for (i = 0; i < n; i++)
        printf("FSIZE %s %lld\n", fs_arg(argv, i + 2), sizes[i]);
    printf("FSIZE count %d total %lld\n", n, total);
    full.drop();
    return 0;
}
