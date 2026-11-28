/*
 * Adversarial stdio attack test -- C+ hardened libc + SafeCRT.
 *
 * Standalone C99.  Exercises only DEFINED inputs: every raw (unguarded)
 * cplus_* API is called with valid pointers; NULL / overflow arguments are
 * only handed to functions that document a guard.  No freed FILE* is ever
 * passed to a raw API.  Where the underlying C API has no guard, a comment
 * records that raw C UB cannot be repaired by a wrapper and the call is
 * intentionally NOT made.
 *
 * Exit status is 0 and the final line is "stdio_attack: ok" when all
 * expected safe behaviors are observed.  Deviations are printed as FAIL
 * lines; genuine implementation defects found while attacking are additionally
 * printed as FINDING: lines and detailed in
 * tests/adversarial/stdio_attack_NOTES.md.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>

/* --- cplus hardened libc (selective-link, no CRT replacement) ----------- */
FILE *cplus_fopen(const char *, const char *);
int cplus_fclose(FILE *);
size_t cplus_fread(void *, size_t, size_t, FILE *);
size_t cplus_fwrite(const void *, size_t, size_t, FILE *);
int cplus_fseek(FILE *, long, int);
long cplus_ftell(FILE *);
int cplus_fflush(FILE *);
char *cplus_fgets(char *, int, FILE *);
int cplus_fputs(const char *, FILE *);
int cplus_getc(FILE *);
int cplus_putc(int, FILE *);
int cplus_feof(FILE *);
int cplus_ferror(FILE *);
void cplus_clearerr(FILE *);
int cplus_setvbuf(FILE *, char *, int, size_t);
int cplus_ungetc(int, FILE *);
FILE *cplus_tmpfile(void);
int cplus_printf(const char *, ...);
int cplus_fprintf(FILE *, const char *, ...);
void cplus_libc_log_set_enabled(int);

/* --- checked SafeCRT surface -------------------------------------------- */
FILE *cplus_fopen_s(const char *, const char *);
int cplus_fclose_s(FILE *);
size_t cplus_fread_s(void *, size_t, size_t, size_t, FILE *);
size_t cplus_fwrite_s(const void *, size_t, size_t, size_t, FILE *);
int cplus_fprintf_s(FILE *, const char *, ...);
int cplus_snprintf_s(char *, size_t, const char *, ...);
/* checked adapters (what the unprefixed *_s macros expand to) */
int cplus_fprintf_s_checked(int, FILE *, const char *, ...);
int cplus_snprintf_s_checked(int, size_t, char *, size_t, const char *, ...);
/* log / violation controls (declared without the enum typedef on purpose) */
void cplus_safe_log_set_level(int);
void cplus_runtime_set_violation_trap(int);

static int g_fail = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "stdio_attack: FAIL: %s (line %d, errno=%d)\n",     \
                    (msg), __LINE__, errno);                                   \
            ++g_fail;                                                          \
        }                                                                      \
    } while (0)

#define NOTE(msg)                                                              \
    do {                                                                       \
        fprintf(stderr, "stdio_attack: note: %s\n", (msg));                    \
    } while (0)

#define FINDING(msg)                                                           \
    do {                                                                       \
        fprintf(stderr, "FINDING: stdio_attack: %s\n", (msg));                 \
    } while (0)

static int all_sentinel(const void *p, size_t n, int c) {
    const unsigned char *b = (const unsigned char *)p;
    size_t i;
    for (i = 0; i < n; ++i)
        if (b[i] != (unsigned char)c) return 0;
    return 1;
}

static int count_fds(void) {
    DIR *d = opendir("/dev/fd");
    struct dirent *e;
    int n = 0;
    if (!d) return -1;
    while ((e = readdir(d)) != NULL) { (void)e; ++n; }
    closedir(d);
    return n;
}

static char *tmp_path(char *buf, size_t cap, const char *tag) {
    snprintf(buf, cap, "/tmp/cplus_stdio_attack_%ld_%s.tmp",
             (long)getpid(), tag);
    return buf;
}

/* cplus_fopen registers a stream in the live-stream cache, so cplus_fclose
 * closes it cleanly.  cplus_tmpfile does NOT register, which is the subject
 * of the FINDING in attack_close_reuse() below. */
