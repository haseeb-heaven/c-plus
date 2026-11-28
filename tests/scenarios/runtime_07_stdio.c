/* runtime_07_stdio.c -- 29 distinct runtime scenarios for ordinary stdio APIs.
 *
 * Built by tests/scenarios/run_scenarios.sh in two modes (plain and UBSan),
 * linked against libcplus_safecrt.a + libcplus_hardened.a.
 *
 * safe_c.h maps fread/fwrite/printf/fprintf to their checked cplus_* wrappers;
 * the remaining stdio calls use the explicit checked cplus_* API where one
 * exists, and plain ISO C otherwise.  Each scenario is self-contained and
 * removes every named temporary file it creates.  No scenario relies on
 * undefined behaviour: the deliberate "double close" case passes a stale
 * pointer only to cplus_fclose(), which refuses it before any real fclose().
 */
#define _POSIX_C_SOURCE 200809L

#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_runtime.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SC07_COUNT 29

/* PID-tagged names keep parallel scenario agents from colliding. */
static const char *sc07_tmpdir(void) {
    const char *d = getenv("TMPDIR");
    return (d != NULL && d[0] != '\0') ? d : "/tmp";
}

static void sc07_path(char *dst, size_t cap, const char *tag) {
    int n = snprintf(dst, cap, "%s/sc07_%ld_%s.tmp",
                     sc07_tmpdir(), (long)getpid(), tag);
    assert(n > 0 && (size_t)n < cap);
}

/* ---- fread ------------------------------------------------------------- */

/* 1. Exact-item read: the wrapper must return precisely the requested count. */
static void sc_07_fread_exact_items(void) {
    char path[160];
    sc07_path(path, sizeof path, "read_exact");
    FILE *f = cplus_fopen(path, "wb+");
    assert(f != NULL);
    assert(cplus_fwrite("0123456789", 1, 10, f) == 10);
    assert(cplus_fseek(f, 0, SEEK_SET) == 0);
    char buf[12] = {0};
    assert(fread(buf, 1, 10, f) == 10);
    assert(strcmp(buf, "0123456789") == 0);
    assert(cplus_fclose(f) == 0);
    assert(cplus_remove(path) == 0);
}

/* 2. Short read: fewer items than requested, feof set, ferror clear. */
static void sc_07_fread_short_read_feof(void) {
    FILE *f = cplus_tmpfile();
    assert(f != NULL);
    assert(cplus_fwrite("abc", 1, 3, f) == 3);
    assert(cplus_fseek(f, 0, SEEK_SET) == 0);
    char buf[8] = {0};
    errno = 0;
    assert(fread(buf, 1, 6, f) == 3);
    assert(feof(f) != 0);
    assert(ferror(f) == 0);
    assert(strcmp(buf, "abc") == 0);
    assert(cplus_fclose(f) == 0);
}

/* 3. size*count overflow is refused before touching the buffer (EOVERFLOW). */
static void sc_07_fread_overflow(void) {
    FILE *f = cplus_tmpfile();
    assert(f != NULL);
    char buf[4] = {0};
    errno = 0;
    assert(fread(buf, SIZE_MAX, 2, f) == 0);
    assert(errno == EOVERFLOW || errno == ERANGE);
    assert(buf[0] == '\0');
    assert(cplus_fclose(f) == 0);
}

/* 4. NULL data buffer with nonzero size/count -> EINVAL, zero items. */
static void sc_07_fread_null_buffer(void) {
    FILE *f = cplus_tmpfile();
    assert(f != NULL);
    errno = 0;
    assert(fread(NULL, 1, 1, f) == 0);
    assert(errno == EINVAL);
    assert(cplus_fclose(f) == 0);
}

/* 5. NULL stream -> EINVAL, zero items (no stream to close). */
static void sc_07_fread_null_stream(void) {
    char buf[4] = {0};
    errno = 0;
    assert(fread(buf, 1, 1, NULL) == 0);
    assert(errno == EINVAL);
}

/* ---- fwrite ------------------------------------------------------------ */

