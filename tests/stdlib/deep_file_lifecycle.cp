/* Deep lifecycle/ownership contract tests for stdlib/file.hp.
 *
 * Covers: null path/mode, invalid arguments, zero-length operations,
 * partial reads and EOF, explicit close and double-close, borrowed stream
 * ownership, cplus_move + double drop, failed operations, deterministic
 * allocation-failure injection, and fd/temp-file cleanup.
 *
 * Fatal cases (compile-time rejection of copying an owning `file`, and the
 * runtime cplus_fatal on invalid ownership) are deliberately NOT executed
 * here: a fatal case terminates the process.  They are exercised by separate
 * one-shot compiler invocations recorded in DEEP_FILE_CONTRACT.md.
 *
 * Returns 0 only when every check passes.
 */
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"

static int open_fd_count(void) {
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

int main(void) {
    error err = error();
    int failures = 0;
    int st;
    char buf[16];

    /* ---- SUCCESS: named file round-trip and cleanup -------------------- */
    {
        const char *path = "cplus_deep_file_lifecycle.tmp";
        {
            file w = file(path, "w");
            if (!w.is_open()) {
                failures++;
                print("FAIL create", 0);
            } else {
                if (w.write_bytes("roundtrip\n", 10, &err) != STDLIB_OK ||
                    w.flush(&err) != STDLIB_OK) {
                    failures++;
                    print("FAIL write-file", 0);
                } else {
                    print("ok write-file", 1);
                }
                if (w.close(&err) != STDLIB_OK) {
                    failures++;
                    print("FAIL close-file", 0);
                }
            }
        }
        {
            file r = file(path, "r");
            char lb[32];
            if (!r.is_open()) {
                failures++;
                print("FAIL reopen", 0);
            } else {
                st = r.read_line(lb, sizeof(lb), &err);
                if (st != STDLIB_OK || strcmp(lb, "roundtrip") != 0) {
                    failures++;
                    print("FAIL read-file", st, lb);
                } else {
                    print("ok read-file", st, lb);
                }
            }
        }
        if (remove(path) != 0) {
            failures++;
            print("FAIL remove-temp", 0);
        } else {
            print("ok remove-temp", 1);
        }
    }

    /* ---- RECOVERABLE: a null path or mode never calls fopen ------------ */
    {
        file n1 = file(NULL, "r");
        file n2 = file("unused", NULL);
        file n3 = file(NULL, NULL);
        if (n1.is_open() || n2.is_open() || n3.is_open() ||
            n1.native() != NULL || n2.native() != NULL || n3.native() != NULL) {
            failures++;
            print("FAIL null-constructors", n1.is_open(), n2.is_open(),
                  n3.is_open());
        } else {
            print("ok null-constructors", 1);
        }
        st = n1.read(buf, sizeof(buf), &err);
        if (st != STDLIB_FAIL || err.category() != STDLIB_CATEGORY_IO) {
            failures++; print("FAIL null-read", st, err.category());
        }
        st = n1.read_line(buf, sizeof(buf), &err);
        if (st != STDLIB_FAIL || err.category() != STDLIB_CATEGORY_IO) {
            failures++; print("FAIL null-readline", st, err.category());
        }
        if (n1.write("x") != 0) {
            failures++; print("FAIL null-write-string", 0);
        }
        st = n1.write_bytes("x", 1, &err);
        if (st != STDLIB_FAIL || err.category() != STDLIB_CATEGORY_IO) {
            failures++; print("FAIL null-write-bytes", st, err.category());
        }
        st = n1.seek(0, SEEK_SET, &err);
        if (st != STDLIB_FAIL || err.category() != STDLIB_CATEGORY_IO) {
            failures++; print("FAIL null-seek", st, err.category());
        }
        st = n1.flush(&err);
        if (st != STDLIB_FAIL || err.category() != STDLIB_CATEGORY_IO) {
            failures++; print("FAIL null-flush", st, err.category());
        }
        st = n1.close(&err);
        if (st != STDLIB_FAIL ||
            err.category() != STDLIB_CATEGORY_LIFECYCLE) {
            failures++; print("FAIL null-close", st, err.category());
        } else {
            print("ok null-handle-ops", 1);
        }
        n1.drop(); n2.drop(); n3.drop();
    }

    /* ---- RECOVERABLE: invalid arguments on a live temp stream ---------- */
    {
        file live = file_temp(&err);
        if (!live.is_open() || !err.ok()) {
            failures++;
            print("FAIL file-temp", 0);
        } else {
            print("ok file-temp", 1);
            if (live.write_bytes(NULL, 0, &err) != STDLIB_OK) {
                failures++; print("FAIL zero-write", 0);
            }
            st = live.write_bytes(NULL, 4, &err);
            if (st != STDLIB_FAIL || err.code() != CPLUS_EXIT_UNSUPPORTED) {
                failures++; print("FAIL null-write", st, err.code());
            }
            st = live.read(buf, 0, &err);
            if (st != STDLIB_FAIL || err.code() != CPLUS_EXIT_UNSUPPORTED) {
                failures++; print("FAIL zero-read", st, err.code());
            }
            st = live.read(NULL, 4, &err);
            if (st != STDLIB_FAIL || err.code() != CPLUS_EXIT_UNSUPPORTED) {
                failures++; print("FAIL null-read", st, err.code());
            }
            st = live.read_line(buf, 0, &err);
            if (st != STDLIB_FAIL) {
                failures++; print("FAIL zero-readline", st);
            }
            st = live.read_line(NULL, 4, &err);
            if (st != STDLIB_FAIL) {
                failures++; print("FAIL null-readline", st);
            }
            if (live.write(NULL) != 0) {
                failures++; print("FAIL null-write-string2", 0);
            }
            print("ok invalid-args", 1);
        }
    }

    /* ---- SUCCESS: partial read then EOF on a live temp stream ---------- */
    {
        file seq = file_temp(&err);
        if (!seq.is_open()) {
            failures++;
            print("FAIL seq-open", 0);
        } else {
            if (seq.write_bytes("abcdefgh", 8, &err) != STDLIB_OK ||
                seq.seek(0, SEEK_SET, &err) != STDLIB_OK) {
                failures++; print("FAIL seq-setup", 0);
            }
            st = seq.read(buf, 4, &err);
            if (st != STDLIB_OK || memcmp(buf, "abcd", 4) != 0) {
                failures++; print("FAIL read-full", st);
            }
            st = seq.read(buf, 8, &err); /* only four bytes remain */
            if (st != STDLIB_OK || memcmp(buf, "efgh", 4) != 0) {
                failures++; print("FAIL read-partial", st);
            }
            st = seq.read(buf, 8, &err);
            if (st != STDLIB_EOF || err.failed_now()) {
                failures++; print("FAIL read-eof", st, err.failed_now());
            } else {
                print("ok partial-eof", 1);
            }
            if (seq.seek(0, SEEK_END, &err) != STDLIB_OK ||
                seq.seek(-1, SEEK_SET, &err) != STDLIB_FAIL ||
                err.category() != STDLIB_CATEGORY_IO) {
                failures++; print("FAIL seek-failure", 0);
            } else {
                print("ok seek-failure", 1);
            }
        }
    }

    /* ---- SUCCESS: close, double close, and drop are recoverable -------- */
    {
        file once = file_temp(&err);
        if (!once.is_open()) {
            failures++;
            print("FAIL close-open", 0);
        } else {
            if (once.close(&err) != STDLIB_OK || once.is_open()) {
                failures++; print("FAIL close-first", 0);
            }
            st = once.close(&err);
            if (st != STDLIB_FAIL ||
                err.category() != STDLIB_CATEGORY_LIFECYCLE) {
                failures++; print("FAIL close-second", st, err.category());
            }
            if (once.close(&err) != STDLIB_FAIL) {
                failures++; print("FAIL close-third", 0);
            }
            if (once.write("x") != 0 ||
                once.read(buf, sizeof(buf), &err) != STDLIB_FAIL ||
                once.flush(&err) != STDLIB_FAIL) {
                failures++; print("FAIL closed-ops", 0);
            }
            once.drop();
            once.drop();
            if (once.is_open()) {
                failures++; print("FAIL double-drop", 0);
            } else {
                print("ok close-double-drop", 1);
            }
        }
    }

    /* ---- SUCCESS: borrowed streams are never closed by the wrapper ----- */
    {
        FILE *raw = tmpfile();
        if (raw != NULL) {
            fputs("hi\n", raw);
            rewind(raw);
            file b = file_borrow(raw);
            if (!b.is_open() || b.native() != raw) {
                failures++; print("FAIL borrow-open", 0);
            }
            st = b.read_line(buf, sizeof(buf), &err);
            if (st != STDLIB_OK || strcmp(buf, "hi") != 0) {
                failures++; print("FAIL borrow-read", st, buf);
            }
            if (b.close(&err) != STDLIB_OK || b.is_open()) {
                failures++; print("FAIL borrow-close", 0);
            }
            rewind(raw);
            if (fgetc(raw) != 'h') {
                failures++; print("FAIL borrow-still-open", 0);
            }
            if (fclose(raw) != 0) {
                failures++; print("FAIL borrow-manual-close", 0);
            } else {
                print("ok borrow-close", 1);
            }
        } else {
            failures++; print("FAIL borrow-tmpfile", 0);
        }
    }
    {
        FILE *raw2 = tmpfile();
        if (raw2 != NULL) {
            file b2 = file_borrow(raw2);
            b2.drop();
            b2.drop();
            if (fclose(raw2) != 0) {
                failures++; print("FAIL borrow-drop", 0);
            } else {
                print("ok borrow-drop", 1);
            }
        } else {
            failures++; print("FAIL borrow2-tmpfile", 0);
        }
    }

    /* ---- SUCCESS: cplus_move transfers ownership exactly once ---------- */
    {
        file source = file_temp(&err);
        if (!source.is_open()) {
            failures++; print("FAIL move-open", 0);
        } else {
            FILE *native = source.native();
            file dest = cplus_move(source);
            /* The moved-from `source` is intentionally not read here: strict
             * mode treats that as use-after-move.  Its implicit destructor at
             * scope exit is the double-drop check -- a non-zeroed handle would
             * close the same FILE* twice and abort the process. */
            if (!dest.is_open() || dest.native() != native) {
                failures++; print("FAIL move-state", dest.is_open());
            }
            if (dest.write_bytes("m", 1, &err) != STDLIB_OK) {
                failures++; print("FAIL move-write", 0);
            }
            dest.drop();
            dest.drop();
            if (dest.is_open()) {
                failures++; print("FAIL move-double-drop", 0);
            } else {
                print("ok move-double-drop", 1);
            }
        }
    }

    /* ---- SUCCESS: no fd leak across owned temp files ------------------- */
    {
        int before = open_fd_count();
        int during = 0;
        int after = 0;
        if (before < 0) {
            failures++; print("FAIL fd-count-unavailable", 0);
        } else {
            file t1 = file_temp(&err);
            file t2 = file_temp(&err);
            file t3 = file_temp(&err);
            during = open_fd_count();
            if (!t1.is_open() || !t2.is_open() || !t3.is_open() ||
                during != before + 3) {
                failures++; print("FAIL fd-during", before, during);
            }
            print("ok fd-during", before, during);
            /* implicit drops at scope exit close all three */
        }
        after = open_fd_count();
        if (before >= 0 && after != before) {
            failures++; print("FAIL fd-leak", before, after);
        } else {
            print("ok fd-leak", before, after);
        }
    }

    /* ---- SUCCESS: stdlib allocation failures do not affect file.hp -----
     * file.hp performs no stdlib_alloc/calloc calls; injection must be a no-op. */
    {
        stdlib_alloc_fail_after(0);
        file t = file_temp(&err);
        if (!t.is_open() || t.write_bytes("z", 1, &err) != STDLIB_OK ||
            t.seek(0, SEEK_SET, &err) != STDLIB_OK) {
            failures++; print("FAIL alloc-injection", 0);
        } else {
            st = t.read(buf, 1, &err);
            if (st != STDLIB_OK || buf[0] != 'z') {
                failures++; print("FAIL alloc-injection-read", st);
            } else {
                print("ok alloc-injection", 1);
            }
        }
        stdlib_alloc_fail_reset();
    }

    print("deep-file-lifecycle", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
