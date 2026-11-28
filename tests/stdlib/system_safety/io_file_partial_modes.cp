/* io_file_partial_modes -- Task 3 Phase 2: partial reads/writes, invalid
 * handles, and read-only vs write-only modes for stdlib/file.hp.
 *
 * Deterministic: all fixtures are file_temp() or PID-unique named files with
 * an asserted remove().  Bounded: fixed counts only.  No variable
 * division/modulo or pointer-parameter subscripts (__builtin_memcpy is used
 * where a byte must be inspected); out-params initialised; unique prefixes.
 *
 * Each check prints "<tag> <0|1>"; final line is
 * "IO_FILE_PARTIAL_MODES <checks> <failures>"; exit 0 only when failures==0.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"

static int ifpm_checks = 0;
static int ifpm_failures = 0;

static void ifpm_check(const char *tag, int cond) {
    ifpm_checks++;
    if (!cond)
        ifpm_failures++;
    print(tag, cond ? 1 : 0);
}

static int ifpm_absent(const char *path) {
    FILE *ifpm_p = fopen(path, "r");
    if (ifpm_p == NULL)
        return 1;
    fclose(ifpm_p);
    return 0;
}

int main(void) {
    error ifpm_err = error();
    char ifpm_pidtag[24];
    char ifpm_ro[96];
    char ifpm_wo[96];
    char ifpm_rw[96];
    char ifpm_buf[32];
    int ifpm_st;
    FILE *ifpm_probe;

    snprintf(ifpm_pidtag, sizeof(ifpm_pidtag), "%d", (int)getpid());
    snprintf(ifpm_ro, sizeof(ifpm_ro), "cplus_ifpm_ro_%s.tmp", ifpm_pidtag);
    snprintf(ifpm_wo, sizeof(ifpm_wo), "cplus_ifpm_wo_%s.tmp", ifpm_pidtag);
    snprintf(ifpm_rw, sizeof(ifpm_rw), "cplus_ifpm_rw_%s.tmp", ifpm_pidtag);

    /* ---- 1. partial reads: full, partial, then EOF ---------------------- */
    {
        file ifpm_t = file_temp(&ifpm_err);
        ifpm_check("partial-open", ifpm_t.is_open());
        ifpm_check("partial-seed",
                   ifpm_t.write_bytes("abcdefgh", 8, &ifpm_err) == STDLIB_OK &&
                       ifpm_t.seek(0, SEEK_SET, &ifpm_err) == STDLIB_OK);
        memset(ifpm_buf, 0, sizeof(ifpm_buf));
        ifpm_st = ifpm_t.read(ifpm_buf, 4, &ifpm_err);
        ifpm_check("partial-first",
                   ifpm_st == STDLIB_OK && memcmp(ifpm_buf, "abcd", 4) == 0);
        memset(ifpm_buf, 0, sizeof(ifpm_buf));
        ifpm_st = ifpm_t.read(ifpm_buf, 8, &ifpm_err); /* only 4 remain */
        ifpm_check("partial-rest",
                   ifpm_st == STDLIB_OK && memcmp(ifpm_buf, "efgh", 4) == 0);
        ifpm_st = ifpm_t.read(ifpm_buf, sizeof(ifpm_buf), &ifpm_err);
        ifpm_check("partial-eof", ifpm_st == STDLIB_EOF && ifpm_err.ok());
        ifpm_st = ifpm_t.read(ifpm_buf, sizeof(ifpm_buf), &ifpm_err);
        ifpm_check("partial-eof-repeat", ifpm_st == STDLIB_EOF && ifpm_err.ok());
        ifpm_t.drop();
    }

    /* ---- 2. zero-length and null-argument operations -------------------- */
    {
        file ifpm_t2 = file_temp(&ifpm_err);
        ifpm_check("arg-open", ifpm_t2.is_open());
        ifpm_check("arg-zero-write",
                   ifpm_t2.write_bytes(NULL, 0, &ifpm_err) == STDLIB_OK &&
                       ifpm_err.ok());
        ifpm_st = ifpm_t2.write_bytes(NULL, 4, &ifpm_err);
        ifpm_check("arg-null-write",
                   ifpm_st == STDLIB_FAIL &&
                       ifpm_err.code() == CPLUS_EXIT_UNSUPPORTED);
        ifpm_st = ifpm_t2.read(NULL, 4, &ifpm_err);
        ifpm_check("arg-null-read",
                   ifpm_st == STDLIB_FAIL &&
                       ifpm_err.code() == CPLUS_EXIT_UNSUPPORTED);
        ifpm_st = ifpm_t2.read(ifpm_buf, 0, &ifpm_err);
        ifpm_check("arg-zero-read",
                   ifpm_st == STDLIB_FAIL &&
                       ifpm_err.code() == CPLUS_EXIT_UNSUPPORTED);
        ifpm_st = ifpm_t2.read_line(NULL, 4, &ifpm_err);
        ifpm_check("arg-null-readline",
                   ifpm_st == STDLIB_FAIL &&
                       ifpm_err.code() == CPLUS_EXIT_UNSUPPORTED);
        ifpm_st = ifpm_t2.read_line(ifpm_buf, 0, &ifpm_err);
        ifpm_check("arg-zero-readline", ifpm_st == STDLIB_FAIL);
        ifpm_st = ifpm_t2.read_line(ifpm_buf, ((size_t)1 << 31), &ifpm_err);
        ifpm_check("arg-huge-readline",
                   ifpm_st == STDLIB_FAIL &&
                       ifpm_err.category() == STDLIB_CATEGORY_BOUNDS);
        ifpm_check("arg-null-string-write", ifpm_t2.write(NULL) == 0);
        ifpm_t2.drop();
    }

    /* ---- 3. invalid / null handles are all recoverable ------------------ */
    {
        file ifpm_dead = file(NULL, NULL);
        ifpm_check("null-open", !ifpm_dead.is_open() && ifpm_dead.native() == NULL);
        ifpm_st = ifpm_dead.read(ifpm_buf, sizeof(ifpm_buf), &ifpm_err);
        ifpm_check("null-read",
                   ifpm_st == STDLIB_FAIL &&
                       ifpm_err.category() == STDLIB_CATEGORY_IO);
        ifpm_st = ifpm_dead.read_line(ifpm_buf, sizeof(ifpm_buf), &ifpm_err);
        ifpm_check("null-readline",
                   ifpm_st == STDLIB_FAIL &&
                       ifpm_err.category() == STDLIB_CATEGORY_IO);
        ifpm_check("null-write-string", ifpm_dead.write("x") == 0);
        ifpm_st = ifpm_dead.write_bytes("x", 1, &ifpm_err);
        ifpm_check("null-write-bytes",
                   ifpm_st == STDLIB_FAIL &&
                       ifpm_err.category() == STDLIB_CATEGORY_IO);
        ifpm_st = ifpm_dead.seek(0, SEEK_SET, &ifpm_err);
        ifpm_check("null-seek",
                   ifpm_st == STDLIB_FAIL &&
                       ifpm_err.category() == STDLIB_CATEGORY_IO);
        ifpm_st = ifpm_dead.flush(&ifpm_err);
        ifpm_check("null-flush",
                   ifpm_st == STDLIB_FAIL &&
                       ifpm_err.category() == STDLIB_CATEGORY_IO);
        ifpm_st = ifpm_dead.close(&ifpm_err);
        ifpm_check("null-close",
                   ifpm_st == STDLIB_FAIL &&
                       ifpm_err.category() == STDLIB_CATEGORY_LIFECYCLE);
        ifpm_dead.drop();
        ifpm_dead.drop();
        ifpm_check("null-double-drop", !ifpm_dead.is_open());
    }
    {
        file ifpm_closed = file_temp(&ifpm_err);
        ifpm_check("closed-pre", ifpm_closed.is_open());
        ifpm_check("closed-close", ifpm_closed.close(&ifpm_err) == STDLIB_OK);
        ifpm_st = ifpm_closed.read(ifpm_buf, sizeof(ifpm_buf), &ifpm_err);
        ifpm_check("closed-read", ifpm_st == STDLIB_FAIL);
        ifpm_st = ifpm_closed.write_bytes("x", 1, &ifpm_err);
        ifpm_check("closed-write", ifpm_st == STDLIB_FAIL);
        ifpm_st = ifpm_closed.flush(&ifpm_err);
        ifpm_check("closed-flush", ifpm_st == STDLIB_FAIL);
        ifpm_st = ifpm_closed.close(&ifpm_err);
        ifpm_check("closed-reclose",
                   ifpm_st == STDLIB_FAIL &&
                       ifpm_err.category() == STDLIB_CATEGORY_LIFECYCLE);
    }

    /* ---- 4. read-only mode: reads succeed, writes fail, content intact -- */
    {
        {
            file ifpm_seed = file(ifpm_ro, "w");
            ifpm_check("ro-seed-open", ifpm_seed.is_open());
            ifpm_seed.write_bytes("RODATA", 6, &ifpm_err);
            ifpm_seed.close(&ifpm_err);
        }
        {
            file ifpm_r = file(ifpm_ro, "r");
            ifpm_check("ro-open", ifpm_r.is_open());
            memset(ifpm_buf, 0, sizeof(ifpm_buf));
            ifpm_st = ifpm_r.read(ifpm_buf, 6, &ifpm_err);
            ifpm_check("ro-read",
                       ifpm_st == STDLIB_OK && memcmp(ifpm_buf, "RODATA", 6) == 0);
            ifpm_st = ifpm_r.write_bytes("X", 1, &ifpm_err);
            ifpm_check("ro-write-bytes",
                       ifpm_st == STDLIB_FAIL &&
                           ifpm_err.category() == STDLIB_CATEGORY_IO &&
                           ifpm_err.code() == CPLUS_EXIT_IO);
            ifpm_check("ro-write-string", ifpm_r.write("Y") == 0);
        }
        {
            file ifpm_v = file(ifpm_ro, "r");
            memset(ifpm_buf, 0, sizeof(ifpm_buf));
            ifpm_st = ifpm_v.read(ifpm_buf, sizeof(ifpm_buf), &ifpm_err);
            ifpm_check("ro-content-intact",
                       ifpm_st == STDLIB_OK && memcmp(ifpm_buf, "RODATA", 6) == 0);
        }
        ifpm_check("ro-cleanup", remove(ifpm_ro) == 0);
        ifpm_check("ro-absent", ifpm_absent(ifpm_ro));
    }

    /* ---- 5. write-only mode: writes succeed, reads fail ----------------- */
    {
        file ifpm_w = file(ifpm_wo, "w");
        ifpm_check("wo-open", ifpm_w.is_open());
        ifpm_check("wo-write",
                   ifpm_w.write_bytes("WODATA", 6, &ifpm_err) == STDLIB_OK);
        ifpm_st = ifpm_w.read(ifpm_buf, sizeof(ifpm_buf), &ifpm_err);
        ifpm_check("wo-read-fails",
                   ifpm_st == STDLIB_FAIL &&
                       ifpm_err.category() == STDLIB_CATEGORY_IO);
        ifpm_st = ifpm_w.read_line(ifpm_buf, sizeof(ifpm_buf), &ifpm_err);
        ifpm_check("wo-readline-fails", ifpm_st == STDLIB_FAIL);
        ifpm_check("wo-close", ifpm_w.close(&ifpm_err) == STDLIB_OK);
        {
            file ifpm_v = file(ifpm_wo, "r");
            memset(ifpm_buf, 0, sizeof(ifpm_buf));
            ifpm_st = ifpm_v.read(ifpm_buf, sizeof(ifpm_buf), &ifpm_err);
            ifpm_check("wo-content",
                       ifpm_st == STDLIB_OK && memcmp(ifpm_buf, "WODATA", 6) == 0);
        }
        ifpm_check("wo-cleanup", remove(ifpm_wo) == 0);
    }

    /* ---- 6. read/write mode ("r+") supports both directions ------------- */
    {
        {
            file ifpm_seed2 = file(ifpm_rw, "w");
            ifpm_seed2.write_bytes("ABCDEFGH", 8, &ifpm_err);
            ifpm_seed2.close(&ifpm_err);
        }
        {
            file ifpm_rw2 = file(ifpm_rw, "r+");
            ifpm_check("rw-open", ifpm_rw2.is_open());
            memset(ifpm_buf, 0, sizeof(ifpm_buf));
            ifpm_st = ifpm_rw2.read(ifpm_buf, 4, &ifpm_err);
            ifpm_check("rw-read",
                       ifpm_st == STDLIB_OK && memcmp(ifpm_buf, "ABCD", 4) == 0);
            ifpm_check("rw-seek",
                       ifpm_rw2.seek(0, SEEK_SET, &ifpm_err) == STDLIB_OK);
            ifpm_check("rw-write",
                       ifpm_rw2.write_bytes("abcd", 4, &ifpm_err) == STDLIB_OK);
            ifpm_check("rw-flush", ifpm_rw2.flush(&ifpm_err) == STDLIB_OK);
        }
        {
            file ifpm_v2 = file(ifpm_rw, "r");
            memset(ifpm_buf, 0, sizeof(ifpm_buf));
            ifpm_st = ifpm_v2.read(ifpm_buf, sizeof(ifpm_buf), &ifpm_err);
            ifpm_check("rw-verify",
                       ifpm_st == STDLIB_OK && memcmp(ifpm_buf, "abcdEFGH", 8) == 0);
        }
        ifpm_check("rw-cleanup", remove(ifpm_rw) == 0);
        ifpm_probe = fopen(ifpm_rw, "r");
        ifpm_check("rw-absent", ifpm_probe == NULL);
        if (ifpm_probe != NULL)
            fclose(ifpm_probe);
    }

    print("IO_FILE_PARTIAL_MODES", ifpm_checks, ifpm_failures);
    return ifpm_failures == 0 ? 0 : 1;
}