/* 6. Exact-item write followed by a read-back of the same items. */
static void sc_07_fwrite_exact_items(void) {
    FILE *f = cplus_tmpfile();
    assert(f != NULL);
    assert(fwrite("abcdef", 1, 6, f) == 6);
    assert(cplus_fflush(f) == 0);
    assert(cplus_fseek(f, 0, SEEK_SET) == 0);
    char buf[8] = {0};
    assert(fread(buf, 1, 6, f) == 6);
    assert(strcmp(buf, "abcdef") == 0);
    assert(cplus_fclose(f) == 0);
}

/* 7. Short write: a read-only stream accepts 0 of 3 bytes and sets ferror. */
static void sc_07_fwrite_short_write(void) {
    char path[160];
    sc07_path(path, sizeof path, "short_write");
    FILE *w = cplus_fopen(path, "wb");
    assert(w != NULL);
    assert(fwrite("seed", 1, 4, w) == 4);
    assert(cplus_fclose(w) == 0);
    FILE *r = cplus_fopen(path, "rb");
    assert(r != NULL);
    errno = 0;
    assert(fwrite("XYZ", 1, 3, r) == 0); /* 0 of 3 items written */
    assert(ferror(r) != 0);
    assert(cplus_fclose(r) == 0);
    assert(cplus_remove(path) == 0);
}

/* 8. fwrite multiplication overflow refused with EOVERFLOW/ERANGE. */
static void sc_07_fwrite_overflow(void) {
    FILE *f = cplus_tmpfile();
    assert(f != NULL);
    errno = 0;
    assert(fwrite("x", SIZE_MAX, 2, f) == 0);
    assert(errno == EOVERFLOW || errno == ERANGE);
    assert(cplus_fclose(f) == 0);
}

/* 9. NULL data buffer and NULL stream are both refused with EINVAL. */
static void sc_07_fwrite_null_args(void) {
    FILE *f = cplus_tmpfile();
    assert(f != NULL);
    char buf[2] = {0};
    errno = 0;
    assert(fwrite(NULL, 1, 1, f) == 0);
    assert(errno == EINVAL);
    errno = 0;
    assert(fwrite(buf, 1, 1, NULL) == 0);
    assert(errno == EINVAL);
    assert(cplus_fclose(f) == 0);
}

/* ---- fopen / fclose ---------------------------------------------------- */

/* 10. fopen success: open, write, reopen read-only and verify the payload. */
static void sc_07_fopen_success(void) {
    char path[160];
    sc07_path(path, sizeof path, "fopen_ok");
    FILE *w = cplus_fopen(path, "wb");
    assert(w != NULL);
    assert(fwrite("payload", 1, 7, w) == 7);
    assert(cplus_fclose(w) == 0);
    FILE *r = cplus_fopen(path, "rb");
    assert(r != NULL);
    char buf[10] = {0};
    assert(fread(buf, 1, 7, r) == 7);
    assert(strcmp(buf, "payload") == 0);
    assert(cplus_fclose(r) == 0);
    assert(cplus_remove(path) == 0);
}

/* 11. fopen failure on a guaranteed-missing directory path. */
static void sc_07_fopen_failure(void) {
    char bad[192];
    int n = snprintf(bad, sizeof bad, "%s/sc07_missing_dir_%ld/x.tmp",
                     sc07_tmpdir(), (long)getpid());
    assert(n > 0 && (size_t)n < sizeof bad);
    errno = 0;
    assert(cplus_fopen(bad, "wb") == NULL);
    assert(errno != 0);
}

/* 12. fopen with NULL path or NULL mode is refused with EINVAL. */
static void sc_07_fopen_null_args(void) {
    errno = 0;
    assert(cplus_fopen(NULL, "r") == NULL);
    assert(errno == EINVAL);
    errno = 0;
    assert(cplus_fopen("ignored.tmp", NULL) == NULL);
    assert(errno == EINVAL);
}

/* 13. fclose once on a live checked stream returns 0. */
static void sc_07_fclose_once(void) {
    FILE *f = cplus_tmpfile();
    assert(f != NULL);
    assert(cplus_fclose(f) == 0);
}

/* 14. Second fclose of the same handle is refused by the checked API (EBADF).
 *     The stale pointer is never passed to the real fclose(). */
static void sc_07_fclose_double_refused(void) {
    char path[160];
    sc07_path(path, sizeof path, "double_close");
    FILE *f = cplus_fopen(path, "wb");
    assert(f != NULL);
    assert(cplus_fclose(f) == 0);
    errno = 0;
    assert(cplus_fclose(f) == EOF);
    assert(errno == EBADF);
    assert(cplus_remove(path) == 0);
}

