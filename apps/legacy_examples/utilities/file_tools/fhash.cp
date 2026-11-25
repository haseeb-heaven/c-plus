/* fhash.cp -- FNV-1a-32 file hasher with bounded, chunked reads.
 *
 * NOT interactive.  All input comes from fixed argv paths; no clock, network,
 * interactive prompt, user home path or shelling out.  Identical argv and
 * fixture bytes always produce byte-identical stdout.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/utilities/file_tools/fhash.cp \
 *       --strict --sanitize=none -I. \
 *       -o examples/utilities/file_tools/.run/fhash
 *
 * Usage:
 *   fhash <path>
 *
 * The file size is established first with the checked fs_size API.  A file
 * larger than FH_MAX is REFUSED with a bounds error (no partial hash is ever
 * printed).  Reads are issued as fixed-size FH_CHUNK blocks through the
 * checked file::read API; the tail (fewer than FH_CHUNK bytes) is read one
 * byte at a time so the exact byte count is known.  Every byte is folded into
 * FNV-1a.  A directory or other non-regular path is refused as a bad path.
 *
 * A built-in known-answer check (FNV-1a("hello") == 0x4f9f2cab) must pass
 * before any argument is processed.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "stdlib/file.hp"
#include "stdlib/fs.hp"

#define FH_MAX   4096
#define FH_CHUNK 64
#define FH_OFFSET 0x811c9dc5u
#define FH_PRIME  0x01000193u
#define FH_SELF_HASH 0x4f9f2cabu

static const char *fh_arg(char **argv, int i) {
    const char *value = NULL;
    if (i <= 0)
        return NULL;
    __builtin_memcpy(&value, argv + i, sizeof(value));
    return value;
}

static int fh_refuse(const char *reason, int code) {
    printf("FHASH error %s code=%d\n", reason, code);
    return 1;
}

static int fh_usage(void) {
    printf("FHASH error usage\n");
    return 2;
}

static unsigned int fh_fnv_bytes(const char *buf, size_t n, unsigned int h) {
    size_t i;
    for (i = 0; i < n; i++) {
        h ^= (unsigned int)(unsigned char)buf[i];
        h *= FH_PRIME;
    }
    return h;
}

static unsigned int fh_fnv_text(const char *text) {
    string s = string(text);
    unsigned int h = FH_OFFSET;
    if (!s.valid()) {
        s.drop();
        return 0u;
    }
    h = fh_fnv_bytes(s.c_str(), s.size(), h);
    s.drop();
    return h;
}

/* Returns 1 on success; on failure err carries the reason and code. */
static int fh_hash_file(const char *path, unsigned int *out_hash,
                        size_t *out_size, error *err) {
    long long sz;
    size_t size;
    size_t hashed = 0;
    unsigned int h = FH_OFFSET;
    file stream;
    char chunk[FH_CHUNK];
    char one[1];
    int ok = 1;

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
    if (sz > (long long)FH_MAX) {
        error_fail_cat(err, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                       "oversized");
        return 0;
    }
    size = (size_t)sz;

    stream = file(path, "rb");
    if (!stream.is_open()) {
        error_fail(err, CPLUS_EXIT_IO, "cannot-open");
        return 0;
    }
    while (hashed + FH_CHUNK <= size) {
        if (stream.read(chunk, FH_CHUNK, err) != STDLIB_OK) {
            ok = 0;
            break;
        }
        h = fh_fnv_bytes(chunk, FH_CHUNK, h);
        hashed += FH_CHUNK;
    }
    while (ok && hashed < size) {
        if (stream.read(one, 1, err) != STDLIB_OK) {
            ok = 0;
            break;
        }
        h = fh_fnv_bytes(one, 1, h);
        hashed++;
    }
    if (ok && stream.close(err) != STDLIB_OK)
        ok = 0;
    stream.drop();
    if (!ok)
        return 0;
    *out_hash = h;
    *out_size = size;
    return 1;
}

int main(int argc, char **argv) {
    error ferr = error();
    unsigned int hash = 0u;
    size_t size = 0;
    const char *path;

    if (fh_fnv_text("hello") != FH_SELF_HASH)
        return fh_refuse("selfcheck-failed", CPLUS_EXIT_UNSUPPORTED);

    if (argc < 2)
        return fh_usage();
    if (argc != 2) {
        printf("FHASH error bad-args\n");
        return 2;
    }
    path = fh_arg(argv, 1);
    if (path == NULL)
        return fh_refuse("null-input", CPLUS_EXIT_NULL_POINTER);

    if (!fh_hash_file(path, &hash, &size, &ferr))
        return fh_refuse(ferr.what(), ferr.code());

    printf("FHASH %08x %lu\n", hash, (unsigned long)size);
    return 0;
}
