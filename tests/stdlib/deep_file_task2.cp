/* Independent deep contract tests for stdlib/file.hp -- Task 2 uncovered cases.
 *
 * This file deliberately contains ONLY cases not already exercised by
 * deep_file_lines.cp and deep_file_lifecycle.cp.  It uses self-contained
 * tmpfile()/named-file fixtures and removes every named file it creates.
 *
 * Covered here (and nowhere else):
 *   1. empty file: read_line/read -> STDLIB_EOF, error clear, buffer untouched,
 *      repeated read_line stays EOF with no stale error;
 *   2. exact-fit at physical EOF ("abcd" in buf[5]) -> OK/"abcd", ftell==4,
 *      next read EOF  (regression for the just-fixed peek logic);
 *   3. exact-fit before newline ("abcd\n" in buf[5]) -> OK/"abcd", ftell==5,
 *      next read EOF with no phantom empty line;
 *   4. rejection alignment ("abcdef\nNEXT\n" in buf[5]) -> FAIL/BOUNDS,
 *      ftell==7 (start of NEXT), next read returns "NEXT";
 *   5. real read failure: read() on a write-only ("w") stream -> FAIL/IO with
 *      ferror(stream) set, no crash;
 *   6. real write failure: write_bytes()/write() on a read-only ("r") stream
 *      -> FAIL/IO and the on-disk content is unchanged;
 *   7. invalid path and invalid mode -> !is_open(), native()==NULL, and later
 *      operations remain recoverable;
 *   8. boundaries: write_bytes(data,0) with non-NULL data, SEEK_CUR round-trip,
 *      seek far past EOF then read_line -> EOF, flush() on a read-only stream
 *      -> OK, and seek() with an invalid whence -> FAIL/IO.
 *
 * SELF-CHECKING: every t2_check() prints "<tag> <1|0>" (1 = pass).  The last
 * line is "FILE_TASK2 <checks> <failures>".  Exit code 0 == no failures.
 * All fixtures are files, so stdout/stdin are not needed; every binary run is
 * bounded by an external `timeout` in the verification commands.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"

static int g_t2_checks = 0;
static int g_t2_failures = 0;

static void t2_check(const char *tag, int cond) {
    g_t2_checks++;
    if (!cond)
        g_t2_failures++;
    print(tag, cond ? 1 : 0);
}

/* Returns 1 when no file exists at `path` (the expected post-cleanup state). */
static int t2_path_absent(const char *path) {
    FILE *t2_probe_handle = fopen(path, "r");
    if (t2_probe_handle == NULL)
        return 1;
    fclose(t2_probe_handle);
    return 0;
}