/* ---- positioning ------------------------------------------------------- */

/* 15. fseek/ftell round trip: seek to a known offset and read from it. */
static void sc_07_fseek_ftell_roundtrip(void) {
    FILE *f = cplus_tmpfile();
    assert(f != NULL);
    assert(fwrite("0123456789", 1, 10, f) == 10);
    assert(cplus_fseek(f, 4, SEEK_SET) == 0);
    assert(cplus_ftell(f) == 4L);
    char buf[4] = {0};
    assert(fread(buf, 1, 2, f) == 2);
    assert(strcmp(buf, "45") == 0);
    assert(cplus_ftell(f) == 6L);
    assert(cplus_fclose(f) == 0);
}

/* 16. rewind resets the file position and clears the EOF indicator. */
static void sc_07_rewind_resets_position(void) {
    FILE *f = cplus_tmpfile();
    assert(f != NULL);
    assert(fwrite("abcdef", 1, 6, f) == 6);
    assert(cplus_fseek(f, 0, SEEK_END) == 0);
    char buf[2] = {0};
    assert(fread(buf, 1, 1, f) == 0);
    assert(feof(f) != 0);
    cplus_rewind(f);
    assert(cplus_ftell(f) == 0L);
    assert(feof(f) == 0);
    assert(cplus_getc(f) == 'a');
    assert(cplus_fclose(f) == 0);
}

/* 17. fgetpos/fsetpos save and restore a mid-file position exactly. */
static void sc_07_fgetpos_fsetpos(void) {
    FILE *f = cplus_tmpfile();
    assert(f != NULL);
    assert(fwrite("ABCDEFGH", 1, 8, f) == 8);
    assert(cplus_fseek(f, 3, SEEK_SET) == 0);
    fpos_t pos;
    assert(cplus_fgetpos(f, &pos) == 0);
    char buf[4] = {0};
    assert(fread(buf, 1, 2, f) == 2);
    assert(strcmp(buf, "DE") == 0);
    assert(cplus_fsetpos(f, &pos) == 0);
    buf[0] = buf[1] = '\0';
    assert(fread(buf, 1, 2, f) == 2);
    assert(strcmp(buf, "DE") == 0);
    assert(cplus_fclose(f) == 0);
}

/* 18. ungetc pushes a character back so the next read returns it again. */
static void sc_07_ungetc_then_read(void) {
    FILE *f = cplus_tmpfile();
    assert(f != NULL);
    assert(fwrite("XY", 1, 2, f) == 2);
    cplus_rewind(f);
    int c = cplus_getc(f);
    assert(c == 'X');
    assert(cplus_ungetc(c, f) == 'X');
    assert(cplus_getc(f) == 'X');
    assert(cplus_getc(f) == 'Y');
    assert(cplus_fclose(f) == 0);
}

/* 19. setvbuf accepts a valid mode before I/O; an invalid mode is rejected.
 *     Each mode is tried on its own fresh stream, so no late-call UB. */
static void sc_07_setvbuf_valid_invalid_mode(void) {
    FILE *f = cplus_tmpfile();
    assert(f != NULL);
    char buf[64];
    assert(cplus_setvbuf(f, buf, _IOFBF, sizeof buf) == 0);
    assert(fwrite("hi", 1, 2, f) == 2);
    assert(cplus_fclose(f) == 0);

    FILE *g = cplus_tmpfile();
    assert(g != NULL);
    char badbuf[64];
    /* POSIX: setvbuf returns nonzero when the mode value is invalid.
     * errno is not required to be set by all implementations. */
    assert(cplus_setvbuf(g, badbuf, 12345, sizeof badbuf) != 0);
    assert(cplus_fclose(g) == 0);
}

/* ---- character and line I/O -------------------------------------------- */

/* 20. fgets honours its n bound and always NUL-terminates. */
static void sc_07_fgets_bounded(void) {
    FILE *f = cplus_tmpfile();
    assert(f != NULL);
    assert(fwrite("abcdefghij\n", 1, 11, f) == 11);
    cplus_rewind(f);
    char buf[6] = {0};
    assert(cplus_fgets(buf, 6, f) == buf);
    assert(strlen(buf) == 5);
    assert(strcmp(buf, "abcde") == 0);
    assert(buf[5] == '\0');
    assert(cplus_fclose(f) == 0);
}

