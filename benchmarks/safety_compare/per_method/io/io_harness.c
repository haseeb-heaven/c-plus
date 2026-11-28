/*
 * io_harness.c -- deterministic per-method C+ checked-I/O safety probe.
 *
 * Scope: only the checked I/O methods exported by libcplus_safecrt.a:
 *   cplus_fopen_s, cplus_fclose_s, cplus_fread_s, cplus_fwrite_s.
 * cplus_tmpfile_s is NOT exported by either archive and is therefore not
 * probed here (see NOTES.md for the nm evidence and the BLOCKED marking).
 *
 * Each probe prints exactly one deterministic line:
 *   <method> <case> <token>
 * where <token> is OK (operation succeeded as expected) or REFUSED (the
 * checked API rejected/bounded a bad input rather than performing UB).
 *
 * Tokens are derived from OBSERVED return values and canary bytes, never
 * hard-coded. A genuine library regression (canary clobber, missing bound)
 * therefore flips a token and is detected by compare.sh as a real mismatch.
 *
 * Build:
 *   /usr/bin/clang -std=c99 \
 *     -Ilibc/install-include -Ithird_party/safeclib/include \
 *     io_harness.c \
 *     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
 *     -lm -pthread -o io_harness
 */

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Checked I/O API (stdlib/safe/safe_io.h), declared here so the probe needs
 * no private headers and no unprefixed macro aliases. */
FILE *cplus_fopen_s(const char *name, const char *mode);
int cplus_fclose_s(FILE *stream);
size_t cplus_fread_s(void *buf, size_t bmax, size_t size, size_t count,
                     FILE *stream);
size_t cplus_fwrite_s(const void *buf, size_t bmax, size_t size, size_t count,
                      FILE *stream);

#define CANARY_BYTE ((unsigned char)0x5Au)
#define CANARY_SZ 4u
#define BUFSZ 8u
#define RAW_SZ (CANARY_SZ + BUFSZ + CANARY_SZ)

static void canary_fill(unsigned char *raw) {
    memset(raw, CANARY_BYTE, RAW_SZ);
}

/* Check ONLY the guard regions, never the [CANARY_SZ, CANARY_SZ+BUFSZ)
 * data region (which intentionally holds non-canary payload bytes). */
static int canary_intact(const unsigned char *raw) {
    size_t i;
    for (i = 0; i < CANARY_SZ; ++i) {
        if (raw[i] != CANARY_BYTE) return 0;
    }
    for (i = CANARY_SZ + BUFSZ; i < RAW_SZ; ++i) {
        if (raw[i] != CANARY_BYTE) return 0;
    }
    return 1;
}

/* An expected-success case: "OK" when the operation succeeded. */
static void emit_ok(const char *method, const char *case_name, int success) {
    printf("%s %s %s\n", method, case_name, success ? "OK" : "REFUSED");
}

/* An expected-guard case: "REFUSED" when the API correctly refused/bounded
 * the bad input. If the guard fails we print "OK", which is a real mismatch
 * against the Rust side and makes compare.sh exit nonzero. */
static void emit_refused(const char *method, const char *case_name,
                         int refused) {
    printf("%s %s %s\n", method, case_name, refused ? "REFUSED" : "OK");
}