int main(void) {
    error t2_err = error();
    int t2_st;
    char t2_buf[16];
    char t2_pidtag[32];
    char t2_wpath[80];
    char t2_rpath[80];
    char t2_valid[80];
    char t2_ropath[80];

    /* Process-unique names: the three verification modes may run in parallel. */
    snprintf(t2_pidtag, sizeof(t2_pidtag), "%d", (int)getpid());
    snprintf(t2_wpath, sizeof(t2_wpath), "cplus_deep_file_task2_writeonly_%s.tmp", t2_pidtag);
    snprintf(t2_rpath, sizeof(t2_rpath), "cplus_deep_file_task2_readonly_%s.tmp", t2_pidtag);
    snprintf(t2_valid, sizeof(t2_valid), "cplus_deep_file_task2_valid_%s.tmp", t2_pidtag);
    snprintf(t2_ropath, sizeof(t2_ropath), "cplus_deep_file_task2_flush_%s.tmp", t2_pidtag);

    /* ---- 1. EMPTY FILE -------------------------------------------------- */
    {
        file t2_empty_file = file_temp(&t2_err);
        int t2_i;
        int t2_untouched;
        if (!t2_empty_file.is_open()) {
            t2_check("empty-open", 0);
        } else {
            t2_check("empty-open", 1);
            memset(t2_buf, 'Z', sizeof(t2_buf));
            t2_st = t2_empty_file.read_line(t2_buf, sizeof(t2_buf), &t2_err);
            t2_untouched = 1;
            for (t2_i = 0; t2_i < (int)sizeof(t2_buf); t2_i++) {
                if (t2_buf[t2_i] != 'Z')
                    t2_untouched = 0;
            }
            t2_check("empty-readline-eof",
                     t2_st == STDLIB_EOF && t2_err.ok() && t2_untouched);
            t2_st = t2_empty_file.read(t2_buf, sizeof(t2_buf), &t2_err);
            t2_check("empty-read-eof",
                     t2_st == STDLIB_EOF && t2_err.ok());
            t2_st = t2_empty_file.read_line(t2_buf, sizeof(t2_buf), &t2_err);
            t2_check("empty-readline-again-eof",
                     t2_st == STDLIB_EOF && t2_err.ok());
        }
    }

    /* ---- 2. EXACT FIT AT PHYSICAL EOF ----------------------------------- */
    {
        file t2_exact_eof = file_temp(&t2_err);
        long t2_exact_eof_pos;
        int t2_setup;
        if (!t2_exact_eof.is_open()) {
            t2_check("exact-eof-setup", 0);
        } else {
            t2_setup = (t2_exact_eof.write_bytes("abcd", 4, &t2_err) == STDLIB_OK &&
                        t2_exact_eof.seek(0, SEEK_SET, &t2_err) == STDLIB_OK);
            t2_check("exact-eof-setup", t2_setup);
            memset(t2_buf, 'Z', sizeof(t2_buf));
            t2_st = t2_exact_eof.read_line(t2_buf, 5, &t2_err);
            t2_check("exact-eof-status",
                     t2_st == STDLIB_OK && strcmp(t2_buf, "abcd") == 0 && t2_err.ok());
            t2_exact_eof_pos = ftell(t2_exact_eof.native());
            t2_check("exact-eof-tell", t2_exact_eof_pos == 4);
            t2_st = t2_exact_eof.read_line(t2_buf, 5, &t2_err);
            t2_check("exact-eof-next-eof", t2_st == STDLIB_EOF && t2_err.ok());
        }
    }

    /* ---- 3. EXACT FIT BEFORE NEWLINE ------------------------------------ */
    {
        file t2_exact_nl = file_temp(&t2_err);
        long t2_exact_nl_pos;
        if (!t2_exact_nl.is_open()) {
            t2_check("exact-nl-setup", 0);
        } else {
            t2_check("exact-nl-setup",
                     t2_exact_nl.write_bytes("abcd\n", 5, &t2_err) == STDLIB_OK &&
                     t2_exact_nl.seek(0, SEEK_SET, &t2_err) == STDLIB_OK);
            memset(t2_buf, 'Z', sizeof(t2_buf));
            t2_st = t2_exact_nl.read_line(t2_buf, 5, &t2_err);
            t2_check("exact-nl-status",
                     t2_st == STDLIB_OK && strcmp(t2_buf, "abcd") == 0 && t2_err.ok());
            t2_exact_nl_pos = ftell(t2_exact_nl.native());
            t2_check("exact-nl-tell", t2_exact_nl_pos == 5);
            /* The newline was consumed by the fit-checking peek: no phantom. */
            t2_st = t2_exact_nl.read_line(t2_buf, 5, &t2_err);
            t2_check("exact-nl-next-eof", t2_st == STDLIB_EOF && t2_err.ok());
        }
    }

    /* ---- 4. POSITION AFTER REJECTION ------------------------------------ */
    {
        file t2_reject = file_temp(&t2_err);
        long t2_reject_pos;
        if (!t2_reject.is_open()) {
            t2_check("reject-setup", 0);
        } else {
            t2_check("reject-setup",
                     t2_reject.write_bytes("abcdef\nNEXT\n", 12, &t2_err) == STDLIB_OK &&
                     t2_reject.seek(0, SEEK_SET, &t2_err) == STDLIB_OK);
            memset(t2_buf, 'Z', sizeof(t2_buf));
            t2_st = t2_reject.read_line(t2_buf, 5, &t2_err);
            t2_check("reject-status",
                     t2_st == STDLIB_FAIL &&
                     t2_err.category() == STDLIB_CATEGORY_BOUNDS &&
                     t2_err.code() == CPLUS_EXIT_BOUNDS && t2_buf[0] == '\0');
            t2_reject_pos = ftell(t2_reject.native());
            t2_check("reject-tell", t2_reject_pos == 7);
            t2_st = t2_reject.read_line(t2_buf, sizeof(t2_buf), &t2_err);
            t2_check("reject-next-line",
                     t2_st == STDLIB_OK && strcmp(t2_buf, "NEXT") == 0);
        }
    }

    /* ---- 5. REAL READ FAILURE (not EOF) --------------------------------- */
    {
        file t2_readfail = file(t2_wpath, "w");
        t2_check("readfail-open", t2_readfail.is_open());
        t2_st = t2_readfail.read(t2_buf, sizeof(t2_buf), &t2_err);
        t2_check("readfail-status",
                 t2_st == STDLIB_FAIL && t2_err.code() == CPLUS_EXIT_IO &&
                 t2_err.category() == STDLIB_CATEGORY_IO);
        t2_check("readfail-ferror", ferror(t2_readfail.native()) != 0);
        t2_readfail.drop();
        t2_check("readfail-cleanup", remove(t2_wpath) == 0);
    }

    /* ---- 6. REAL WRITE FAILURE ------------------------------------------ */
    {
        {
            file t2_seed_r = file(t2_rpath, "w");
            t2_seed_r.write_bytes("KEEP\n", 5, &t2_err);
        }
        file t2_writefail = file(t2_rpath, "r");
        t2_check("writefail-open", t2_writefail.is_open());
        t2_st = t2_writefail.write_bytes("X", 1, &t2_err);
        t2_check("writebytesfail-status",
                 t2_st == STDLIB_FAIL && t2_err.category() == STDLIB_CATEGORY_IO &&
                 t2_err.code() == CPLUS_EXIT_IO);
        t2_check("writefail-string-zero", t2_writefail.write("Y") == 0);
        t2_writefail.drop();
        {
            file t2_verify_r = file(t2_rpath, "r");
            memset(t2_buf, 0, sizeof(t2_buf));
            t2_st = t2_verify_r.read(t2_buf, sizeof(t2_buf), &t2_err);
            t2_check("writefail-content-intact",
                     t2_st == STDLIB_OK && strcmp(t2_buf, "KEEP\n") == 0);
        }
        t2_check("writefail-cleanup", remove(t2_rpath) == 0);
    }

    /* ---- 7. INVALID PATH AND MODE --------------------------------------- */
    {
        {
            file t2_seed_v = file(t2_valid, "w");
            t2_seed_v.write_bytes("v", 1, &t2_err);
        }
        file t2_badpath = file("/definitely/not/a/real/dir/x", "r");
        file t2_badmode = file(t2_valid, "q");
        t2_check("badpath-open",
                 !t2_badpath.is_open() && t2_badpath.native() == NULL);
        t2_check("badmode-open",
                 !t2_badmode.is_open() && t2_badmode.native() == NULL);
        t2_st = t2_badpath.read(t2_buf, sizeof(t2_buf), &t2_err);
        int t2_recover = (t2_st == STDLIB_FAIL &&
                          t2_err.category() == STDLIB_CATEGORY_IO);
        t2_st = t2_badpath.close(&t2_err);
        t2_recover = t2_recover && t2_st == STDLIB_FAIL &&
                     t2_err.category() == STDLIB_CATEGORY_LIFECYCLE;
        t2_badpath.drop();
        t2_badmode.drop();
        t2_check("badpath-recoverable", t2_recover);
        t2_check("validpath-cleanup", remove(t2_valid) == 0);
    }

    /* ---- 8. SEEK / FLUSH / WRITE_BYTES BOUNDARIES ----------------------- */
    {
        file t2_bound = file_temp(&t2_err);
        long t2_p1, t2_p2;
        if (!t2_bound.is_open()) {
            t2_check("bound-open", 0);
        } else {
            t2_check("bound-open", 1);
            t2_st = t2_bound.write_bytes("abc", 0, &t2_err);
            t2_check("writebytes-zero-nondata", t2_st == STDLIB_OK && t2_err.ok());
            t2_bound.write_bytes("0123456789", 10, &t2_err);
            t2_bound.seek(2, SEEK_SET, &t2_err);
            t2_p1 = ftell(t2_bound.native());
            t2_st = t2_bound.seek(0, SEEK_CUR, &t2_err);
            t2_p2 = ftell(t2_bound.native());
            t2_check("seek-cur-roundtrip",
                     t2_st == STDLIB_OK && t2_p1 == 2 && t2_p2 == 2);
            t2_bound.seek(1000, SEEK_SET, &t2_err);
            t2_st = t2_bound.read_line(t2_buf, sizeof(t2_buf), &t2_err);
            t2_check("seek-past-eof-eof", t2_st == STDLIB_EOF && t2_err.ok());
            t2_st = t2_bound.seek(0, 999, &t2_err);
            t2_check("seek-invalid-whence",
                     t2_st == STDLIB_FAIL && t2_err.category() == STDLIB_CATEGORY_IO);
        }
    }
    {
        {
            file t2_seed_f = file(t2_ropath, "w");
            t2_seed_f.write_bytes("ro", 2, &t2_err);
        }
        file t2_flushro = file(t2_ropath, "r");
        t2_check("flushro-open", t2_flushro.is_open());
        t2_st = t2_flushro.flush(&t2_err);
        t2_check("flush-readonly-ok", t2_st == STDLIB_OK && t2_err.ok());
        t2_flushro.drop();
        t2_check("flushpath-cleanup", remove(t2_ropath) == 0);
    }

    /* ---- CLEANUP PROOF: none of the named fixtures survived ------------- */
    {
        int t2_leftover = 0;
        if (!t2_path_absent(t2_wpath)) t2_leftover = 1;
        if (!t2_path_absent(t2_rpath)) t2_leftover = 1;
        if (!t2_path_absent(t2_valid)) t2_leftover = 1;
        if (!t2_path_absent(t2_ropath)) t2_leftover = 1;
        t2_check("cleanup-no-leftovers", t2_leftover == 0);
    }

    print("FILE_TASK2", g_t2_checks, g_t2_failures);
    return g_t2_failures == 0 ? 0 : 1;
}