/* 21. fputs writes a whole string without its terminator. */
static void sc_07_fputs_roundtrip(void) {
    FILE *f = cplus_tmpfile();
    assert(f != NULL);
    assert(cplus_fputs("hello\n", f) >= 0);
    cplus_rewind(f);
    char buf[16] = {0};
    assert(cplus_fgets(buf, sizeof buf, f) != NULL);
    assert(strcmp(buf, "hello\n") == 0);
    assert(cplus_fclose(f) == 0);
}

/* 22. putc/getc EOF handling: putc(EOF) writes 0xFF; getc at end is EOF. */
static void sc_07_getc_putc_eof(void) {
    FILE *f = cplus_tmpfile();
    assert(f != NULL);
    assert(cplus_putc('Q', f) != EOF);
    assert(cplus_putc(EOF, f) == 0xFF); /* converted to unsigned char */
    cplus_rewind(f);
    assert(cplus_getc(f) == 'Q');
    assert(cplus_getc(f) == 0xFF);
    assert(cplus_getc(f) == EOF);
    assert(feof(f) != 0);
    assert(ferror(f) == 0);
    assert(cplus_fclose(f) == 0);
}

/* 23. clearerr clears the EOF indicator after a short read hits end. */
static void sc_07_clearerr_after_eof(void) {
    FILE *f = cplus_tmpfile();
    assert(f != NULL);
    assert(fwrite("A", 1, 1, f) == 1);
    cplus_rewind(f);
    assert(cplus_getc(f) == 'A');
    assert(cplus_getc(f) == EOF);
    assert(feof(f) != 0);
    cplus_clearerr(f);
    assert(feof(f) == 0);
    assert(ferror(f) == 0);
    assert(cplus_fclose(f) == 0);
}

/* ---- stream lifecycle -------------------------------------------------- */

/* 24. tmpfile lifecycle: write, rewind, read back, close. */
static void sc_07_tmpfile_lifecycle(void) {
    FILE *f = cplus_tmpfile();
    assert(f != NULL);
    assert(fwrite("tmp", 1, 3, f) == 3);
    cplus_rewind(f);
    char buf[6] = {0};
    assert(fread(buf, 1, 3, f) == 3);
    assert(strcmp(buf, "tmp") == 0);
    assert(cplus_fclose(f) == 0); /* tmpfile unlinks itself on close */
}

/* 25. freopen redirects an existing stream to a second named file. */
static void sc_07_freopen_stream(void) {
    char a[160], b[160];
    sc07_path(a, sizeof a, "freopen_a");
    sc07_path(b, sizeof b, "freopen_b");
    FILE *f = cplus_fopen(a, "wb");
    assert(f != NULL);
    assert(fwrite("old", 1, 3, f) == 3);
    FILE *g = cplus_freopen(b, "wb", f);
    assert(g != NULL);
    assert(cplus_fputs("new-data", g) >= 0);
    assert(cplus_fclose(g) == 0);
    FILE *chk = cplus_fopen(b, "rb");
    assert(chk != NULL);
    char buf[12] = {0};
    assert(fread(buf, 1, 8, chk) == 8);
    assert(strcmp(buf, "new-data") == 0);
    assert(cplus_fclose(chk) == 0);
    assert(cplus_remove(a) == 0);
    assert(cplus_remove(b) == 0);
}

/* 26. rename then remove on named temporary files. */
static void sc_07_remove_rename_temp(void) {
    char a[160], b[160];
    sc07_path(a, sizeof a, "rename_a");
    sc07_path(b, sizeof b, "rename_b");
    FILE *f = cplus_fopen(a, "wb");
    assert(f != NULL);
    assert(fwrite("payload", 1, 7, f) == 7);
    assert(cplus_fclose(f) == 0);
    assert(cplus_rename(a, b) == 0);
    FILE *g = cplus_fopen(b, "rb");
    assert(g != NULL);
    char buf[10] = {0};
    assert(fread(buf, 1, 7, g) == 7);
    assert(strcmp(buf, "payload") == 0);
    assert(cplus_fclose(g) == 0);
    assert(cplus_remove(b) == 0);
    errno = 0;
    assert(cplus_remove(b) != 0); /* already gone */
}

