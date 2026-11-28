/* C99 harness for the checked file/stdio APIs, mirroring io.rs case-for-case.
 *
 * Each checked API is called for the same cases as the safe-Rust examples and
 * the normalized outcome is emitted as `<api> <case> <token>` on stderr, so
 * compare.sh can diff it against io.rs. stdout is reserved for the printf_s
 * demonstration (discarded by compare.sh).
 *
 * The `_s` API family is provided by build/safe/libcplus_safecrt.a under its
 * real `cplus_*` symbols (the unprefixed fopen_s/fread_s/... are macros in
 * safe_io.h / safe_format.h). The `cplus_fopen/fclose/fread/fwrite/tmpfile`
 * family is provided by build/libc/libcplus_hardened.a. Both archives are
 * linked simultaneously; they define disjoint global symbols.
 *
 * No user name, home directory, or hard-coded user path is used: every file
 * lives in a unique directory under TMPDIR (falling back to /tmp).
 */
#define _POSIX_C_SOURCE 200112L
#define CPLUS_SAFE_NO_UNPREFIXED_MACROS

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "safe_io.h"     /* cplus_fopen_s, cplus_fclose_s, cplus_fread_s, cplus_fwrite_s */
#include "safe_format.h" /* cplus_printf_s, cplus_fprintf_s, cplus_scanf_s, cplus_fscanf_s */

/* Hardened-libc family (libc/install-include/cplus_libc.h). */
extern FILE *cplus_fopen(const char *path, const char *mode);
extern int cplus_fclose(FILE *stream);
extern size_t cplus_fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
extern size_t cplus_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
extern FILE *cplus_tmpfile(void);

static const unsigned char BIN[8] = {0x00, 0x01, 0x02, 0x03, 0xff, 0xfe, 0x80, 0x7f};
static char g_root[512];

static void emit(const char *api, const char *case_name, const char *token) {
    fprintf(stderr, "%s %s %s\n", api, case_name, token);
}

static void make_root(void) {
    const char *td = getenv("TMPDIR");
    if (!td || !*td) td = "/tmp";
    snprintf(g_root, sizeof g_root, "%s/cplus_c_io_%ld_%u",
             td, (long)getpid(), (unsigned)time(NULL));
    if (mkdir(g_root, 0700) != 0 && errno != EEXIST) {
        perror("mkdir");
        exit(2);
    }
}

static void mkpath(char *out, size_t n, const char *name) {
    snprintf(out, n, "%s/%s", g_root, name);
}

static void write_file(const char *p, const void *d, size_t n) {
    FILE *f = fopen(p, "wb");
    if (!f) { perror("fopen fixture"); exit(2); }
    if (n) fwrite(d, 1, n, f);
    fclose(f);
}

static size_t file_size(const char *p) {
    struct stat st;
    if (stat(p, &st) != 0) return (size_t)-1;
    return (size_t)st.st_size;
}