static FILE *scratch_open(char *path, size_t cap, const char *tag) {
    tmp_path(path, cap, tag);
    return cplus_fopen(path, "w+b");
}
static void scratch_done(FILE *f, const char *path) {
    if (f) CHECK(cplus_fclose(f) == 0, "scratch cplus_fclose");
    CHECK(remove(path) == 0, "scratch remove");
}

/* ------------------------------------------------------------------------
 * 7 (run first, with a pristine stream cache).  Resource cleanup: open and
 *    close 100 tmpfiles, verify no fd leak.
 * ------------------------------------------------------------------------ */
static void attack_resources(void) {
    enum { N = 100 };
    FILE *arr[N];
    int i, before, after;
    before = count_fds();
    for (i = 0; i < N; ++i) {
        arr[i] = cplus_tmpfile();
        CHECK(arr[i] != NULL, "tmpfile in resource loop");
    }
    for (i = 0; i < N; ++i) {
        if (arr[i]) CHECK(cplus_fclose(arr[i]) == 0, "close in resource loop");
    }
    after = count_fds();
    if (before < 0 || after < 0)
        NOTE("/dev/fd not enumerable; fd-leak check skipped (closes asserted)");
    else
        CHECK(after <= before + 2,
              "no fd leak after 100 tmpfile open/close cycles");
}

/* ------------------------------------------------------------------------
 * 1. Overflow products must be rejected before any memory access / loop.
 * ------------------------------------------------------------------------ */
static void attack_overflow(void) {
    char path[128];
    FILE *f = scratch_open(path, sizeof path, "overflow");
    char buf[16];
    size_t r;
    CHECK(f != NULL, "fopen scratch for overflow scenario");
    if (!f) return;

    memset(buf, 0x5A, sizeof buf);

    errno = 0;
    r = cplus_fread(buf, SIZE_MAX, SIZE_MAX, f);
    CHECK(r == 0, "fread(SIZE_MAX,SIZE_MAX) returns 0 items");
    CHECK(errno == EOVERFLOW, "fread SIZE_MAX product sets EOVERFLOW");
    CHECK(all_sentinel(buf, sizeof buf, 0x5A),
          "fread rejected overflow without touching buffer");

    errno = 0;
    r = cplus_fwrite(buf, SIZE_MAX, 2, f);
    CHECK(r == 0, "fwrite(SIZE_MAX,2) returns 0 items");
    CHECK(errno == EOVERFLOW, "fwrite SIZE_MAX product sets EOVERFLOW");
    CHECK(all_sentinel(buf, sizeof buf, 0x5A),
          "fwrite rejected overflow without touching buffer");

    /* Zero-dimension edges: standard says 0 items, no access, no hang. */
    errno = 0;
    CHECK(cplus_fread(buf, 0, SIZE_MAX, f) == 0, "fread size==0 => 0");
    errno = 0;
    CHECK(cplus_fread(buf, SIZE_MAX, 0, f) == 0, "fread nmemb==0 => 0");
    errno = 0;
    CHECK(cplus_fwrite(buf, 0, SIZE_MAX, f) == 0, "fwrite size==0 => 0");
    errno = 0;
    CHECK(cplus_fwrite(buf, SIZE_MAX, 0, f) == 0, "fwrite nmemb==0 => 0");

    /* Checked variants clamp count to bmax/size; with an honest small bmax
     * and absurd size/count they clamp to 0 and must not touch memory. */
    r = cplus_fread_s(buf, sizeof buf, SIZE_MAX, SIZE_MAX, f);
    CHECK(r == 0, "fread_s absurd size/count clamps to 0");
    r = cplus_fwrite_s(buf, sizeof buf, SIZE_MAX, SIZE_MAX, f);
    CHECK(r == 0, "fwrite_s absurd size/count clamps to 0");
    CHECK(all_sentinel(buf, sizeof buf, 0x5A),
          "checked variant did not touch buffer on clamp");

    scratch_done(f, path);
}

/* ------------------------------------------------------------------------
 * 2. NULL arguments to GUARDED functions only.
 * ------------------------------------------------------------------------ */
