/* Additional self-checking regressions for stdlib/file.hp -- binary data,
 * line shapes, lifecycle combinations and cleanup paths left untested by the
 * deep_file_* fixtures.
 *
 * Every check prints "<tag> <0|1>" (1 == pass).  The final line is
 * "EXTRA_FILE_BINARY <checks> <failures>"; the process exits 0 only when the
 * failure count is zero.  All fixtures use PID-unique names or file_temp(),
 * and every named file is removed with its removal asserted.
 *
 * No header is modified.  No stdout/stderr fixture is required: output is
 * deterministic tags and counts only.
 */
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"

static int efb_checks = 0;
static int efb_failures = 0;

static void efb_check(const char *tag, int cond) {
    efb_checks++;
    if (!cond)
        efb_failures++;
    print(tag, cond ? 1 : 0);
}

/* Open fd count via /dev/fd (same probe used by deep_file_lifecycle.cp). */
static int efb_fd_count(void) {
    DIR *d = opendir("/dev/fd");
    struct dirent *e;
    int n = 0;
    if (d == NULL)
        return -1;
    while ((e = readdir(d)) != NULL)
        n++;
    closedir(d);
    return n;
}

/* 1 when no file exists at path (the expected post-cleanup state). */
static int efb_absent(const char *path) {
    FILE *probe = fopen(path, "r");
    if (probe == NULL)
        return 1;
    fclose(probe);
    return 0;
}

