/* End-to-end integration program for the C+ toolchain.
 *
 * Built by the real driver (`./build/c+ build --release`) and linked against
 * BOTH runtime libraries: build/safe/libcplus_safecrt.a (checked SafeCRT) and
 * build/libc/libcplus_hardened.a (hardened libc).  Ordinary C spellings are
 * deliberately kept in the source; safe_c.h redirects them to the checked
 * implementations while the explicit `_s` entry points are called directly as
 * well.  The same scenario battery runs twice (SafeCRT logging OFF then TRACE)
 * and the two result snapshots must be byte-for-byte identical.
 *
 * The I/O scratch path is argv[1] so the runner owns the location; no user or
 * machine-specific path is baked into this source.
 */
#define _POSIX_C_SOURCE 200809L

#include "stdlib/safe/safe_c.h"

#include "stdlib/safe/safe_error.h"
#include "stdlib/safe/safe_log.h"
#include "stdlib/safe/safe_runtime.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* Safeclib constraint code: not enough space (destination too small). */
#define CPLUS_INTEGRATION_ESNOSPC 406

/* Every observable outcome of one run is recorded here.  The two runs (logging
 * off / logging on) are compared with ordinary memcmp in main(). */
enum {
    R_LIVE_BASE = 0,
    R_BYTES_BASE,
    R_META,
    R_EXPLICIT_MALLOC_S,
    R_TRACKED_SIZE,
    R_LIVE_AFTER_ALLOC,
    R_BYTES_AFTER_ALLOC,
    R_MEMSET,
    R_MEMCPY,
    R_MEMMOVE,
    R_MEMCMP,
    R_STRCPY,
    R_STRNLEN,
    R_SNPRINTF,
    R_FILE_WRITE,
    R_FILE_READ,
    R_FILE_CMP,
    R_CHECKED_FOPEN,
    R_OVERFLOW_MEMSET,
    R_OVERFLOW_ERRNO,
    R_CHECKED_MEMCPY_RC,
    R_CHECKED_MEMCPY_KEEP,
    R_DOUBLE_FREE_FIRST,
    R_DOUBLE_FREE_SECOND,
    R_FORMAT_N_RC,
    R_FORMAT_N_KEEP,
    R_LIVE_END,
    R_BYTES_END,
    R_COUNT
};

static int all_bytes_are(const unsigned char *p, size_t n, unsigned char value) {
    size_t i;
    for (i = 0; i < n; ++i)
        if (p[i] != value)
            return 0;
    return 1;
}

/* One complete, side-effect-bounded scenario battery.  Does not print; main()
 * prints the per-scenario outcome lines from the returned snapshot. */