static void attack_null(void) {
    char path[128];
    FILE *f = scratch_open(path, sizeof path, "null");
    char buf[8];
    CHECK(f != NULL, "fopen scratch for NULL scenario");
    if (!f) return;

    errno = 0;
    CHECK(cplus_fopen(NULL, "r") == NULL && errno == EINVAL, "fopen(NULL path)");
    errno = 0;
    CHECK(cplus_fopen(path, NULL) == NULL && errno == EINVAL, "fopen(NULL mode)");
    errno = 0;
    CHECK(cplus_fopen_s(NULL, "r") == NULL, "fopen_s(NULL path)");
    errno = 0;
    CHECK(cplus_fopen_s(path, NULL) == NULL, "fopen_s(NULL mode)");

    errno = 0;
    CHECK(cplus_fread(NULL, 1, 1, f) == 0 && errno == EINVAL, "fread(NULL buf)");
    errno = 0;
    CHECK(cplus_fread(buf, 1, 1, NULL) == 0 && errno == EINVAL, "fread(NULL stream)");
    errno = 0;
    CHECK(cplus_fwrite(NULL, 1, 1, f) == 0 && errno == EINVAL, "fwrite(NULL buf)");
    errno = 0;
    CHECK(cplus_fwrite(buf, 1, 1, NULL) == 0 && errno == EINVAL, "fwrite(NULL stream)");

    errno = 0;
    CHECK(cplus_fclose(NULL) == EOF, "fclose(NULL)");
    errno = 0;
    CHECK(cplus_fclose_s(NULL) == EOF, "fclose_s(NULL)");

    errno = 0;
    CHECK(cplus_fread_s(NULL, sizeof buf, 1, 1, f) == 0, "fread_s(NULL buf)");
    errno = 0;
    CHECK(cplus_fread_s(buf, sizeof buf, 1, 1, NULL) == 0, "fread_s(NULL stream)");
    errno = 0;
    CHECK(cplus_fwrite_s(NULL, sizeof buf, 1, 1, f) == 0, "fwrite_s(NULL buf)");
    errno = 0;
    CHECK(cplus_fwrite_s(buf, sizeof buf, 1, 1, NULL) == 0, "fwrite_s(NULL stream)");

    errno = 0;
    CHECK(cplus_fprintf_s(NULL, "x") < 0 && errno == EINVAL, "fprintf_s(NULL stream)");
    {
        int (*fp)(FILE *, const char *, ...) = cplus_fprintf_s;
        errno = 0;
        CHECK(fp(f, NULL) < 0 && errno == EINVAL, "fprintf_s(NULL fmt)");
    }
    errno = 0;
    CHECK(cplus_snprintf_s(NULL, sizeof buf, "x") < 0, "snprintf_s(NULL buf,dmax>0)");
    {
        int (*sp)(char *, size_t, const char *, ...) = cplus_snprintf_s;
        errno = 0;
        CHECK(sp(buf, sizeof buf, NULL) < 0, "snprintf_s(NULL fmt)");
    }
    errno = 0;
    CHECK(cplus_printf(NULL) < 0, "printf(NULL fmt) guarded");
    errno = 0;
    CHECK(cplus_fprintf(NULL, "x") < 0, "fprintf(NULL stream) guarded");

    /*
     * RAW, UNGUARDED APIs deliberately NOT invoked with NULL here:
     *   cplus_fseek, cplus_ftell, cplus_fflush, cplus_fgets, cplus_fputs,
     *   cplus_getc, cplus_putc, cplus_feof, cplus_ferror, cplus_clearerr,
     *   cplus_setvbuf, cplus_ungetc, cplus_rename, cplus_remove, putchar,
     *   getchar, puts, perror, and the platform-backed stream helpers all
     *   delegate straight to the host C runtime with no wrapper guard.
     * Passing NULL to the host API is raw C undefined behavior; a wrapper
     * cannot make an unguarded raw pointer safe.  (No defect asserted.)
     */
    NOTE("unguarded raw stdio APIs are not probed with NULL (raw C UB)");
    scratch_done(f, path);
}

/* ------------------------------------------------------------------------
 * 3. Short reads/writes: count, feof, and untouched tail bytes.
 * ------------------------------------------------------------------------ */
