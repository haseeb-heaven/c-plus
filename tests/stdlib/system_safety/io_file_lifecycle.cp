/* io_file_lifecycle -- Task 3 Phase 2: seek/flush failures, repeated
 * close/drop, borrowed FILE* ownership, and cleanup after failed init.
 *
 * The borrowed-handle checks are observable: the raw FILE* is probed with
 * fgetc/ftell after the wrapper's drop(), destructor, and explicit close(),
 * then closed by the test itself.  fd-count probes use /dev/fd.
 *
 * Deterministic and bounded.  No variable division/modulo or pointer-parameter
 * subscripts; out-params initialised; unique prefixes.  Each check prints
 * "<tag> <0|1>"; final line is "IO_FILE_LIFECYCLE <checks> <failures>"; exit 0
 * only when failures==0.
 */
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"

static int iflc_checks = 0;
static int iflc_failures = 0;

static void iflc_check(const char *tag, int cond) {
    iflc_checks++;
    if (!cond)
        iflc_failures++;
    print(tag, cond ? 1 : 0);
}

static int iflc_fd_count(void) {
    DIR *iflc_d = opendir("/dev/fd");
    struct dirent *iflc_e;
    int iflc_n = 0;
    if (iflc_d == NULL)
        return -1;
    while ((iflc_e = readdir(iflc_d)) != NULL)
        iflc_n++;
    closedir(iflc_d);
    return iflc_n;
}