static void run_core(const char *path, long *r) {
    unsigned char *buf;
    unsigned char *zero;
    unsigned char *big;
    unsigned char *big_grown;
    unsigned char *df;
    unsigned char *explicit;
    unsigned char back[8];
    unsigned char checked_dst[8];
    char fmt_dst[16];
    char text[16];
    char formatted[32];
    cplus_safe_alloc_meta meta;
    FILE *f;
    FILE *checked_f;
    size_t w, nread;
    int n, nrc, kept, count = 0;
    size_t i;

    for (i = 0; i < R_COUNT; ++i)
        r[i] = 0;

    r[R_LIVE_BASE] = (long)cplus_safe_alloc_live_count();
    r[R_BYTES_BASE] = (long)cplus_safe_alloc_live_bytes();

    /* Ordinary allocation spellings -> tracked malloc_s/calloc_s/realloc_s. */
    buf = (unsigned char *)malloc(8);
    zero = (unsigned char *)calloc(2, 4);
    big = (unsigned char *)malloc(16);
    if (!buf || !zero || !big) {
        fprintf(stderr, "integration: allocation failed\n");
        free(buf);
        free(zero);
        free(big);
        return;
    }
    /* A distinct result variable keeps the ordinary realloc spelling while
     * never overwriting the original pointer on a failure path. */
    big_grown = (unsigned char *)realloc(big, 16);
    if (!big_grown) {
        fprintf(stderr, "integration: realloc failed\n");
        free(buf);
        free(zero);
        free(big);
        return;
    }
    big = big_grown;

    /* Allocation metadata and live-count consistency. */
    r[R_META] = (cplus_safe_alloc_get_meta(buf, &meta) == 0 && meta.is_live &&
                meta.byte_size == 8 && meta.elem_size == 1) ? 1 : 0;
    r[R_TRACKED_SIZE] = (long)cplus_safe_ptr_size(buf);
    r[R_LIVE_AFTER_ALLOC] = (long)cplus_safe_alloc_live_count();
    r[R_BYTES_AFTER_ALLOC] = (long)cplus_safe_alloc_live_bytes();

    /* Explicit checked entry point alongside the ordinary spellings above. */
    explicit = (unsigned char *)malloc_s(4);
    r[R_EXPLICIT_MALLOC_S] =
        (explicit != NULL && cplus_safe_ptr_size(explicit) == 4) ? 1 : 0;
    free(explicit);

    /* In-bounds raw memory operations, all via the ordinary C spellings. */
    memset(buf, 0xA5, 8);
    r[R_MEMSET] = all_bytes_are(buf, 8, 0xA5) ? 1 : 0;
    memcpy(big, buf, 8);
    r[R_MEMCPY] = (memcmp(big, buf, 8) == 0) ? 1 : 0;
    memmove(big + 4, big, 8); /* overlapping, but memmove is defined */
    r[R_MEMMOVE] = all_bytes_are(big + 4, 8, 0xA5) ? 1 : 0;
    r[R_MEMCMP] = (memcmp(buf, buf, 8) == 0 && memcmp(buf, zero, 4) != 0) ? 1 : 0;

    /* Ordinary string calls (strcpy is bounded by safe_c.h; strnlen is a
     * bounded query, also exercised through its checked strnlen_s form). */
    strcpy(text, "integration");
    r[R_STRCPY] = (text[0] == 'i' && text[10] == 'n' && text[11] == '\0') ? 1 : 0;
    r[R_STRNLEN] = (long)strnlen(text, sizeof text);
    if (strnlen_s(text, sizeof text) != (rsize_t)r[R_STRNLEN])
        r[R_STRNLEN] = -1;

    /* Ordinary snprintf and printf spellings. */
    n = snprintf(formatted, sizeof formatted, "n=%d", 42);
    r[R_SNPRINTF] = (n == 4 && formatted[0] == 'n' && formatted[3] == '2') ? 1 : 0;

    /* Ordinary fopen/fwrite/fread/fclose round trip. */
    f = fopen(path, "wb+");
    if (!f) {
        free(buf);
        free(zero);
        free(big);
        return;
    }
    w = fwrite(buf, 1, 8, f);
    rewind(f);
    memset(back, 0, sizeof back);
    nread = fread(back, 1, 8, f);
    fclose(f);
    r[R_FILE_WRITE] = (long)w;
    r[R_FILE_READ] = (long)nread;
    r[R_FILE_CMP] = (memcmp(back, buf, 8) == 0) ? 1 : 0;

    /* Checked stream entry point. */
    checked_f = fopen_s(path, "rb");
    r[R_CHECKED_FOPEN] = (checked_f != NULL) ? 1 : 0;
    if (checked_f)
        fclose_s(checked_f);

    /* Refused over-capacity write through the ordinary memset spelling: the
     * tracked 8-byte block is asked to receive 16 bytes.  With the SafeCRT
     * violation trap disabled this returns without touching the destination. */
    errno = 0;
    memset(buf, 0x41, 16);
    r[R_OVERFLOW_MEMSET] = all_bytes_are(buf, 8, 0xA5) ? 1 : 0;
    r[R_OVERFLOW_ERRNO] = (errno == ERANGE) ? 1 : 0;

    /* Refused over-capacity write through the checked memcpy_s API: dmax=4
     * while smax=8 must return ESNOSPC (406) and leave the destination alone. */
    memset(checked_dst, 0x5A, sizeof checked_dst);
    r[R_CHECKED_MEMCPY_RC] = (long)memcpy_s(checked_dst, 4, big, 8);
    /* C+ safeclib never clears the destination on a refused write: dmax is
     * untrusted and a wild clear is itself a memory-safety bug.  The whole
     * destination must survive unchanged and no source byte (0xA5) may
     * appear in it. */
    r[R_CHECKED_MEMCPY_KEEP] = all_bytes_are(checked_dst, 8, 0x5A) ? 1 : 0;

    /* Double-free refusal via the checked free API. */
    df = (unsigned char *)malloc(16);
    if (!df) {
        free(buf);
        free(zero);
        free(big);
        return;
    }
    r[R_DOUBLE_FREE_FIRST] = (long)free_checked_s(df);
    r[R_DOUBLE_FREE_SECOND] = (long)free_checked_s(df);

    /* Checked formatting must refuse a forbidden %n specifier.  The literal
     * passes the CWE-134 literal gate, then fails the %n gate. */
    memset(fmt_dst, 0x5A, sizeof fmt_dst);
    nrc = snprintf_s(fmt_dst, sizeof fmt_dst, "n=%n", &count);
    r[R_FORMAT_N_RC] = (long)nrc;
    kept = 1;
    for (i = 0; i < sizeof fmt_dst; ++i)
        if ((unsigned char)fmt_dst[i] != 0x5A)
            kept = 0;
    r[R_FORMAT_N_KEEP] = kept;

    /* Release ordinary allocations; the live table must return to baseline. */
    free(buf);
    free(zero);
    free(big);
    r[R_LIVE_END] = (long)cplus_safe_alloc_live_count();
    r[R_BYTES_END] = (long)cplus_safe_alloc_live_bytes();
}