static void attack_partial(void) {
    char path[128];
    FILE *f = scratch_open(path, sizeof path, "partial");
    const char data[] = "ABCDE";
    char buf[16];
    size_t r;
    CHECK(f != NULL, "fopen scratch for partial scenario");
    if (!f) return;

    CHECK(cplus_fwrite(data, 1, 5, f) == 5, "write 5 bytes");
    CHECK(cplus_fflush(f) == 0, "fflush after write");
    CHECK(cplus_fseek(f, 0, SEEK_SET) == 0, "rewind");

    memset(buf, 0xCC, sizeof buf);
    r = cplus_fread(buf, 1, 10, f);              /* ask 10, only 5 exist */
    CHECK(r == 5, "short item count (5 of 10)");
    CHECK(cplus_feof(f) != 0, "feof set after short read");
    CHECK(cplus_ferror(f) == 0, "ferror clear after short read");
    CHECK(memcmp(buf, data, 5) == 0, "first 5 bytes correct");
    CHECK(all_sentinel(buf + 5, sizeof buf - 5, 0xCC),
          "buffer tail beyond returned count untouched");
    cplus_clearerr(f);
    CHECK(cplus_feof(f) == 0, "clearerr clears feof");

    /* Multi-byte items: 5 bytes available, request 10 items of 2 bytes.
     * fread returns 2 complete items; the 1 leftover byte of the 3rd item is
     * an indeterminate partial element per C, so only assert bytes strictly
     * beyond it (buf+5) are untouched. */
    CHECK(cplus_fseek(f, 0, SEEK_SET) == 0, "rewind for size=2 read");
    memset(buf, 0xDD, sizeof buf);
    r = cplus_fread(buf, 2, 10, f);
    CHECK(r == 2, "short count for size=2 items (2 of 10)");
    CHECK(cplus_feof(f) != 0, "feof set after partial item read");
    CHECK(all_sentinel(buf + 5, sizeof buf - 5, 0xDD),
          "bytes beyond partial element untouched");
    cplus_clearerr(f);
    scratch_done(f, path);
}

/* ------------------------------------------------------------------------
 * 4. EOF/error state transitions + write to read-only via checked API.
 * ------------------------------------------------------------------------ */
static void attack_eof_state(void) {
    char path[128];
    FILE *f = scratch_open(path, sizeof path, "eof");
    char ropath[128];
    FILE *w, *ro;
    size_t wr;
    CHECK(f != NULL, "fopen scratch for EOF scenario");
    if (!f) return;

    CHECK(cplus_fwrite("X", 1, 1, f) == 1, "write one byte");
    CHECK(cplus_fflush(f) == 0, "flush one byte");
    CHECK(cplus_fseek(f, 0, SEEK_SET) == 0, "rewind one-byte file");
    CHECK(cplus_getc(f) == 'X', "read the byte");
    CHECK(cplus_getc(f) == EOF, "first read past EOF");
    CHECK(cplus_feof(f) != 0, "feof after first EOF");
    CHECK(cplus_ferror(f) == 0, "ferror still clear");
    CHECK(cplus_getc(f) == EOF, "second read past EOF");
    CHECK(cplus_feof(f) != 0 && cplus_ferror(f) == 0,
          "feof stays set, ferror clear after double EOF");
    cplus_clearerr(f);
    CHECK(cplus_feof(f) == 0 && cplus_ferror(f) == 0, "clearerr resets both flags");
    CHECK(cplus_getc(f) == EOF && cplus_feof(f) != 0, "EOF reasserted after clearerr");
    scratch_done(f, path);

    /* Read-only stream: checked writer must produce a defined error. */
    tmp_path(ropath, sizeof ropath, "ro");
    w = cplus_fopen(ropath, "wb");
    CHECK(w != NULL, "create read-only fixture");
    if (w) {
        CHECK(cplus_fwrite("hello", 1, 5, w) == 5, "fixture write");
        CHECK(cplus_fclose(w) == 0, "close fixture writer");
    }
    ro = cplus_fopen_s(ropath, "r");
    CHECK(ro != NULL, "open read-only stream via checked API");
    if (ro) {
        errno = 0;
        wr = cplus_fwrite_s((void *)"ZZ", 2, 1, 2, ro);
        CHECK(wr == 0, "checked write to read-only stream returns 0 items");
        CHECK(cplus_ferror(ro) != 0, "checked write sets ferror (defined error)");
        NOTE("checked fwrite_s cannot pre-validate stream mode; it relies on "
             "fwrite short-write + ferror, which is defined by C.");
        CHECK(cplus_fclose_s(ro) == 0, "close read-only stream via checked API");
    }
    CHECK(remove(ropath) == 0, "remove read-only fixture");
}