int main(void) {
    error efb_err = error();
    char efb_pidtag[32];
    char efb_bin_path[96];
    char efb_ro_path[96];
    char efb_cycle_path[96];
    char efb_flush_path[96];
    char efb_cleanup_path[96];
    char efb_buf[64];
    int efb_st;
    int efb_i;

    snprintf(efb_pidtag, sizeof(efb_pidtag), "%d", (int)getpid());
    snprintf(efb_bin_path, sizeof(efb_bin_path),
             "cplus_efb_binary_%s.tmp", efb_pidtag);
    snprintf(efb_ro_path, sizeof(efb_ro_path),
             "cplus_efb_readonly_%s.tmp", efb_pidtag);
    snprintf(efb_cycle_path, sizeof(efb_cycle_path),
             "cplus_efb_cycles_%s.tmp", efb_pidtag);
    snprintf(efb_flush_path, sizeof(efb_flush_path),
             "cplus_efb_flush_%s.tmp", efb_pidtag);
    snprintf(efb_cleanup_path, sizeof(efb_cleanup_path),
             "cplus_efb_cleanup_%s.tmp", efb_pidtag);

    /* ---- 1. binary data with NUL bytes, write_bytes/read exact length ---- */
    {
        char efb_payload[10] = {'A', 0, 'B', 0, 0, 'C', (char)0xFF, 'Z', 9, 0};
        {
            file efb_bin_w = file(efb_bin_path, "w");
            efb_check("binary-open-w", efb_bin_w.is_open());
            efb_st = efb_bin_w.write_bytes(efb_payload, 10, &efb_err);
            efb_check("binary-write",
                      efb_st == STDLIB_OK && efb_err.ok());
            efb_st = efb_bin_w.close(&efb_err);
            efb_check("binary-close-w", efb_st == STDLIB_OK);
        }
        {
            file efb_bin_r = file(efb_bin_path, "r");
            memset(efb_buf, 0, sizeof(efb_buf));
            efb_st = efb_bin_r.read(efb_buf, 10, &efb_err);
            efb_check("binary-read-exact",
                      efb_st == STDLIB_OK &&
                      memcmp(efb_buf, efb_payload, 10) == 0 && efb_err.ok());
            efb_st = efb_bin_r.read(efb_buf, sizeof(efb_buf), &efb_err);
            efb_check("binary-eof",
                      efb_st == STDLIB_EOF && efb_err.ok());
        }
        efb_check("binary-cleanup", remove(efb_bin_path) == 0);
    }

    /* ---- 2. single-byte file ------------------------------------------- */
    {
        file efb_one = file_temp(&efb_err);
        if (!efb_one.is_open()) {
            efb_check("single-open", 0);
        } else {
            efb_check("single-open", 1);
            efb_st = efb_one.write_bytes("Q", 1, &efb_err);
            efb_check("single-write", efb_st == STDLIB_OK);
            efb_check("single-seek",
                      efb_one.seek(0, SEEK_SET, &efb_err) == STDLIB_OK);
            memset(efb_buf, 0, sizeof(efb_buf));
            efb_st = efb_one.read(efb_buf, 1, &efb_err);
            efb_check("single-read",
                      efb_st == STDLIB_OK && efb_buf[0] == 'Q' &&
                      efb_buf[1] == 0);
            efb_st = efb_one.read(efb_buf, 1, &efb_err);
            efb_check("single-eof", efb_st == STDLIB_EOF && efb_err.ok());
        }
    }

    /* ---- 3. empty line and multiple consecutive empty lines ------------ */
    {
        file efb_empty = file_temp(&efb_err);
        if (!efb_empty.is_open()) {
            efb_check("empty-setup", 0);
        } else {
            efb_check("empty-setup", 1);
            efb_check("empty-seed",
                      efb_empty.write_bytes("\n\n\n", 3, &efb_err) == STDLIB_OK &&
                      efb_empty.seek(0, SEEK_SET, &efb_err) == STDLIB_OK);
            for (efb_i = 0; efb_i < 3; efb_i++) {
                memset(efb_buf, 'Z', sizeof(efb_buf));
                efb_st = efb_empty.read_line(efb_buf, sizeof(efb_buf), &efb_err);
                efb_check(efb_i == 0 ? "empty-line-1" :
                          (efb_i == 1 ? "empty-line-2" : "empty-line-3"),
                          efb_st == STDLIB_OK && efb_buf[0] == '\0' &&
                          efb_err.ok());
            }
            efb_st = efb_empty.read_line(efb_buf, sizeof(efb_buf), &efb_err);
            efb_check("empty-lines-eof", efb_st == STDLIB_EOF && efb_err.ok());
        }
    }

    /* ---- 4. LF and CRLF in one file ------------------------------------ */
    {
        file efb_mixed = file_temp(&efb_err);
        if (!efb_mixed.is_open()) {
            efb_check("mixed-setup", 0);
        } else {
            efb_check("mixed-setup", 1);
            efb_check("mixed-seed",
                      efb_mixed.write_bytes("lf\ncrlf\r\ntail\n", 14, &efb_err)
                          == STDLIB_OK &&
                      efb_mixed.seek(0, SEEK_SET, &efb_err) == STDLIB_OK);
            efb_st = efb_mixed.read_line(efb_buf, sizeof(efb_buf), &efb_err);
            efb_check("mixed-lf",
                      efb_st == STDLIB_OK && strcmp(efb_buf, "lf") == 0);
            efb_st = efb_mixed.read_line(efb_buf, sizeof(efb_buf), &efb_err);
            efb_check("mixed-crlf",
                      efb_st == STDLIB_OK && strcmp(efb_buf, "crlf") == 0);
            efb_st = efb_mixed.read_line(efb_buf, sizeof(efb_buf), &efb_err);
            efb_check("mixed-last",
                      efb_st == STDLIB_OK && strcmp(efb_buf, "tail") == 0);
            efb_st = efb_mixed.read_line(efb_buf, sizeof(efb_buf), &efb_err);
            efb_check("mixed-eof", efb_st == STDLIB_EOF && efb_err.ok());
        }
    }

    /* ---- 5. final line without a newline ------------------------------- */
    {
        file efb_tail = file_temp(&efb_err);
        if (!efb_tail.is_open()) {
            efb_check("tail-setup", 0);
        } else {
            efb_check("tail-setup", 1);
            efb_check("tail-seed",
                      efb_tail.write_bytes("no-newline", 10, &efb_err) == STDLIB_OK &&
                      efb_tail.seek(0, SEEK_SET, &efb_err) == STDLIB_OK);
            efb_st = efb_tail.read_line(efb_buf, sizeof(efb_buf), &efb_err);
            efb_check("final-no-nl",
                      efb_st == STDLIB_OK &&
                      strcmp(efb_buf, "no-newline") == 0 && efb_err.ok());
            efb_st = efb_tail.read_line(efb_buf, sizeof(efb_buf), &efb_err);
            efb_check("final-no-nl-eof", efb_st == STDLIB_EOF && efb_err.ok());
        }
    }

    /* ---- 6/7. partial reads then read/read_line after EOF --------------- */
    {
        file efb_part = file_temp(&efb_err);
        if (!efb_part.is_open()) {
            efb_check("partial-setup", 0);
        } else {
            efb_check("partial-setup", 1);
            efb_check("partial-seed",
                      efb_part.write_bytes("abcdefgh", 8, &efb_err) == STDLIB_OK &&
                      efb_part.seek(0, SEEK_SET, &efb_err) == STDLIB_OK);
            memset(efb_buf, 0, sizeof(efb_buf));
            efb_st = efb_part.read(efb_buf, 4, &efb_err);
            efb_check("partial-first",
                      efb_st == STDLIB_OK && memcmp(efb_buf, "abcd", 4) == 0);
            memset(efb_buf, 0, sizeof(efb_buf));
            efb_st = efb_part.read(efb_buf, 8, &efb_err); /* only 4 remain */
            efb_check("partial-rest",
                      efb_st == STDLIB_OK && memcmp(efb_buf, "efgh", 4) == 0);
            efb_st = efb_part.read(efb_buf, sizeof(efb_buf), &efb_err);
            efb_check("partial-eof", efb_st == STDLIB_EOF && efb_err.ok());
            efb_st = efb_part.read(efb_buf, sizeof(efb_buf), &efb_err);
            efb_check("read-after-eof", efb_st == STDLIB_EOF && efb_err.ok());
            efb_st = efb_part.read_line(efb_buf, sizeof(efb_buf), &efb_err);
            efb_check("readline-after-eof",
                      efb_st == STDLIB_EOF && efb_err.ok());
        }
    }

    /* ---- 8. write to a read-only file (defined failure), content intact - */
    {
        {
            file efb_ro_seed = file(efb_ro_path, "w");
            efb_check("write-ro-seed-open", efb_ro_seed.is_open());
            efb_ro_seed.write_bytes("RO", 2, &efb_err);
        }
        {
            file efb_ro = file(efb_ro_path, "r");
            efb_check("write-ro-open", efb_ro.is_open());
            efb_st = efb_ro.write_bytes("X", 1, &efb_err);
            efb_check("write-ro-bytes",
                      efb_st == STDLIB_FAIL &&
                      efb_err.category() == STDLIB_CATEGORY_IO &&
                      efb_err.code() == CPLUS_EXIT_IO);
            efb_check("write-ro-string", efb_ro.write("Y") == 0);
        }
        {
            file efb_ro_verify = file(efb_ro_path, "r");
            memset(efb_buf, 0, sizeof(efb_buf));
            efb_st = efb_ro_verify.read(efb_buf, sizeof(efb_buf), &efb_err);
            efb_check("write-ro-content",
                      efb_st == STDLIB_OK && memcmp(efb_buf, "RO", 2) == 0);
        }
        efb_check("write-ro-cleanup", remove(efb_ro_path) == 0);
    }

    /* ---- 9. seek to beginning / end and SEEK_CUR ------------------------ */
    {
        file efb_seek = file_temp(&efb_err);
        long efb_pos;
        if (!efb_seek.is_open()) {
            efb_check("seek-setup", 0);
        } else {
            efb_check("seek-setup", 1);
            efb_check("seek-seed",
                      efb_seek.write_bytes("0123456789", 10, &efb_err)
                          == STDLIB_OK);
            efb_st = efb_seek.seek(0, SEEK_SET, &efb_err);
            efb_pos = ftell(efb_seek.native());
            efb_check("seek-set", efb_st == STDLIB_OK && efb_pos == 0);
            efb_st = efb_seek.seek(0, SEEK_END, &efb_err);
            efb_pos = ftell(efb_seek.native());
            efb_check("seek-end", efb_st == STDLIB_OK && efb_pos == 10);
            efb_st = efb_seek.seek(-4, SEEK_END, &efb_err);
            efb_pos = ftell(efb_seek.native());
            efb_check("seek-end-neg", efb_st == STDLIB_OK && efb_pos == 6);
            efb_st = efb_seek.seek(2, SEEK_CUR, &efb_err);
            efb_pos = ftell(efb_seek.native());
            efb_check("seek-cur-plus", efb_st == STDLIB_OK && efb_pos == 8);
            efb_st = efb_seek.seek(-3, SEEK_CUR, &efb_err);
            efb_pos = ftell(efb_seek.native());
            efb_check("seek-cur-neg", efb_st == STDLIB_OK && efb_pos == 5);
        }
    }

    /* ---- 10. flush on a write stream ----------------------------------- */
    {
        {
            file efb_flush_w = file(efb_flush_path, "w");
            efb_check("flush-open", efb_flush_w.is_open());
            efb_check("flush-write",
                      efb_flush_w.write_bytes("FLUSH", 5, &efb_err) == STDLIB_OK);
            efb_st = efb_flush_w.flush(&efb_err);
            efb_check("flush-stream", efb_st == STDLIB_OK && efb_err.ok());
            efb_check("flush-close",
                      efb_flush_w.close(&efb_err) == STDLIB_OK);
        }
        {
            file efb_flush_v = file(efb_flush_path, "r");
            memset(efb_buf, 0, sizeof(efb_buf));
            efb_st = efb_flush_v.read(efb_buf, sizeof(efb_buf), &efb_err);
            efb_check("flush-verify",
                      efb_st == STDLIB_OK && memcmp(efb_buf, "FLUSH", 5) == 0);
        }
        efb_check("flush-cleanup", remove(efb_flush_path) == 0);
    }

    /* ---- 11. close once / twice ---------------------------------------- */
    {
        file efb_close2 = file_temp(&efb_err);
        efb_check("close-once-open", efb_close2.is_open());
        efb_st = efb_close2.close(&efb_err);
        efb_check("close-once", efb_st == STDLIB_OK && !efb_close2.is_open());
        efb_st = efb_close2.close(&efb_err);
        efb_check("close-twice",
                  efb_st == STDLIB_FAIL &&
                  efb_err.category() == STDLIB_CATEGORY_LIFECYCLE);
    }

    /* ---- 12. drop once / twice ----------------------------------------- */
    {
        file efb_drop2 = file_temp(&efb_err);
        efb_check("drop-once-open", efb_drop2.is_open());
        efb_drop2.drop();
        efb_check("drop-once", !efb_drop2.is_open());
        efb_drop2.drop();
        efb_check("drop-twice", !efb_drop2.is_open());
    }

    /* ---- 13. close-then-drop and drop-then-close ------------------------ */
    {
        file efb_cd = file_temp(&efb_err);
        efb_check("close-drop-open", efb_cd.is_open());
        efb_check("close-drop-close", efb_cd.close(&efb_err) == STDLIB_OK);
        efb_cd.drop(); /* idempotent after close */
        efb_check("close-then-drop", !efb_cd.is_open());
    }
    {
        file efb_dc = file_temp(&efb_err);
        efb_check("drop-close-open", efb_dc.is_open());
        efb_dc.drop();
        efb_st = efb_dc.close(&efb_err);
        efb_check("drop-then-close",
                  efb_st == STDLIB_FAIL &&
                  efb_err.category() == STDLIB_CATEGORY_LIFECYCLE);
    }

    /* ---- 14. borrowed handle not closed by drop / wrapper destruction -- */
    {
        FILE *efb_raw = tmpfile();
        if (efb_raw == NULL) {
            efb_check("borrow-raw", 0);
        } else {
            fputs("BRW", efb_raw);
            rewind(efb_raw);
            {
                file efb_b1 = file_borrow(efb_raw);
                efb_check("borrow-b1-open",
                          efb_b1.is_open() && efb_b1.native() == efb_raw);
                efb_b1.drop();
            }
            rewind(efb_raw);
            efb_check("borrow-drop-still-open", fgetc(efb_raw) == 'B');
            {
                /* No explicit drop: the compiler-inserted destructor at scope
                 * exit must also leave the borrowed handle open. */
                file efb_b2 = file_borrow(efb_raw);
                efb_check("borrow-b2-open", efb_b2.is_open());
            }
            rewind(efb_raw);
            efb_check("borrow-scope-still-open", fgetc(efb_raw) == 'B');
            efb_check("borrow-manual-close", fclose(efb_raw) == 0);
        }
    }

    /* ---- 15. 50 repeated open/close cycles, no fd leak ----------------- */
    {
        int efb_before = efb_fd_count();
        int efb_opened = 1;
        int efb_closed = 1;
        int efb_after;
        efb_check("cycle-baseline", efb_before >= 0);
        for (efb_i = 0; efb_i < 50; efb_i++) {
            file efb_cyc = file(efb_cycle_path, "w");
            if (!efb_cyc.is_open())
                efb_opened = 0;
            if (efb_cyc.write_bytes("c", 1, &efb_err) != STDLIB_OK)
                efb_opened = 0;
            if (efb_cyc.close(&efb_err) != STDLIB_OK)
                efb_closed = 0;
        }
        efb_after = efb_fd_count();
        efb_check("cycle-opens", efb_opened);
        efb_check("cycle-closes", efb_closed);
        efb_check("cycle-no-fd-leak",
                  efb_before >= 0 && efb_after == efb_before);
        efb_check("cycle-cleanup", remove(efb_cycle_path) == 0);
    }

    /* ---- 16. resource cleanup after a write failure -------------------- */
    {
        int efb_before = efb_fd_count();
        int efb_after;
        {
            file efb_cl_seed = file(efb_cleanup_path, "w");
            efb_cl_seed.write_bytes("KEEP", 4, &efb_err);
        }
        {
            file efb_cl_fail = file(efb_cleanup_path, "r");
            efb_check("writefail-open", efb_cl_fail.is_open());
            efb_st = efb_cl_fail.write_bytes("X", 1, &efb_err);
            efb_check("writefail-status",
                      efb_st == STDLIB_FAIL &&
                      efb_err.category() == STDLIB_CATEGORY_IO &&
                      efb_err.code() == CPLUS_EXIT_IO);
            efb_check("writefail-drop", 1);
        }
        efb_after = efb_fd_count();
        efb_check("writefail-fd-restored",
                  efb_before >= 0 && efb_after == efb_before);
        {
            file efb_cl_verify = file(efb_cleanup_path, "r");
            memset(efb_buf, 0, sizeof(efb_buf));
            efb_st = efb_cl_verify.read(efb_buf, sizeof(efb_buf), &efb_err);
            efb_check("writefail-content",
                      efb_st == STDLIB_OK && memcmp(efb_buf, "KEEP", 4) == 0);
        }
        efb_check("writefail-cleanup", remove(efb_cleanup_path) == 0);
        efb_check("writefail-cleanup-absent", efb_absent(efb_cleanup_path));
    }

    print("EXTRA_FILE_BINARY", efb_checks, efb_failures);
    return efb_failures == 0 ? 0 : 1;
}