static int report(const char *scenario, int ok) {
    printf("scenario %-28s %s\n", scenario, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "build/integration/app_io.bin";
    long off[R_COUNT], on[R_COUNT];
    int failures = 0;

    /* Refusals are survivable in this suite; without this, SafeCRT would
     * terminate on the first intentional violation (fail-stop default). */
    cplus_runtime_set_violation_trap(0);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    printf("scenario driver+runtime+libs    linked\n");
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    run_core(path, off);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_TRACE);
    run_core(path, on);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);

    failures += report("ordinary allocation+metadata",
                       off[R_META] == 1 && off[R_EXPLICIT_MALLOC_S] == 1 &&
                       off[R_TRACKED_SIZE] == 8 &&
                       off[R_LIVE_AFTER_ALLOC] == off[R_LIVE_BASE] + 3 &&
                       off[R_BYTES_AFTER_ALLOC] == off[R_BYTES_BASE] + 32);
    failures += report("memset success", off[R_MEMSET] == 1);
    failures += report("memcpy success", off[R_MEMCPY] == 1);
    failures += report("memmove success", off[R_MEMMOVE] == 1);
    failures += report("memcmp success", off[R_MEMCMP] == 1);
    failures += report("strcpy+strnlen success",
                       off[R_STRCPY] == 1 && off[R_STRNLEN] == 11);
    failures += report("snprintf+printf success", off[R_SNPRINTF] == 1);
    failures += report("fopen/fwrite/fread/fclose",
                       off[R_FILE_WRITE] == 8 && off[R_FILE_READ] == 8 &&
                       off[R_FILE_CMP] == 1);
    failures += report("checked fopen_s", off[R_CHECKED_FOPEN] == 1);
    failures += report("over-capacity write refused",
                       off[R_OVERFLOW_MEMSET] == 1 && off[R_OVERFLOW_ERRNO] == 1);
    failures += report("checked memcpy_s refused",
                       off[R_CHECKED_MEMCPY_RC] == CPLUS_INTEGRATION_ESNOSPC &&
                       off[R_CHECKED_MEMCPY_KEEP] == 1);
    failures += report("double-free refused",
                       off[R_DOUBLE_FREE_FIRST] == CPLUS_SAFE_OK &&
                       off[R_DOUBLE_FREE_SECOND] == CPLUS_SAFE_EINVAL);
    failures += report("checked %n rejection",
                       off[R_FORMAT_N_RC] < 0 && off[R_FORMAT_N_KEEP] == 1);
    failures += report("live-count consistency",
                       off[R_LIVE_END] == off[R_LIVE_BASE] &&
                       off[R_BYTES_END] == off[R_BYTES_BASE]);
    failures += report("logging off/trace identical",
                       memcmp(off, on, sizeof off) == 0);

    printf("failures: %d\n", failures);
    if (failures != 0) {
        printf("integration: FAILED\n");
        return 1;
    }
    printf("integration: ok\n");
    return 0;
}