int main(void) {
    error iflc_err = error();
    char iflc_buf[32];
    int iflc_st;
    int iflc_before;
    int iflc_after;
    int iflc_k;
    int iflc_all_ok;
    long iflc_pos;

    /* ---- 1. seek success and deterministic seek failures ---------------- */
    {
        file iflc_s = file_temp(&iflc_err);
        iflc_check("seek-open", iflc_s.is_open());
        iflc_check("seek-seed",
                   iflc_s.write_bytes("0123456789", 10, &iflc_err) == STDLIB_OK);
        iflc_st = iflc_s.seek(0, SEEK_END, &iflc_err);
        iflc_pos = ftell(iflc_s.native());
        iflc_check("seek-end", iflc_st == STDLIB_OK && iflc_pos == 10);
        iflc_st = iflc_s.seek(-4, SEEK_END, &iflc_err);
        iflc_pos = ftell(iflc_s.native());
        iflc_check("seek-end-neg", iflc_st == STDLIB_OK && iflc_pos == 6);
        iflc_st = iflc_s.seek(2, SEEK_CUR, &iflc_err);
        iflc_pos = ftell(iflc_s.native());
        iflc_check("seek-cur-plus", iflc_st == STDLIB_OK && iflc_pos == 8);
        iflc_st = iflc_s.seek(-3, SEEK_CUR, &iflc_err);
        iflc_pos = ftell(iflc_s.native());
        iflc_check("seek-cur-neg", iflc_st == STDLIB_OK && iflc_pos == 5);

        /* negative absolute offset cannot seek */
        iflc_st = iflc_s.seek(-1, SEEK_SET, &iflc_err);
        iflc_check("seek-fail-neg",
                   iflc_st == STDLIB_FAIL &&
                       iflc_err.category() == STDLIB_CATEGORY_IO &&
                       iflc_err.code() == CPLUS_EXIT_IO);
        /* invalid whence cannot seek */
        iflc_st = iflc_s.seek(0, 9999, &iflc_err);
        iflc_check("seek-fail-whence",
                   iflc_st == STDLIB_FAIL &&
                       iflc_err.category() == STDLIB_CATEGORY_IO);
        iflc_s.drop();
        iflc_st = iflc_s.seek(0, SEEK_SET, &iflc_err);
        iflc_check("seek-fail-dropped", iflc_st == STDLIB_FAIL);
    }

    /* ---- 2. flush success, and deterministic flush failures ------------- */
    {
        file iflc_f = file_temp(&iflc_err);
        iflc_check("flush-open", iflc_f.is_open());
        iflc_check("flush-write",
                   iflc_f.write_bytes("F", 1, &iflc_err) == STDLIB_OK);
        iflc_st = iflc_f.flush(&iflc_err);
        iflc_check("flush-ok", iflc_st == STDLIB_OK && iflc_err.ok());
        iflc_check("flush-close", iflc_f.close(&iflc_err) == STDLIB_OK);
        iflc_st = iflc_f.flush(&iflc_err);
        iflc_check("flush-fail-closed",
                   iflc_st == STDLIB_FAIL &&
                       iflc_err.category() == STDLIB_CATEGORY_IO);
    }
    {
        file iflc_dead = file(NULL, NULL);
        iflc_st = iflc_dead.flush(&iflc_err);
        iflc_check("flush-fail-null",
                   iflc_st == STDLIB_FAIL &&
                       iflc_err.category() == STDLIB_CATEGORY_IO);
        iflc_dead.drop();
    }

    /* ---- 3. repeated close / drop and mixed orders ---------------------- */
    {
        file iflc_c = file_temp(&iflc_err);
        iflc_check("close-open", iflc_c.is_open());
        iflc_check("close-first", iflc_c.close(&iflc_err) == STDLIB_OK);
        iflc_st = iflc_c.close(&iflc_err);
        iflc_check("close-second",
                   iflc_st == STDLIB_FAIL &&
                       iflc_err.category() == STDLIB_CATEGORY_LIFECYCLE);
        iflc_st = iflc_c.close(&iflc_err);
        iflc_check("close-third", iflc_st == STDLIB_FAIL);
        iflc_c.drop();
        iflc_c.drop();
        iflc_check("close-then-drop", !iflc_c.is_open());
    }
    {
        file iflc_dp = file_temp(&iflc_err);
        iflc_check("drop-open", iflc_dp.is_open());
        iflc_dp.drop();
        iflc_dp.drop();
        iflc_dp.drop();
        iflc_check("drop-repeat", !iflc_dp.is_open());
        iflc_st = iflc_dp.close(&iflc_err);
        iflc_check("drop-then-close",
                   iflc_st == STDLIB_FAIL &&
                       iflc_err.category() == STDLIB_CATEGORY_LIFECYCLE);
    }

    /* ---- 4. borrowed FILE* is NEVER closed by the wrapper (observable) -- */
    {
        FILE *iflc_raw = tmpfile();
        iflc_check("borrow-raw", iflc_raw != NULL);
        if (iflc_raw != NULL) {
            fputs("BRW", iflc_raw);
            rewind(iflc_raw);
            {
                file iflc_b1 = file_borrow(iflc_raw);
                iflc_check("borrow-b1",
                           iflc_b1.is_open() && iflc_b1.native() == iflc_raw);
                memset(iflc_buf, 0, sizeof(iflc_buf));
                iflc_st = iflc_b1.read_line(iflc_buf, sizeof(iflc_buf), &iflc_err);
                iflc_check("borrow-read",
                           iflc_st == STDLIB_OK && strcmp(iflc_buf, "BRW") == 0);
                iflc_b1.drop();
            }
            rewind(iflc_raw);
            iflc_check("borrow-after-drop",
                       fgetc(iflc_raw) == 'B' && !ferror(iflc_raw));
            {
                /* No explicit drop: scope destructor must also leave it open. */
                file iflc_b2 = file_borrow(iflc_raw);
                iflc_check("borrow-b2", iflc_b2.is_open());
            }
            rewind(iflc_raw);
            iflc_check("borrow-after-destructor",
                       fgetc(iflc_raw) == 'B' && !ferror(iflc_raw));
            {
                file iflc_b3 = file_borrow(iflc_raw);
                iflc_st = iflc_b3.close(&iflc_err);
                iflc_check("borrow-explicit-close-ok",
                           iflc_st == STDLIB_OK && iflc_err.ok());
            }
            rewind(iflc_raw);
            iflc_check("borrow-after-wrapper-close",
                       fgetc(iflc_raw) == 'B' && !ferror(iflc_raw));
            /* raw handle still writable proves it was never fclose()d */
            rewind(iflc_raw);
            iflc_check("borrow-raw-write", fputs("Z", iflc_raw) != EOF);
            rewind(iflc_raw);
            iflc_check("borrow-raw-verify", fgetc(iflc_raw) == 'Z');
            iflc_check("borrow-manual-close", fclose(iflc_raw) == 0);
        }
    }

    /* ---- 5. cleanup after failed init: many failed opens, no fd leak ---- */
    {
        iflc_before = iflc_fd_count();
        iflc_check("failedinit-baseline", iflc_before >= 0);
        iflc_all_ok = 1;
        for (iflc_k = 0; iflc_k < 32; iflc_k++) {
            file iflc_bad = file("cplus_no_such_file_zzzz", "r");
            if (iflc_bad.is_open() || iflc_bad.native() != NULL)
                iflc_all_ok = 0;
            iflc_st = iflc_bad.read(iflc_buf, sizeof(iflc_buf), &iflc_err);
            if (iflc_st != STDLIB_FAIL)
                iflc_all_ok = 0;
            iflc_bad.drop();
            iflc_bad.drop();
        }
        iflc_after = iflc_fd_count();
        iflc_check("failedinit-handles", iflc_all_ok);
        iflc_check("failedinit-no-leak",
                   iflc_before >= 0 && iflc_after == iflc_before);
    }

    /* ---- 6. repeated successful temp-file cycles leave no fd behind ----- */
    {
        iflc_before = iflc_fd_count();
        iflc_all_ok = 1;
        for (iflc_k = 0; iflc_k < 64; iflc_k++) {
            file iflc_cyc = file_temp(&iflc_err);
            if (!iflc_cyc.is_open())
                iflc_all_ok = 0;
            if (iflc_cyc.write_bytes("x", 1, &iflc_err) != STDLIB_OK)
                iflc_all_ok = 0;
            if (iflc_cyc.close(&iflc_err) != STDLIB_OK)
                iflc_all_ok = 0;
        }
        iflc_after = iflc_fd_count();
        iflc_check("cycles-ok", iflc_all_ok);
        iflc_check("cycles-no-leak",
                   iflc_before >= 0 && iflc_after == iflc_before);
    }

    print("IO_FILE_LIFECYCLE", iflc_checks, iflc_failures);
    return iflc_failures == 0 ? 0 : 1;
}