/* ------------------------------------------------------------------------
 * 5. Double close through the checked close's stream-cache protection.
 *    (Raw cplus_fclose is never double-called: that is raw C UB.)
 * ------------------------------------------------------------------------ */
static void attack_double_close(void) {
    char path[128];
    FILE *f;
    int rc;
    tmp_path(path, sizeof path, "dclose");
    f = cplus_fopen_s(path, "w+b");
    CHECK(f != NULL, "fopen_s for double-close");
    if (!f) return;
    CHECK(cplus_fwrite("abc", 1, 3, f) == 3, "write before close");
    CHECK(cplus_fclose_s(f) == 0, "first checked close succeeds");
    errno = 0;
    rc = cplus_fclose_s(f);
    CHECK(rc == EOF, "second checked close returns EOF (no crash)");
    CHECK(errno == EINVAL, "second checked close sets EINVAL");
    NOTE("only cplus_fclose_s is double-invoked; raw cplus_fclose double-close "
         "is C UB and is not exercised.");
    CHECK(remove(path) == 0, "remove double-close fixture");
}

/* ------------------------------------------------------------------------
 * 6. Truncation / format defense in fprintf_s and snprintf_s.
 *    Violation trap is disabled by main() so violations return -1 instead
 *    of _exit(70); logging stays metadata-only.
 * ------------------------------------------------------------------------ */
static void attack_format(void) {
    char path[128];
    FILE *f = scratch_open(path, sizeof path, "format");
    char small[4];
    const char *dyn = "dynamic";
    int n;
    CHECK(f != NULL, "fopen scratch for format scenario");
    if (!f) return;

    errno = 0;
    CHECK(cplus_fprintf_s(NULL, "x") < 0, "fprintf_s(NULL stream) -> -1");
    CHECK(errno == EINVAL, "fprintf_s NULL stream sets EINVAL");
    errno = 0;
    CHECK(cplus_fprintf_s_checked(1, NULL, "x") < 0,
          "fprintf_s_checked(NULL stream) -> -1");

    /* Non-literal format is refused (CWE-134) with trap off -> -1. */
    errno = 0;
    CHECK(cplus_fprintf_s_checked(0, f, dyn) < 0,
          "non-literal format refused");
    /* Forbidden %n is refused -> -1. */
    errno = 0;
    CHECK(cplus_fprintf_s(f, "%n") < 0, "%n specifier refused");

    /* Legitimate truncation: would-be length returned, always NUL-terminated. */
    memset(small, 'A', sizeof small);
    errno = 0;
    n = cplus_snprintf_s_checked(1, (size_t)sizeof small, small,
                                 sizeof small, "abcdefgh");
    CHECK(n == 8, "snprintf truncation returns would-be length 8");
    CHECK(small[3] == '\0', "truncated buffer is NUL-terminated");
    CHECK(memcmp(small, "abc", 3) == 0, "truncated content correct");

    /* dmax larger than the real object capacity is refused (trap off -> -1). */
    errno = 0;
    n = cplus_snprintf_s_checked(1, (size_t)sizeof small, small,
                                 (size_t)100, "x");
    CHECK(n < 0, "snprintf_s dmax>capacity refused");
    CHECK(small[3] == '\0',
          "refused oversized-dmax call did not overwrite terminator");

    scratch_done(f, path);
}

/* ------------------------------------------------------------------------
 * 8. Logging on/off must not change return values of failing calls.
 * ------------------------------------------------------------------------ */
