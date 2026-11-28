/* io_file_empty_large -- Task 3 Phase 2: empty files and large (<=64KiB)
 * files through stdlib/file.hp.
 *
 * Deterministic and bounded: every fixture is a PID-unique named file with an
 * asserted remove(), or a file_temp().  No variable division/modulo, no
 * pointer-parameter subscripts (all such access uses __builtin_memcpy), every
 * out-parameter is initialised, and every local has a unique prefix.
 *
 * Each check prints "<tag> <0|1>" (1 == pass).  The final line is
 * "IO_FILE_EMPTY_LARGE <checks> <failures>"; exit is 0 only when failures==0.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"

static int ifel_checks = 0;
static int ifel_failures = 0;

static void ifel_check(const char *tag, int cond) {
    ifel_checks++;
    if (!cond)
        ifel_failures++;
    print(tag, cond ? 1 : 0);
}

/* Deterministic byte pattern, 1..64, written via __builtin_memcpy so no
 * pointer-parameter subscript appears in the test source. */
static void ifel_pattern(char *dst, size_t n) {
    size_t ifel_k;
    char ifel_byte;
    for (ifel_k = 0; ifel_k < n; ifel_k++) {
        ifel_byte = (char)(1 + (ifel_k & (size_t)63));
        __builtin_memcpy(dst + ifel_k, &ifel_byte, sizeof(ifel_byte));
    }
}