int main(void) {
    unsigned char raw[RAW_SZ];
    unsigned char *buf = raw + CANARY_SZ;
    char path[128];
    FILE *seed;
    FILE *f;
    FILE *bio;
    size_t r;
    int ok;

    /* A real named file, created deterministically, for fopen_s valid_open. */
    snprintf(path, sizeof path, "cplus_io_probe_%ld.tmp", (long)getpid());
    seed = fopen(path, "wb");
    if (seed != NULL) {
        canary_fill(raw);
        (void)fwrite(buf, 1, BUFSZ, seed);
        (void)fwrite(buf, 1, BUFSZ, seed);
        fclose(seed);
    }

    /* ---------------- fopen_s ---------------- */
    emit_refused("fopen_s", "null_path", cplus_fopen_s(NULL, "rb") == NULL);
    emit_refused("fopen_s", "null_mode", cplus_fopen_s(path, NULL) == NULL);

    f = cplus_fopen_s(path, "rb"); /* valid open on a real file */
    emit_ok("fopen_s", "valid_open", f != NULL);
    if (f == NULL) {
        remove(path);
        return 1;
    }

    /* ---------------- fread_s ----------------
     * Use a real tmpfile() stream, seeded with 16 deterministic bytes, so the
     * bound checks apply to actual file I/O rather than a mock. */
    bio = tmpfile();
    if (bio == NULL) {
        emit_ok("fread_s", "valid_read", 0);
        (void)cplus_fclose_s(f);
        remove(path);
        return 1;
    }
    canary_fill(raw);
    memset(buf, 0x11, BUFSZ);
    (void)fwrite(buf, 1, BUFSZ, bio);
    (void)fwrite(buf, 1, BUFSZ, bio);
    rewind(bio);

    canary_fill(raw);
    r = cplus_fread_s(buf, BUFSZ, 1, BUFSZ, bio);
    ok = (r == BUFSZ) && canary_intact(raw) && buf[0] == 0x11u;
    emit_ok("fread_s", "valid_read", ok);

    canary_fill(raw);
    r = cplus_fread_s(NULL, BUFSZ, 1, 1, bio);
    emit_refused("fread_s", "null_buf", r == 0);

    canary_fill(raw);
    r = cplus_fread_s(buf, BUFSZ, 1, 1, NULL);
    emit_refused("fread_s", "null_stream", r == 0);

    canary_fill(raw);
    r = cplus_fread_s(buf, 0, 1, 1, bio); /* zero buffer bound */
    emit_refused("fread_s", "zero_bound", r == 0);

    canary_fill(raw);
    rewind(bio);
    r = cplus_fread_s(buf, BUFSZ, 1, BUFSZ + 1, bio); /* one over bound */
    ok = (r <= BUFSZ) && canary_intact(raw);
    emit_refused("fread_s", "one_over_bound", ok);

    emit_ok("fread_s", "canary_intact", canary_intact(raw));

    /* ---------------- fwrite_s ---------------- */
    canary_fill(raw);
    memset(buf, 0x22, BUFSZ);
    r = cplus_fwrite_s(buf, BUFSZ, 1, BUFSZ, bio);
    ok = (r == BUFSZ) && canary_intact(raw);
    emit_ok("fwrite_s", "valid_write", ok);

    canary_fill(raw);
    r = cplus_fwrite_s(NULL, BUFSZ, 1, 1, bio);
    emit_refused("fwrite_s", "null_buf", r == 0);

    canary_fill(raw);
    r = cplus_fwrite_s(buf, BUFSZ, 1, 1, NULL);
    emit_refused("fwrite_s", "null_stream", r == 0);

    canary_fill(raw);
    r = cplus_fwrite_s(buf, 0, 1, 1, bio); /* zero buffer bound */
    emit_refused("fwrite_s", "zero_bound", r == 0);

    canary_fill(raw);
    r = cplus_fwrite_s(buf, BUFSZ, 1, BUFSZ + 1, bio); /* one over bound */
    ok = (r <= BUFSZ) && canary_intact(raw);
    emit_refused("fwrite_s", "one_over_bound", ok);

    emit_ok("fwrite_s", "canary_intact", canary_intact(raw));

    /* ---------------- fclose_s ---------------- */
    emit_ok("fclose_s", "valid_close", cplus_fclose_s(bio) == 0);
    emit_refused("fclose_s", "double_close", cplus_fclose_s(bio) == EOF);
    emit_refused("fclose_s", "null_stream", cplus_fclose_s(NULL) == EOF);

    (void)cplus_fclose_s(f);
    remove(path);
    return 0;
}
