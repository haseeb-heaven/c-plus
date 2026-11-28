/*
 * test_raii50_file_lifecycle.c
 *
 * RAII / cleanup discipline for the checked stdio handle. A FILE* acquired with
 * cplus_fopen_s() is always released with cplus_fclose_s() on every path: the
 * success path, a failed-open path (handle left NULL), a simulated early-return
 * (goto cleanup), and an error path where a second open fails while the first is
 * still open. The handle is NULL after close; a double close is a defined error,
 * not UB. An open-handle ledger proves nothing is left open. A deterministic LCG
 * runs 2400 open/write/read/close cycles.
 */
#include <stdio.h>
#include <string.h>
#include "safe_io.h"
#include "safe_error.h"
#include "safe_log.h"

static int failures = 0;
static unsigned long checks = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        checks++;                                                              \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,   \
                    #cond);                                                    \
        }                                                                      \
    } while (0)

static unsigned long g_lcg;
static void lcg_seed(unsigned long s) { g_lcg = s ? s : 1UL; }
static unsigned long lcg_next(void) {
    g_lcg = g_lcg * 6364136223846793005UL + 1442695040888963407UL;
    return g_lcg;
}
static unsigned lcg_below(unsigned n) {
    return (unsigned)((lcg_next() >> 33) % (unsigned long)n);
}

static long g_open = 0;
static int open_f(FILE **f, const char *path, const char *mode) {
    FILE *fp = cplus_fopen_s(path, mode);
    *f = fp;
    if (fp != NULL) { g_open++; return CPLUS_SAFE_OK; }
    return CPLUS_SAFE_EINVAL;
}
static int close_f(FILE **f) {
    FILE *raw = *f;
    int rc = cplus_fclose_s(raw);
    if (rc == 0) { *f = NULL; g_open--; return CPLUS_SAFE_OK; }
    return CPLUS_SAFE_EINVAL;
}

static const char *const PATH = "/tmp/cplus_raii50_file_lifecycle.tmp";
static const char *const BADPATH = "/no/such/cplus/raii50/dir/file.tmp";

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    lcg_seed(0x0F11E5UL);

    /* --- NULL argument contract: never dereferenced ----------------------- */
    {
        CHECK(cplus_fopen_s(NULL, "wb") == NULL);
        CHECK(cplus_fopen_s(PATH, NULL) == NULL);
        CHECK(g_open == 0);
    }

    /* --- failed open: handle stays NULL, ledger unchanged ----------------- */
    {
        FILE *f = NULL;
        CHECK(open_f(&f, BADPATH, "rb") != CPLUS_SAFE_OK);
        CHECK(f == NULL);
        CHECK(g_open == 0);
    }

    /* --- fclose on NULL is a defined error -------------------------------- */
    {
        CHECK(cplus_fclose_s(NULL) == EOF);
    }

    /* --- success path: acquire, write, rewind, read, release -------------- */
    {
        FILE *f = NULL;
        char rbuf[8];
        CHECK(open_f(&f, PATH, "wb+") == CPLUS_SAFE_OK);
        CHECK(f != NULL);
        CHECK(g_open == 1);
        CHECK(cplus_fwrite_s("ABCDEFGH", 8, 1, 8, f) == 8);
        rewind(f);
        memset(rbuf, 0, sizeof rbuf);
        CHECK(cplus_fread_s(rbuf, sizeof rbuf, 1, 8, f) == 8);
        CHECK(memcmp(rbuf, "ABCDEFGH", 8) == 0);
        CHECK(close_f(&f) == CPLUS_SAFE_OK);
        CHECK(f == NULL);                 /* handle NULL after release */
        CHECK(g_open == 0);               /* nothing left open         */
    }

    /* --- double close: second is a defined error (EOF) -------------------- */
    {
        FILE *f = NULL;
        CHECK(open_f(&f, PATH, "wb") == CPLUS_SAFE_OK);
        {
            FILE *raw = f;
            CHECK(close_f(&f) == CPLUS_SAFE_OK);
            CHECK(f == NULL);
            CHECK(cplus_fclose_s(raw) == EOF);
        }
        CHECK(g_open == 0);
    }

    /* --- simulated early-return: cleanup label closes the handle ---------- */
    {
        FILE *f = NULL;
        volatile int bail = 1;
        CHECK(open_f(&f, PATH, "wb+") == CPLUS_SAFE_OK);
        CHECK(f != NULL);
        if (bail) goto cleanup;
        cplus_fwrite_s("x", 1, 1, 1, f);   /* skipped work */
    cleanup:
        CHECK(close_f(&f) == CPLUS_SAFE_OK);
        CHECK(f == NULL);
        CHECK(g_open == 0);
    }

    /* --- error path: first open succeeds, second fails, first is closed --- */
    {
        FILE *good = NULL;
        FILE *bad = NULL;
        CHECK(open_f(&good, PATH, "wb+") == CPLUS_SAFE_OK);
        CHECK(g_open == 1);
        CHECK(open_f(&bad, BADPATH, "rb") != CPLUS_SAFE_OK);
        CHECK(bad == NULL);
        CHECK(g_open == 1);               /* only `good` is open */
        CHECK(close_f(&good) == CPLUS_SAFE_OK);
        CHECK(good == NULL);
        CHECK(g_open == 0);
    }

    /* --- randomized open -> write -> read -> close, 2400 cycles ----------- */
    {
        unsigned iter;
        for (iter = 0; iter < 2400; ++iter) {
            FILE *f = NULL;
            char wbuf[4];
            char rbuf[4];
            size_t i;
            for (i = 0; i < 4; ++i) wbuf[i] = (char)('a' + (lcg_below(26)));

            CHECK(open_f(&f, PATH, "wb+") == CPLUS_SAFE_OK);
            CHECK(f != NULL);
            CHECK(g_open == 1);
            CHECK(cplus_fwrite_s(wbuf, sizeof wbuf, 1, 4, f) == 4);
            rewind(f);
            memset(rbuf, 0, sizeof rbuf);
            CHECK(cplus_fread_s(rbuf, sizeof rbuf, 1, 4, f) == 4);
            CHECK(memcmp(rbuf, wbuf, 4) == 0);
            CHECK(close_f(&f) == CPLUS_SAFE_OK);
            CHECK(f == NULL);
            CHECK(g_open == 0);
        }
    }

    remove(PATH);

    CHECK(g_open == 0);
    if (failures == 0) {
        fprintf(stderr, "test_raii50_file_lifecycle: %lu assertions\n", checks);
        printf("test_raii50_file_lifecycle: ok\n");
        return 0;
    }
    printf("test_raii50_file_lifecycle: FAILURES=%d\n", failures);
    return 1;
}
