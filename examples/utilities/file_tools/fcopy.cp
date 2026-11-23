/* fcopy.cp -- bounded file copy with exact-fit / one-over checks.
 *
 * NOT interactive.  Both paths come from fixed argv; no clock, network,
 * interactive prompt, user home path or shelling out.  Identical argv and
 * fixture bytes yield byte-identical stdout.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/utilities/file_tools/fcopy.cp \
 *       --strict --sanitize=none -I. \
 *       -o examples/utilities/file_tools/.run/fcopy
 *
 * Usage:
 *   fcopy <src> <dst>
 *
 * The source is preflighted with the checked fs_size API.  A source of exactly
 * FC_CAP bytes is an exact fit and is accepted; a source of FC_CAP + 1 bytes
 * is REFUSED as "oversized" before any write occurs.  Reads use fixed-size
 * FC_CHUNK blocks through file::read with a one-byte tail for the exact count.
 * The destination is rejected when it is a directory, an empty path or an
 * overlong path.  After writing, the destination size must equal the source
 * size exactly (a one-over destination is refused) and its bytes are read back
 * and compared; only then is success printed.
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "stdlib/file.hp"
#include "stdlib/fs.hp"

#define FC_CAP      1024
#define FC_CHUNK    32
#define FC_PATH_CAP 192

static const char *fc_arg(char **argv, int i) {
    const char *value = NULL;
    if (i <= 0)
        return NULL;
    __builtin_memcpy(&value, argv + i, sizeof(value));
    return value;
}

static int fc_refuse(const char *reason, int code) {
    printf("FCOPY error %s code=%d\n", reason, code);
    return 1;
}

static int fc_usage(void) {
    printf("FCOPY error usage\n");
    return 2;
}

/* Read exactly `want` bytes (want <= FC_CAP) from path into buf. */
static int fc_read(const char *path, char *buf, size_t want, error *err) {
    file stream;
    size_t have = 0;
    char chunk[FC_CHUNK];
    char one[1];

    stream = file(path, "rb");
    if (!stream.is_open()) {
        error_fail(err, CPLUS_EXIT_IO, "cannot-open");
        return 0;
    }
    while (have + FC_CHUNK <= want) {
        if (stream.read(chunk, FC_CHUNK, err) != STDLIB_OK) {
            stream.drop();
            return 0;
        }
        __builtin_memcpy(buf + have, chunk, FC_CHUNK);
        have += FC_CHUNK;
    }
    while (have < want) {
        if (stream.read(one, 1, err) != STDLIB_OK) {
            stream.drop();
            return 0;
        }
        __builtin_memcpy(buf + have, one, 1);
        have++;
    }
    if (stream.close(err) != STDLIB_OK) {
        stream.drop();
        return 0;
    }
    stream.drop();
    return 1;
}

int main(int argc, char **argv) {
    error ferr = error();
    char src_buf[FC_CAP];
    char dst_buf[FC_CAP];
    const char *src;
    const char *dst;
    long long sz;
    size_t size;
    file out;
    int ok = 1;

    if (argc < 3)
        return fc_usage();
    if (argc != 3) {
        printf("FCOPY error bad-args\n");
        return 2;
    }
    src = fc_arg(argv, 1);
    dst = fc_arg(argv, 2);
    if (src == NULL || dst == NULL)
        return fc_refuse("null-input", CPLUS_EXIT_NULL_POINTER);

    /* Source preflight: present, regular, and at most the exact-fit cap. */
    if (src == NULL || strlen(src) == 0)
        return fc_refuse("bad-path", CPLUS_EXIT_IO);
    if (!fs_exists(src))
        return fc_refuse("missing-file", CPLUS_EXIT_IO);
    if (!fs_is_file(src))
        return fc_refuse("bad-path", CPLUS_EXIT_IO);
    sz = fs_size(src, &ferr);
    if (sz < 0)
        return fc_refuse(ferr.ok() ? "read-error" : ferr.what(), ferr.code());
    if (sz > (long long)FC_CAP)
        return fc_refuse("oversized", CPLUS_EXIT_BOUNDS);
    size = (size_t)sz;

    /* Destination preflight: length via the checked owned-string API. */
    {
        string dst_path = string(dst);
        size_t dlen = dst_path.valid() ? dst_path.size() : 0;
        dst_path.drop();
        if (dlen == 0 || dlen > FC_PATH_CAP)
            return fc_refuse("bad-destination", CPLUS_EXIT_IO);
    }
    if (fs_exists(dst) && !fs_is_file(dst))
        return fc_refuse("bad-destination", CPLUS_EXIT_IO);

    if (!fc_read(src, src_buf, size, &ferr))
        return fc_refuse(ferr.ok() ? "read-error" : ferr.what(), ferr.code());

    out = file(dst, "wb");
    if (!out.is_open())
        return fc_refuse("cannot-open-destination", CPLUS_EXIT_IO);
    if (out.write_bytes(src_buf, size, &ferr) != STDLIB_OK)
        ok = 0;
    if (ok && out.close(&ferr) != STDLIB_OK)
        ok = 0;
    out.drop();
    if (!ok)
        return fc_refuse(ferr.ok() ? "write-error" : ferr.what(), ferr.code());

    /* Verify: exact destination size, then byte-exact contents. */
    if (!fs_exists(dst))
        return fc_refuse("destination-missing", CPLUS_EXIT_IO);
    sz = fs_size(dst, &ferr);
    if (sz < 0)
        return fc_refuse(ferr.ok() ? "read-error" : ferr.what(), ferr.code());
    if (sz != (long long)size)
        return fc_refuse("destination-one-over", CPLUS_EXIT_BOUNDS);
    if (!fc_read(dst, dst_buf, size, &ferr))
        return fc_refuse(ferr.ok() ? "read-error" : ferr.what(), ferr.code());
    if (memcmp(src_buf, dst_buf, size) != 0)
        return fc_refuse("verify-mismatch", CPLUS_EXIT_IO);

    printf("FCOPY copied %lu\n", (unsigned long)size);
    printf("FCOPY verified %lu\n", (unsigned long)size);
    return 0;
}