int main(void) {
    error ifel_err = error();
    char ifel_pidtag[24];
    char ifel_path[96];
    char ifel_buf[256];
    char ifel_pat[256];
    int ifel_st;
    long ifel_pos;
    int ifel_round;
    int ifel_all_ok;
    FILE *ifel_probe;

    snprintf(ifel_pidtag, sizeof(ifel_pidtag), "%d", (int)getpid());
    snprintf(ifel_path, sizeof(ifel_path), "cplus_ifel_%s.tmp", ifel_pidtag);
    ifel_pattern(ifel_pat, sizeof(ifel_pat));

    /* ---- 1. empty named file: create, close, reopen, EOF on both readers - */
    {
        file ifel_w = file(ifel_path, "w");
        ifel_check("empty-create", ifel_w.is_open());
        ifel_st = ifel_w.close(&ifel_err);
        ifel_check("empty-close", ifel_st == STDLIB_OK && ifel_err.ok());
    }
    {
        file ifel_r = file(ifel_path, "r");
        ifel_check("empty-reopen", ifel_r.is_open());
        memset(ifel_buf, 0x5A, sizeof(ifel_buf));
        ifel_st = ifel_r.read(ifel_buf, sizeof(ifel_buf), &ifel_err);
        ifel_check("empty-read-eof", ifel_st == STDLIB_EOF && ifel_err.ok());
        ifel_st = ifel_r.read_line(ifel_buf, sizeof(ifel_buf), &ifel_err);
        ifel_check("empty-readline-eof", ifel_st == STDLIB_EOF && ifel_err.ok());
        ifel_st = ifel_r.seek(0, SEEK_END, &ifel_err);
        ifel_pos = ftell(ifel_r.native());
        ifel_check("empty-size-zero", ifel_st == STDLIB_OK && ifel_pos == 0);
    }
    ifel_check("empty-cleanup", remove(ifel_path) == 0);
    ifel_probe = fopen(ifel_path, "r");
    ifel_check("empty-absent", ifel_probe == NULL);
    if (ifel_probe != NULL)
        fclose(ifel_probe);

    /* ---- 2. empty anonymous temp file ----------------------------------- */
    {
        file ifel_t = file_temp(&ifel_err);
        ifel_check("emptytemp-open", ifel_t.is_open() && ifel_err.ok());
        ifel_st = ifel_t.read(ifel_buf, sizeof(ifel_buf), &ifel_err);
        ifel_check("emptytemp-eof", ifel_st == STDLIB_EOF && ifel_err.ok());
        ifel_st = ifel_t.read_line(ifel_buf, sizeof(ifel_buf), &ifel_err);
        ifel_check("emptytemp-line-eof", ifel_st == STDLIB_EOF && ifel_err.ok());
        ifel_t.drop();
    }

    /* ---- 3. one-byte file (small boundary before the large cases) ------- */
    {
        file ifel_one = file_temp(&ifel_err);
        ifel_check("one-open", ifel_one.is_open());
        ifel_check("one-write",
                   ifel_one.write_bytes("Q", 1, &ifel_err) == STDLIB_OK);
        ifel_check("one-seek",
                   ifel_one.seek(0, SEEK_SET, &ifel_err) == STDLIB_OK);
        memset(ifel_buf, 0, sizeof(ifel_buf));
        ifel_st = ifel_one.read(ifel_buf, 1, &ifel_err);
        {
            char ifel_byte;
            __builtin_memcpy(&ifel_byte, ifel_buf, sizeof(ifel_byte));
            ifel_check("one-read", ifel_st == STDLIB_OK && ifel_byte == 'Q');
        }
        ifel_st = ifel_one.read(ifel_buf, 1, &ifel_err);
        ifel_check("one-eof", ifel_st == STDLIB_EOF && ifel_err.ok());
        ifel_one.drop();
    }

    /* ---- 4. large named file: exactly 64KiB, 256-byte chunks ------------ */
    {
        file ifel_w = file(ifel_path, "wb");
        ifel_check("large-open-w", ifel_w.is_open());
        ifel_all_ok = 1;
        for (ifel_round = 0; ifel_round < 256; ifel_round++) {
            if (ifel_w.write_bytes(ifel_pat, 256, &ifel_err) != STDLIB_OK)
                ifel_all_ok = 0;
        }
        ifel_check("large-write-64k", ifel_all_ok && ifel_err.ok());
        ifel_check("large-close-w",
                   ifel_w.close(&ifel_err) == STDLIB_OK && ifel_err.ok());
    }
    {
        file ifel_r = file(ifel_path, "rb");
        ifel_check("large-open-r", ifel_r.is_open());
        ifel_st = ifel_r.seek(0, SEEK_END, &ifel_err);
        ifel_pos = ftell(ifel_r.native());
        ifel_check("large-size-64k",
                   ifel_st == STDLIB_OK && ifel_pos == 65536);
        ifel_check("large-seek-reset",
                   ifel_r.seek(0, SEEK_SET, &ifel_err) == STDLIB_OK);
        ifel_all_ok = 1;
        for (ifel_round = 0; ifel_round < 256; ifel_round++) {
            memset(ifel_buf, 0, sizeof(ifel_buf));
            ifel_st = ifel_r.read(ifel_buf, 256, &ifel_err);
            if (ifel_st != STDLIB_OK || memcmp(ifel_buf, ifel_pat, 256) != 0)
                ifel_all_ok = 0;
        }
        ifel_check("large-read-64k", ifel_all_ok && ifel_err.ok());
        ifel_st = ifel_r.read(ifel_buf, sizeof(ifel_buf), &ifel_err);
        ifel_check("large-eof-after", ifel_st == STDLIB_EOF && ifel_err.ok());
    }
    ifel_check("large-cleanup", remove(ifel_path) == 0);

    /* ---- 5. large named file: 64KiB-1 (non block-aligned) --------------- */
    {
        file ifel_w = file(ifel_path, "wb");
        ifel_check("near-open-w", ifel_w.is_open());
        ifel_all_ok = 1;
        for (ifel_round = 0; ifel_round < 255; ifel_round++) {
            if (ifel_w.write_bytes(ifel_pat, 256, &ifel_err) != STDLIB_OK)
                ifel_all_ok = 0;
        }
        if (ifel_w.write_bytes(ifel_pat, 255, &ifel_err) != STDLIB_OK)
            ifel_all_ok = 0;
        ifel_check("near-write-64k-minus-1", ifel_all_ok && ifel_err.ok());
        ifel_check("near-close-w", ifel_w.close(&ifel_err) == STDLIB_OK);
    }
    {
        file ifel_r = file(ifel_path, "rb");
        ifel_check("near-open-r", ifel_r.is_open());
        ifel_st = ifel_r.seek(0, SEEK_END, &ifel_err);
        ifel_pos = ftell(ifel_r.native());
        ifel_check("near-size", ifel_st == STDLIB_OK && ifel_pos == 65535);
        ifel_check("near-seek-reset",
                   ifel_r.seek(0, SEEK_SET, &ifel_err) == STDLIB_OK);
        ifel_all_ok = 1;
        for (ifel_round = 0; ifel_round < 255; ifel_round++) {
            memset(ifel_buf, 0, sizeof(ifel_buf));
            ifel_st = ifel_r.read(ifel_buf, 256, &ifel_err);
            if (ifel_st != STDLIB_OK || memcmp(ifel_buf, ifel_pat, 256) != 0)
                ifel_all_ok = 0;
        }
        memset(ifel_buf, 0, sizeof(ifel_buf));
        ifel_st = ifel_r.read(ifel_buf, 255, &ifel_err);
        if (ifel_st != STDLIB_OK || memcmp(ifel_buf, ifel_pat, 255) != 0)
            ifel_all_ok = 0;
        ifel_check("near-read-all", ifel_all_ok && ifel_err.ok());
        ifel_st = ifel_r.read(ifel_buf, 1, &ifel_err);
        ifel_check("near-eof-after", ifel_st == STDLIB_EOF && ifel_err.ok());
    }
    ifel_check("near-cleanup", remove(ifel_path) == 0);
    ifel_probe = fopen(ifel_path, "r");
    ifel_check("near-absent", ifel_probe == NULL);
    if (ifel_probe != NULL)
        fclose(ifel_probe);

    print("IO_FILE_EMPTY_LARGE", ifel_checks, ifel_failures);
    return ifel_failures == 0 ? 0 : 1;
}