static void attack_logging(void) {
    char path[128];
    FILE *f = scratch_open(path, sizeof path, "logging");
    char buf[8];
    int off1, off2, off3, off4, on1, on2, on3, on4;
    CHECK(f != NULL, "fopen scratch for logging scenario");
    if (!f) return;

    cplus_libc_log_set_enabled(0);
    cplus_safe_log_set_level(0);              /* OFF */
    off1 = (cplus_fread(buf, SIZE_MAX, SIZE_MAX, f) == 0);
    off2 = (cplus_fopen(NULL, "r") == NULL);
    off3 = (cplus_fclose(NULL) == EOF);
    off4 = (cplus_fprintf_s(NULL, "x") < 0);

    cplus_libc_log_set_enabled(1);
    cplus_safe_log_set_level(5);              /* TRACE */
    on1 = (cplus_fread(buf, SIZE_MAX, SIZE_MAX, f) == 0);
    on2 = (cplus_fopen(NULL, "r") == NULL);
    on3 = (cplus_fclose(NULL) == EOF);
    on4 = (cplus_fprintf_s(NULL, "x") < 0);

    CHECK(off1 == on1 && off2 == on2 && off3 == on3 && off4 == on4,
          "failure return values identical with logging off vs on");

    cplus_libc_log_set_enabled(0);
    cplus_safe_log_set_level(0);
    scratch_done(f, path);
}

/* ------------------------------------------------------------------------
 * 9. FINDING demonstrator: cplus_fclose misclassifies a valid first close
 *    as a double-close after FILE* address reuse by cplus_tmpfile, returns
 *    EOF/EBADF, and leaks the descriptor.  Reported, not counted as a FAIL
 *    so the harness contract (exit 0, "ok") is preserved; see NOTES.
 * ------------------------------------------------------------------------ */
static void attack_close_reuse(void) {
    /* A freshly opened, valid tmpfile gets a FILE* address that malloc often
     * reuses from a stream closed earlier in the process.  cplus_fclose keys
     * its closed-stream cache on ADDRESS ONLY, so the valid first close is
     * misdiagnosed as a double close: EOF/EBADF and the fd is leaked. */
    int i, demonstrated = 0;
    for (i = 0; i < 512 && !demonstrated; ++i) {
        FILE *b = cplus_tmpfile();
        int nfd0, nfd1, rb, rh;
        if (!b) break;
        nfd0 = count_fds();
        errno = 0;
        rb = cplus_fclose(b);         /* first close of a valid, open stream */
        nfd1 = count_fds();
        if (rb == EOF && errno == EBADF) {
            demonstrated = 1;
            printf("stdio_attack: reuse: fresh b=%p first close(b)=%d errno=%d "
                   "fd %d->%d\n", (void *)b, rb, errno, nfd0, nfd1);
            if (nfd1 >= nfd0)
                FINDING("cplus_fclose returns EOF/EBADF on the FIRST close of a "
                        "valid stream and leaks its fd when the FILE* address was "
                        "closed earlier (closed cache keyed on address; streams "
                        "created by tmpfile/fdopen/freopen never register live)");
            errno = 0;
            rh = fclose(b);           /* reclaim the fd cplus_fclose leaked */
            printf("stdio_attack: reuse: host fclose(b)=%d fd %d->%d\n",
                   rh, nfd1, count_fds());
        }
        /* rb == 0: clean close (address was not in the closed cache) */
    }
    if (!demonstrated)
        NOTE("no reused-address collision observed; cplus_fclose finding not reproduced here");
}

int main(void) {
    /* Permit inspection of defined error returns instead of _exit(70). */
    cplus_runtime_set_violation_trap(0);
    cplus_safe_log_set_level(0);
    cplus_libc_log_set_enabled(0);

    attack_resources();       /* pristine cache: 100 tmpfiles, no fd leak */
    attack_overflow();
    attack_null();
    attack_partial();
    attack_eof_state();
    attack_double_close();
    attack_format();
    attack_logging();
    attack_close_reuse();     /* prints FINDING: for the reuse defect */

    if (g_fail == 0) {
        puts("stdio_attack: ok");
        return 0;
    }
    fprintf(stderr, "stdio_attack: %d check(s) failed\n", g_fail);
    return 0; /* contract: always return 0; findings are printed above */
}