/* ---- formatted output / logging --------------------------------------- */

/* 27. printf/fprintf with valid literal formats (clang checks them at compile
 *     time through the attributed cplus_* wrappers). */
static void sc_07_printf_fprintf_valid_formats(void) {
    int r = printf("sc07: printf %d %s\n", 7, "ok");
    assert(r > 0);
    FILE *f = cplus_tmpfile();
    assert(f != NULL);
    assert(cplus_fprintf(f, "%d/%d %s %.2f\n", 1, 2, "x", 3.5) > 0);
    cplus_rewind(f);
    char buf[64] = {0};
    assert(cplus_fgets(buf, sizeof buf, f) != NULL);
    assert(strstr(buf, "1/2 x 3.50") != NULL);
    assert(cplus_fclose(f) == 0);
}

/* 28. snprintf truncation returns the would-be length and NUL-terminates. */
static void sc_07_snprintf_truncation(void) {
    char small[8];
    int r = cplus_snprintf(small, sizeof small, "abcdefghij");
    assert(r == 10);
    assert(strlen(small) == 7);
    assert(strcmp(small, "abcdefg") == 0);
    char big[16];
    r = cplus_snprintf(big, sizeof big, "%d-%s", 12, "xy");
    assert(r == 5);
    assert(strcmp(big, "12-xy") == 0);
}

/* Identical deterministic operation sequence, used by the logging-parity case. */
static void sc07_parity_seq(int out[8], const char *tag) {
    char path[160];
    sc07_path(path, sizeof path, tag);
    FILE *f = cplus_fopen(path, "wb+");
    out[0] = (f != NULL);
    out[1] = (int)cplus_fwrite("HELLO", 1, 5, f);
    out[2] = (cplus_fseek(f, 0, SEEK_SET) == 0);
    char b[8] = {0};
    out[3] = (int)fread(b, 1, 5, f);
    out[4] = (strcmp(b, "HELLO") == 0);
    errno = 0;
    out[5] = (int)fread(NULL, 1, 1, f); /* refused, sets EINVAL */
    out[6] = errno;
    out[7] = (cplus_fclose(f) == 0) && (cplus_remove(path) == 0);
}

/* 29. Enabling opt-in libc logging must not change any API result or errno. */
static void sc_07_logging_parity(void) {
    int off[8], on[8];
    cplus_libc_log_set_enabled(0);
    sc07_parity_seq(off, "log_off");
    cplus_libc_log_set_enabled(1);
    sc07_parity_seq(on, "log_on");
    cplus_libc_log_set_enabled(0);
    for (int i = 0; i < 8; ++i) {
        assert(off[i] == on[i]);
    }
    assert(off[0] == 1 && off[1] == 5 && off[2] == 1);
    assert(off[3] == 5 && off[4] == 1 && off[5] == 0);
    assert(off[6] == EINVAL && off[7] == 1);
}

int main(void) {
    /* Tests inspect NULL/ERANGE/EFAULT-style return values rather than traps. */
    cplus_runtime_set_violation_trap(0);

    sc_07_fread_exact_items();
    sc_07_fread_short_read_feof();
    sc_07_fread_overflow();
    sc_07_fread_null_buffer();
    sc_07_fread_null_stream();
    sc_07_fwrite_exact_items();
    sc_07_fwrite_short_write();
    sc_07_fwrite_overflow();
    sc_07_fwrite_null_args();
    sc_07_fopen_success();
    sc_07_fopen_failure();
    sc_07_fopen_null_args();
    sc_07_fclose_once();
    sc_07_fclose_double_refused();
    sc_07_fseek_ftell_roundtrip();
    sc_07_rewind_resets_position();
    sc_07_fgetpos_fsetpos();
    sc_07_ungetc_then_read();
    sc_07_setvbuf_valid_invalid_mode();
    sc_07_fgets_bounded();
    sc_07_fputs_roundtrip();
    sc_07_getc_putc_eof();
    sc_07_clearerr_after_eof();
    sc_07_tmpfile_lifecycle();
    sc_07_freopen_stream();
    sc_07_remove_rename_temp();
    sc_07_printf_fprintf_valid_formats();
    sc_07_snprintf_truncation();
    sc_07_logging_parity();

    printf("runtime_stdio: %d scenarios ok\n", SC07_COUNT);
    return 0;
}