int main(void) {
    char p[600], pb[600];
    make_root();

    /* Shared fixtures (ASCII + binary), mirroring io.rs. */
    mkpath(p, sizeof p, "fixture_ascii");
    write_file(p, "ABCD", 4);
    char ascii[600];
    snprintf(ascii, sizeof ascii, "%s", p);
    mkpath(pb, sizeof pb, "fixture_bin");
    write_file(pb, BIN, sizeof BIN);
    char binary[600];
    snprintf(binary, sizeof binary, "%s", pb);

    /* ------------------------------------------------------------ safe CRT */
    /* fopen_s */
    {
        FILE *f = cplus_fopen_s(ascii, "r");
        emit("fopen_s", "valid", f ? "ok" : "null");
        if (f) cplus_fclose_s(f);
    }
    {
        FILE *f = cplus_fopen_s(NULL, "r");
        emit("fopen_s", "null-path", f ? "ok" : "null");
        if (f) cplus_fclose_s(f);
    }

    /* fclose_s */
    {
        mkpath(p, sizeof p, "fc_valid");
        write_file(p, "x", 1);
        FILE *f = cplus_fopen_s(p, "r");
        int rc = cplus_fclose_s(f);
        emit("fclose_s", "valid", rc == 0 ? "closed" : "mismatch");
    }
    {
        int rc = cplus_fclose_s(NULL);
        emit("fclose_s", "null-stream", rc == EOF ? "null" : "mismatch");
    }
    {
        mkpath(p, sizeof p, "fc_dc");
        write_file(p, "x", 1);
        FILE *f = cplus_fopen_s(p, "r");
        int rc1 = cplus_fclose_s(f);
        int rc2 = cplus_fclose_s(f); /* closed-marker check precedes fclose */
        emit("fclose_s", "double-close",
             (rc1 == 0 && rc2 == EOF) ? "refused" : "mismatch");
    }

    /* fread_s */
    {
        FILE *f = cplus_fopen_s(ascii, "r");
        char buf[4] = {0};
        size_t n = cplus_fread_s(buf, 4, 1, 4, f);
        emit("fread_s", "valid",
             (n == 4 && memcmp(buf, "ABCD", 4) == 0) ? "ok" : "mismatch");
        if (f) cplus_fclose_s(f);
    }
    {
        char buf[8];
        size_t n = cplus_fread_s(buf, sizeof buf, 1, 1, NULL);
        emit("fread_s", "null-stream", n == 0 ? "null" : "mismatch");
    }
    {
        FILE *f = cplus_fopen_s(ascii, "r");
        size_t n = cplus_fread_s(NULL, 8, 1, 1, f);
        emit("fread_s", "null-buf", n == 0 ? "null" : "mismatch");
        if (f) cplus_fclose_s(f);
    }
    {
        FILE *f = cplus_fopen_s(ascii, "r");
        char buf[8];
        size_t n = cplus_fread_s(buf, 8, 1, 0, f);
        emit("fread_s", "zero", n == 0 ? "zero" : "mismatch");
        if (f) cplus_fclose_s(f);
    }
    {
        FILE *f = cplus_fopen_s(ascii, "r");
        char buf[3] = {0};
        size_t n = cplus_fread_s(buf, 3, 1, 4, f); /* bmax=3 clamps count=4 */
        emit("fread_s", "one-over", n == 3 ? "clamped" : "mismatch");
        if (f) cplus_fclose_s(f);
    }
    {
        FILE *f = cplus_fopen_s(binary, "rb");
        unsigned char buf[8] = {0};
        size_t n = cplus_fread_s(buf, 8, 1, 8, f);
        emit("fread_s", "binary",
             (n == 8 && memcmp(buf, BIN, sizeof BIN) == 0) ? "roundtrip" : "mismatch");
        if (f) cplus_fclose_s(f);
    }

    /* fwrite_s */
    {
        mkpath(p, sizeof p, "fw_valid");
        FILE *f = cplus_fopen_s(p, "wb");
        size_t n = cplus_fwrite_s("ABCD", 4, 1, 4, f);
        emit("fwrite_s", "valid", n == 4 ? "ok" : "mismatch");
        if (f) cplus_fclose_s(f);
    }
    {
        size_t n = cplus_fwrite_s("x", 1, 1, 1, NULL);
        emit("fwrite_s", "null-stream", n == 0 ? "null" : "mismatch");
    }
    {
        mkpath(p, sizeof p, "fw_nullbuf");
        FILE *f = cplus_fopen_s(p, "wb");
        size_t n = cplus_fwrite_s(NULL, 8, 1, 1, f);
        emit("fwrite_s", "null-buf", n == 0 ? "null" : "mismatch");
        if (f) cplus_fclose_s(f);
    }
    {
        mkpath(p, sizeof p, "fw_zero");
        FILE *f = cplus_fopen_s(p, "wb");
        size_t n = cplus_fwrite_s("x", 8, 1, 0, f);
        emit("fwrite_s", "zero", n == 0 ? "zero" : "mismatch");
        if (f) cplus_fclose_s(f);
    }
    {
        mkpath(p, sizeof p, "fw_over");
        FILE *f = cplus_fopen_s(p, "wb");
        size_t n = cplus_fwrite_s("ABCD", 3, 1, 4, f); /* bmax=3 clamps count=4 */
        if (f) cplus_fclose_s(f);
        emit("fwrite_s", "one-over",
             (n == 3 && file_size(p) == 3) ? "clamped" : "mismatch");
    }
    {
        mkpath(p, sizeof p, "fw_bin");
        FILE *f = cplus_fopen_s(p, "wb");
        size_t n = cplus_fwrite_s(BIN, sizeof BIN, 1, sizeof BIN, f);
        if (f) cplus_fclose_s(f);
        unsigned char rb[8] = {0};
        FILE *r = fopen(p, "rb");
        size_t rn = r ? fread(rb, 1, sizeof rb, r) : 0;
        if (r) fclose(r);
        emit("fwrite_s", "binary",
             (n == sizeof BIN && rn == sizeof BIN && memcmp(rb, BIN, sizeof BIN) == 0)
                 ? "roundtrip" : "mismatch");
    }

    /* fprintf_s */
    {
        mkpath(p, sizeof p, "fp_valid");
        FILE *f = cplus_fopen_s(p, "wb");
        int n = cplus_fprintf_s(f, "io-%d", 42);
        if (f) cplus_fclose_s(f);
        char rb[32] = {0};
        FILE *r = fopen(p, "rb");
        size_t rn = r ? fread(rb, 1, sizeof rb - 1, r) : 0;
        if (r) fclose(r);
        emit("fprintf_s", "valid",
             (n > 0 && rn == 5 && memcmp(rb, "io-42", 5) == 0) ? "ok" : "mismatch");
    }
    {
        int n = cplus_fprintf_s(NULL, "x");
        emit("fprintf_s", "null-stream", n < 0 ? "null" : "mismatch");
    }

    /* fscanf_s */
    {
        mkpath(p, sizeof p, "fs_valid");
        write_file(p, "io 42\n", 6);
        FILE *f = cplus_fopen_s(p, "r");
        char w[32] = {0};
        int num = 0;
        int n = f ? cplus_fscanf_s(f, "%31s %d", w, &num) : -1;
        int ok = (n == 2 && strcmp(w, "io") == 0 && num == 42);
        emit("fscanf_s", "valid", ok ? "ok" : "mismatch");
        if (f) cplus_fclose_s(f);
    }
    {
        char w[32] = {0};
        int n = cplus_fscanf_s(NULL, "%31s", w);
        emit("fscanf_s", "null-stream", n < 0 ? "null" : "mismatch");
    }

    /* printf_s */
    {
        int n = cplus_printf_s("io 42\n"); /* stdout demo, discarded by compare.sh */
        emit("printf_s", "valid", n > 0 ? "ok" : "mismatch");
    }
    {
        int n = cplus_printf_s(NULL);
        emit("printf_s", "null-format", n < 0 ? "null" : "mismatch");
    }

    /* scanf_s */
    {
        char w[32] = {0};
        int num = 0;
        int n = cplus_scanf_s("%31s %d", w, &num); /* stdin fed by compare.sh */
        int ok = (n == 2 && strcmp(w, "io") == 0 && num == 42);
        emit("scanf_s", "valid", ok ? "ok" : "mismatch");
    }
    {
        int n = cplus_scanf_s(NULL);
        emit("scanf_s", "null-format", n < 0 ? "null" : "mismatch");
    }

    /* --------------------------------------------------------- hardened libc */
    /* cplus_fopen */
    {
        FILE *f = cplus_fopen(ascii, "r");
        emit("cplus_fopen", "valid", f ? "ok" : "null");
        if (f) cplus_fclose(f);
    }
    {
        FILE *f = cplus_fopen(NULL, "r");
        emit("cplus_fopen", "null-path", f ? "ok" : "null");
        if (f) cplus_fclose(f);
    }

    /* cplus_fclose */
    {
        mkpath(p, sizeof p, "cfc_valid");
        write_file(p, "x", 1);
        FILE *f = cplus_fopen(p, "r");
        int rc = cplus_fclose(f);
        emit("cplus_fclose", "valid", rc == 0 ? "closed" : "mismatch");
    }
    {
        int rc = cplus_fclose(NULL);
        emit("cplus_fclose", "null-stream", rc == EOF ? "null" : "mismatch");
    }
    {
        mkpath(p, sizeof p, "cfc_dc");
        write_file(p, "x", 1);
        FILE *f = cplus_fopen(p, "r");
        int rc1 = cplus_fclose(f);
        int rc2 = cplus_fclose(f);
        emit("cplus_fclose", "double-close",
             (rc1 == 0 && rc2 == EOF) ? "refused" : "mismatch");
    }

    /* cplus_fread */
    {
        FILE *f = cplus_fopen(ascii, "r");
        char buf[4] = {0};
        size_t n = cplus_fread(buf, 1, 4, f);
        emit("cplus_fread", "valid",
             (n == 4 && memcmp(buf, "ABCD", 4) == 0) ? "ok" : "mismatch");
        if (f) cplus_fclose(f);
    }
    {
        char buf[8];
        size_t n = cplus_fread(buf, 1, 1, NULL);
        emit("cplus_fread", "null-stream", n == 0 ? "null" : "mismatch");
    }
    {
        FILE *f = cplus_fopen(ascii, "r");
        size_t n = cplus_fread(NULL, 1, 1, f);
        emit("cplus_fread", "null-buf", n == 0 ? "null" : "mismatch");
        if (f) cplus_fclose(f);
    }
    {
        FILE *f = cplus_fopen(ascii, "r");
        char buf[8];
        size_t n = cplus_fread(buf, 1, 0, f);
        emit("cplus_fread", "zero", n == 0 ? "zero" : "mismatch");
        if (f) cplus_fclose(f);
    }
    {
        FILE *f = cplus_fopen(binary, "rb");
        unsigned char buf[8] = {0};
        size_t n = cplus_fread(buf, 1, sizeof BIN, f);
        emit("cplus_fread", "binary",
             (n == sizeof BIN && memcmp(buf, BIN, sizeof BIN) == 0) ? "roundtrip" : "mismatch");
        if (f) cplus_fclose(f);
    }

    /* cplus_fwrite */
    {
        mkpath(p, sizeof p, "cfw_valid");
        FILE *f = cplus_fopen(p, "wb");
        size_t n = cplus_fwrite("ABCD", 1, 4, f);
        emit("cplus_fwrite", "valid", n == 4 ? "ok" : "mismatch");
        if (f) cplus_fclose(f);
    }
    {
        size_t n = cplus_fwrite("x", 1, 1, NULL);
        emit("cplus_fwrite", "null-stream", n == 0 ? "null" : "mismatch");
    }
    {
        mkpath(p, sizeof p, "cfw_nullbuf");
        FILE *f = cplus_fopen(p, "wb");
        size_t n = cplus_fwrite(NULL, 1, 1, f);
        emit("cplus_fwrite", "null-buf", n == 0 ? "null" : "mismatch");
        if (f) cplus_fclose(f);
    }
    {
        mkpath(p, sizeof p, "cfw_zero");
        FILE *f = cplus_fopen(p, "wb");
        size_t n = cplus_fwrite("x", 1, 0, f);
        emit("cplus_fwrite", "zero", n == 0 ? "zero" : "mismatch");
        if (f) cplus_fclose(f);
    }
    {
        mkpath(p, sizeof p, "cfw_bin");
        FILE *f = cplus_fopen(p, "wb");
        size_t n = cplus_fwrite(BIN, 1, sizeof BIN, f);
        if (f) cplus_fclose(f);
        unsigned char rb[8] = {0};
        FILE *r = fopen(p, "rb");
        size_t rn = r ? fread(rb, 1, sizeof rb, r) : 0;
        if (r) fclose(r);
        emit("cplus_fwrite", "binary",
             (n == sizeof BIN && rn == sizeof BIN && memcmp(rb, BIN, sizeof BIN) == 0)
                 ? "roundtrip" : "mismatch");
    }

    /* cplus_tmpfile */
    {
        FILE *t = cplus_tmpfile();
        emit("cplus_tmpfile", "valid", t ? "ok" : "null");
        if (t) cplus_fclose(t);
    }
    {
        FILE *t = cplus_tmpfile();
        size_t w = t ? cplus_fwrite(BIN, 1, sizeof BIN, t) : 0;
        if (t) { fflush(t); rewind(t); }
        unsigned char rb[8] = {0};
        size_t r = t ? cplus_fread(rb, 1, sizeof BIN, t) : 0;
        emit("cplus_tmpfile", "write-read",
             (w == sizeof BIN && r == sizeof BIN && memcmp(rb, BIN, sizeof BIN) == 0)
                 ? "roundtrip" : "mismatch");
        if (t) cplus_fclose(t);
    }
    {
        FILE *t = cplus_tmpfile();
        int rc = cplus_fclose(t);
        emit("cplus_tmpfile", "close", rc == 0 ? "closed" : "mismatch");
    }

    return 0;
}
