/* EDGE100 agent 3/10 -- exhaustive edge-case campaign for stdlib I/O APIs.
 *
 * Targets (EVERY public API): stdlib/io.hp, stdlib/file.hp, stdlib/fs.hp,
 * stdlib/json.hp.  Only tests/edge100/io/ is touched; no source/header edits.
 *
 * One forked child per (api, case) with a 4s alarm watchdog and 0xA5 canaries
 * around every caller buffer.  The parent classifies PASS / FAIL / CRASH.
 * Allocation failure is injected deterministically by macro-renaming the two
 * libc allocations made from header code (json_parse_file's realloc and
 * file_temp's tmpfile); yyjson-internal allocations are covered by the
 * pre-allocation length/depth budget guards.
 *
 * Build: sh tests/edge100/io/run.sh
 * Final line: EDGE100 io <apis> <cases> <passed> <failed>
 */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cplus/cplus.h"

/* ---- deterministic allocation-failure injection ------------------------ */
static int g_fail_realloc;
static int g_fail_tmpfile;

static void *edge100_realloc(void *p, size_t n) {
    if (g_fail_realloc != 0) return (void *)0;
    return realloc(p, n);
}

static FILE *edge100_tmpfile(void) {
    if (g_fail_tmpfile != 0) return (FILE *)0;
    return tmpfile();
}

#define realloc edge100_realloc
#define tmpfile edge100_tmpfile
#include "stdlib/error.hp"
#include "stdlib/io.hp"
#include "stdlib/file.hp"
#include "stdlib/fs.hp"
#include "stdlib/json.hp"
#undef tmpfile
#undef realloc

/* ---- globals: canaried work areas -------------------------------------- */
#define GUARD 32u
#define CAP 4096u
#define BIG 45000u
#define ALARM_SEC 4
#define TMPDIR "tests/edge100/io/out/tmp"

static char g_buf[GUARD + CAP + GUARD];
static char g_text[GUARD + BIG + GUARD];
static char g_out[GUARD + 512u + GUARD];
static char g_path[512];
static char g_path2[512];
static char g_line[256];
static char g_nguard1[64];
static char g_nguard2[64];
static char g_fguard1[64];
static char g_fguard2[64];
static int g_int_out;
static long g_long_out;
static float g_float_out;
static double g_double_out;

/* ---- API / case enumeration -------------------------------------------- */
enum {
    /* io.hp (7) */
    A_IO_PRINTLN, A_IO_TOKEN, A_IO_READ_LINE, A_IO_READ_INT, A_IO_READ_LONG,
    A_IO_READ_FLOAT, A_IO_READ_DOUBLE,
    /* file.hp (13) */
    A_FILE_CTOR, A_FILE_IS_OPEN, A_FILE_NATIVE, A_FILE_READ, A_FILE_READ_LINE,
    A_FILE_WRITE, A_FILE_WRITE_BYTES, A_FILE_SEEK, A_FILE_FLUSH, A_FILE_CLOSE,
    A_FILE_DROP, A_FILE_BORROW, A_FILE_TEMP,
    /* fs.hp (8) */
    A_FS_EXISTS, A_FS_IS_FILE, A_FS_IS_DIR, A_FS_SIZE, A_FS_REMOVE, A_FS_RENAME,
    A_FS_MKDIR, A_FS_MKDTEMP,
    /* json.hp (34) */
    A_JSON_BUDGET, A_JSON_CTOR, A_JSON_VALID, A_JSON_READ_SIZE,
    A_JSON_VALUE_COUNT, A_JSON_ROOT, A_JSON_DROP, A_JSON_PARSE_BUDGET,
    A_JSON_PARSE_FILE, A_JSON_VIEW_OF, A_JSON_CLOSE_STREAM, A_JSON_FREE,
    A_JSON_VIEW_VALID, A_JSON_VIEW_IS_OBJECT, A_JSON_VIEW_IS_ARRAY,
    A_JSON_VIEW_IS_NUMBER, A_JSON_VIEW_IS_STRING, A_JSON_VIEW_IS_BOOL,
    A_JSON_VIEW_GET_INT, A_JSON_VIEW_GET_DOUBLE, A_JSON_VIEW_GET_BOOL,
    A_JSON_VIEW_GET_STRING, A_JSON_VIEW_SIZE, A_JSON_VIEW_AT,
    A_JSON_VIEW_MEMBER, A_JSON_VIEW_WRITE,
    A_JSON_BUILDER_CTOR, A_JSON_BUILDER_VALID, A_JSON_BUILDER_ADD_INT,
    A_JSON_BUILDER_ADD_DOUBLE, A_JSON_BUILDER_ADD_BOOL,
    A_JSON_BUILDER_ADD_STRING, A_JSON_BUILDER_WRITE, A_JSON_BUILDER_DROP,
    A_COUNT
};

enum {
    C_VALID, C_NULL, C_ZERO, C_ONE_OVER, C_EOF, C_INVALID_HANDLE,
    C_DOUBLE_CLOSE, C_MALFORMED, C_TRUNCATED, C_DEPTH_16384, C_DEPTH_16385,
    C_EXACT_FIT, C_CRLF, C_ALLOC_FAIL, C_COUNT
};

#define CS(k) (1u << (k))
#define ALL_CASES ((1u << C_COUNT) - 1u)

static const char *API_NAMES[A_COUNT] = {
    "io.println", "io.stdlib_io_token", "io.read_line", "io.read_int",
    "io.read_long", "io.read_float", "io.read_double",
    "file.ctor", "file.is_open", "file.native", "file.read", "file.read_line",
    "file.write", "file.write_bytes", "file.seek", "file.flush", "file.close",
    "file.drop", "file_borrow", "file_temp",
    "fs.exists", "fs.is_file", "fs.is_dir", "fs.size", "fs.remove",
    "fs.rename", "fs.mkdir", "fs.mkdtemp",
    "json.json_input_within_budget", "json.ctor", "json.valid",
    "json.read_size", "json.value_count", "json.root", "json.drop",
    "json.parse_budget", "json.parse_file", "json_view_of",
    "json_close_stream", "json_free",
    "json_view.valid", "json_view.is_object", "json_view.is_array",
    "json_view.is_number", "json_view.is_string", "json_view.is_bool",
    "json_view.get_int", "json_view.get_double", "json_view.get_bool",
    "json_view.get_string", "json_view.size", "json_view.at",
    "json_view.member", "json_view.write",
    "json_builder.ctor", "json_builder.valid", "json_builder.add_int",
    "json_builder.add_double", "json_builder.add_bool",
    "json_builder.add_string", "json_builder.write", "json_builder.drop"
};

static const char *API_HEADERS[A_COUNT] = {
    "io.hp","io.hp","io.hp","io.hp","io.hp","io.hp","io.hp",
    "file.hp","file.hp","file.hp","file.hp","file.hp","file.hp","file.hp",
    "file.hp","file.hp","file.hp","file.hp","file.hp","file.hp",
    "fs.hp","fs.hp","fs.hp","fs.hp","fs.hp","fs.hp","fs.hp","fs.hp",
    "json.hp","json.hp","json.hp","json.hp","json.hp","json.hp","json.hp",
    "json.hp","json.hp","json.hp","json.hp","json.hp",
    "json.hp","json.hp","json.hp","json.hp","json.hp","json.hp",
    "json.hp","json.hp","json.hp","json.hp","json.hp","json.hp","json.hp",
    "json.hp","json.hp","json.hp","json.hp","json.hp","json.hp","json.hp"
};

static const char *CASE_NAMES[C_COUNT] = {
    "valid","null","zero","one_over","eof","invalid_handle","double_close",
    "malformed","truncated","depth_16384","depth_16385","exact_fit","crlf",
    "alloc_fail"
};

/* Case matrix: api -> applicable edge cases. */
static const unsigned short API_CASES[A_COUNT] = {
    /* io.hp */
    CS(C_VALID),
    CS(C_VALID)|CS(C_NULL)|CS(C_ZERO)|CS(C_ONE_OVER)|CS(C_EOF)|CS(C_EXACT_FIT)|CS(C_CRLF),
    CS(C_VALID)|CS(C_NULL)|CS(C_ZERO)|CS(C_ONE_OVER)|CS(C_EOF)|CS(C_EXACT_FIT)|CS(C_CRLF),
    CS(C_VALID)|CS(C_NULL)|CS(C_ZERO)|CS(C_ONE_OVER)|CS(C_EOF)|CS(C_MALFORMED)|CS(C_TRUNCATED)|CS(C_EXACT_FIT)|CS(C_CRLF),
    CS(C_VALID)|CS(C_NULL)|CS(C_ZERO)|CS(C_ONE_OVER)|CS(C_EOF)|CS(C_MALFORMED)|CS(C_TRUNCATED)|CS(C_EXACT_FIT)|CS(C_CRLF),
    CS(C_VALID)|CS(C_NULL)|CS(C_ZERO)|CS(C_ONE_OVER)|CS(C_EOF)|CS(C_MALFORMED)|CS(C_TRUNCATED)|CS(C_EXACT_FIT)|CS(C_CRLF),
    CS(C_VALID)|CS(C_NULL)|CS(C_ZERO)|CS(C_ONE_OVER)|CS(C_EOF)|CS(C_MALFORMED)|CS(C_TRUNCATED)|CS(C_EXACT_FIT)|CS(C_CRLF),
    /* file.hp */
    CS(C_VALID)|CS(C_NULL)|CS(C_ZERO)|CS(C_INVALID_HANDLE),
    CS(C_VALID)|CS(C_NULL)|CS(C_INVALID_HANDLE)|CS(C_DOUBLE_CLOSE),
    CS(C_VALID)|CS(C_NULL)|CS(C_DOUBLE_CLOSE),
    CS(C_VALID)|CS(C_NULL)|CS(C_ZERO)|CS(C_ONE_OVER)|CS(C_EOF)|CS(C_INVALID_HANDLE)|CS(C_DOUBLE_CLOSE)|CS(C_EXACT_FIT)|CS(C_CRLF),
    CS(C_VALID)|CS(C_NULL)|CS(C_ZERO)|CS(C_ONE_OVER)|CS(C_EOF)|CS(C_INVALID_HANDLE)|CS(C_EXACT_FIT)|CS(C_CRLF),
    CS(C_VALID)|CS(C_NULL)|CS(C_ZERO)|CS(C_INVALID_HANDLE)|CS(C_DOUBLE_CLOSE)|CS(C_CRLF),
    CS(C_VALID)|CS(C_NULL)|CS(C_ZERO)|CS(C_ONE_OVER)|CS(C_INVALID_HANDLE)|CS(C_DOUBLE_CLOSE),
    CS(C_VALID)|CS(C_ZERO)|CS(C_INVALID_HANDLE)|CS(C_DOUBLE_CLOSE),
    CS(C_VALID)|CS(C_INVALID_HANDLE)|CS(C_DOUBLE_CLOSE),
    CS(C_VALID)|CS(C_NULL)|CS(C_INVALID_HANDLE)|CS(C_DOUBLE_CLOSE),
    CS(C_VALID)|CS(C_DOUBLE_CLOSE),
    CS(C_VALID)|CS(C_NULL)|CS(C_DOUBLE_CLOSE),
    CS(C_VALID)|CS(C_NULL)|CS(C_ALLOC_FAIL),
    /* fs.hp */
    CS(C_VALID)|CS(C_NULL)|CS(C_ZERO)|CS(C_MALFORMED)|CS(C_EXACT_FIT)|CS(C_CRLF),
    CS(C_VALID)|CS(C_NULL)|CS(C_ZERO)|CS(C_MALFORMED)|CS(C_EXACT_FIT)|CS(C_CRLF),
    CS(C_VALID)|CS(C_NULL)|CS(C_ZERO)|CS(C_MALFORMED)|CS(C_EXACT_FIT)|CS(C_CRLF),
    CS(C_VALID)|CS(C_NULL)|CS(C_ZERO)|CS(C_MALFORMED),
    CS(C_VALID)|CS(C_NULL)|CS(C_ZERO)|CS(C_DOUBLE_CLOSE)|CS(C_EXACT_FIT),
    CS(C_VALID)|CS(C_NULL)|CS(C_ZERO)|CS(C_INVALID_HANDLE)|CS(C_EXACT_FIT),
    CS(C_VALID)|CS(C_NULL)|CS(C_ZERO)|CS(C_DOUBLE_CLOSE)|CS(C_EXACT_FIT),
    CS(C_VALID)|CS(C_NULL)|CS(C_ZERO)|CS(C_ONE_OVER)|CS(C_MALFORMED)|CS(C_EXACT_FIT),
    /* json.hp */
    CS(C_VALID)|CS(C_NULL)|CS(C_ZERO)|CS(C_ONE_OVER)|CS(C_DEPTH_16384)|CS(C_DEPTH_16385)|CS(C_MALFORMED)|CS(C_CRLF),
    CS(C_VALID)|CS(C_NULL)|CS(C_ZERO)|CS(C_MALFORMED)|CS(C_TRUNCATED)|CS(C_DEPTH_16384)|CS(C_DEPTH_16385)|CS(C_EXACT_FIT)|CS(C_CRLF),
    CS(C_VALID)|CS(C_NULL)|CS(C_MALFORMED),
    CS(C_VALID)|CS(C_NULL)|CS(C_MALFORMED),
    CS(C_VALID)|CS(C_NULL)|CS(C_MALFORMED),
    CS(C_VALID)|CS(C_NULL)|CS(C_MALFORMED),
    CS(C_VALID)|CS(C_DOUBLE_CLOSE),
    CS(C_VALID)|CS(C_NULL)|CS(C_ZERO)|CS(C_ONE_OVER)|CS(C_MALFORMED)|CS(C_TRUNCATED)|CS(C_DEPTH_16384)|CS(C_DEPTH_16385)|CS(C_EXACT_FIT)|CS(C_CRLF)|CS(C_ALLOC_FAIL),
    CS(C_VALID)|CS(C_NULL)|CS(C_ZERO)|CS(C_MALFORMED)|CS(C_TRUNCATED)|CS(C_DEPTH_16384)|CS(C_DEPTH_16385)|CS(C_EXACT_FIT)|CS(C_CRLF)|CS(C_ALLOC_FAIL),
    CS(C_VALID)|CS(C_NULL),
    CS(C_VALID)|CS(C_NULL),
    CS(C_VALID)|CS(C_NULL),
    CS(C_VALID)|CS(C_NULL)|CS(C_MALFORMED),
    CS(C_VALID)|CS(C_NULL)|CS(C_MALFORMED),
    CS(C_VALID)|CS(C_NULL)|CS(C_MALFORMED),
    CS(C_VALID)|CS(C_NULL)|CS(C_MALFORMED),
    CS(C_VALID)|CS(C_NULL)|CS(C_MALFORMED),
    CS(C_VALID)|CS(C_NULL)|CS(C_MALFORMED),
    CS(C_VALID)|CS(C_NULL)|CS(C_ZERO)|CS(C_ONE_OVER)|CS(C_INVALID_HANDLE)|CS(C_MALFORMED),
    CS(C_VALID)|CS(C_NULL)|CS(C_MALFORMED),
    CS(C_VALID)|CS(C_NULL)|CS(C_MALFORMED),
    CS(C_VALID)|CS(C_NULL)|CS(C_MALFORMED),
    CS(C_VALID)|CS(C_NULL)|CS(C_ZERO),
    CS(C_VALID)|CS(C_ZERO)|CS(C_ONE_OVER)|CS(C_INVALID_HANDLE),
    CS(C_VALID)|CS(C_NULL)|CS(C_INVALID_HANDLE),
    CS(C_VALID)|CS(C_NULL),                                            /* json_view.write */
    CS(C_VALID),                                                       /* json_builder.ctor */
    CS(C_VALID)|CS(C_DOUBLE_CLOSE),                                    /* json_builder.valid */
    CS(C_VALID)|CS(C_NULL)|CS(C_ZERO)|CS(C_INVALID_HANDLE),            /* add_int */
    CS(C_VALID)|CS(C_NULL)|CS(C_ZERO)|CS(C_MALFORMED)|CS(C_INVALID_HANDLE), /* add_double */
    CS(C_VALID)|CS(C_NULL)|CS(C_INVALID_HANDLE),                       /* add_bool */
    CS(C_VALID)|CS(C_NULL)|CS(C_ZERO)|CS(C_INVALID_HANDLE),            /* add_string */
    CS(C_VALID)|CS(C_DOUBLE_CLOSE),                                    /* json_builder.write */
    CS(C_VALID)|CS(C_DOUBLE_CLOSE)                                     /* json_builder.drop */
};

/* ---- helpers ----------------------------------------------------------- */
#define CHK(x) do { if (!(x)) ok = 0; } while (0)
#define COPY(s) do { const char *_p = (s); size_t _n = strlen(_p); \
                     memcpy(in + in_len, _p, _n); in_len += _n; } while (0)
#define COPYT(s) do { const char *_p = (s); size_t _n = strlen(_p); \
                      memcpy(text + len, _p, _n); len += _n; } while (0)
#define COPYF(s) do { const char *_p = (s); size_t _n = strlen(_p); \
                      memcpy(fin + fin_len, _p, _n); fin_len += _n; } while (0)

static void fill_a5(void *p, size_t n) { memset(p, 0xA5, n); }

static int all_a5(const char *p, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) if (p[i] != (char)0xA5) return 0;
    return 1;
}

/* Left/right guards intact AND every byte from `used` to CAP untouched. */
static int buf_clean(size_t used) {
    size_t i;
    if (!all_a5(g_buf, GUARD)) return 0;
    if (!all_a5(g_buf + GUARD + CAP, GUARD)) return 0;
    if (used > CAP) return 0;
    for (i = used; i < CAP; i++)
        if (g_buf[GUARD + i] != (char)0xA5) return 0;
    return 1;
}

static int text_clean(void) {
    return all_a5(g_text, GUARD) && all_a5(g_text + GUARD + BIG, GUARD);
}

static char g_casepath[600];

static void case_path(int id, const char *suffix) {
    int n = snprintf(g_casepath, sizeof(g_casepath), "%s/case_%05d_%s",
                     TMPDIR, id, suffix);
    if (n < 0 || (size_t)n >= sizeof(g_casepath)) g_casepath[0] = '\0';
}

static void path_copy(char *dst, size_t cap) {
    size_t n = strlen(g_casepath);
    if (n >= cap) n = cap - 1u;
    memcpy(dst, g_casepath, n);
    dst[n] = '\0';
}

static int write_file(const char *path, const char *data, size_t len) {
    FILE *fp = fopen(path, "wb");
    size_t w;
    int rc;
    if (fp == (FILE *)0) return 0;
    if (len != 0u) {
        w = fwrite(data, 1, len, fp);
        if (w != len) { (void)fclose(fp); return 0; }
    }
    rc = fclose(fp);
    return rc == 0;
}

static int file_content_is(const char *path, const char *expected, size_t len) {
    FILE *rf = fopen(path, "rb");
    char back[64];
    size_t got;
    if (rf == (FILE *)0) return 0;
    got = fread(back, 1, sizeof(back), rf);
    if (fclose(rf) != 0) return 0;
    if (got != len) return 0;
    return memcmp(back, expected, len) == 0;
}

static int feed_stdin(int id, const char *data, size_t len) {
    const char *path;
    FILE *w;
    FILE *rd;
    size_t wrote;
    case_path(id, "in");
    path = g_casepath;
    w = fopen(path, "wb");
    if (w == (FILE *)0) return 0;
    if (len != 0u) {
        wrote = fwrite(data, 1, len, w);
        if (wrote != len) { (void)fclose(w); (void)unlink(path); return 0; }
    }
    if (fclose(w) != 0) { (void)unlink(path); return 0; }
    rd = freopen(path, "rb", stdin);
    if (rd == (FILE *)0) { (void)unlink(path); return 0; }
    return 1;
}

static void build_nested(char *dst, size_t depth, size_t *len) {
    size_t i, n = 0;
    for (i = 0; i < depth; i++) dst[n++] = '[';
    for (i = 0; i < depth; i++) dst[n++] = ']';
    *len = n;
}

/* ===================== io.hp cases ====================================== */
static int case_io(int api, int cas, int id) {
    error e = error();
    char in[128];
    size_t in_len = 0, size = 0;
    char *buf = g_buf + GUARD;
    int rc, ok = 1;

    if (api == A_IO_PRINTLN) {
        println();
        return 1;
    }

    fill_a5(g_buf, sizeof(g_buf));
    fill_a5(g_nguard1, sizeof(g_nguard1));
    fill_a5(g_nguard2, sizeof(g_nguard2));

    if (api == A_IO_TOKEN) {
        switch (cas) {
        case C_NULL:     COPY("hello\n"); size = 8u; break;
        case C_ZERO:     COPY("hello\n"); size = 0u; break;
        case C_EOF:      in_len = 0u; size = 8u; break;
        case C_ONE_OVER: COPY("abcdefghij\n"); size = 8u; break;
        case C_EXACT_FIT:COPY("abcdefg\n"); size = 8u; break;
        case C_CRLF:     COPY("hello\r\n"); size = 16u; break;
        default:         COPY("hello\n"); size = 16u; break;
        }
        if (!feed_stdin(id, in, in_len)) return 0;
        rc = stdlib_io_token(cas == C_NULL ? (char *)0 : buf, size);
        if (cas == C_NULL || cas == C_ZERO || cas == C_EOF)
            CHK(rc == 1);
        else if (cas == C_ONE_OVER) {
            CHK(rc == 2);
            CHK(memcmp(buf, "abcdefg", 7u) == 0);
        } else if (cas == C_EXACT_FIT) {
            CHK(rc == 0);
            CHK(memcmp(buf, "abcdefg", 7u) == 0);
        } else {
            CHK(rc == 0);
            CHK(memcmp(buf, "hello", 5u) == 0);
        }
        CHK(buf_clean(size));
        return ok;
    }

    if (api == A_IO_READ_LINE) {
        switch (cas) {
        case C_NULL:     COPY("hello\n"); size = 16u; break;
        case C_ZERO:     COPY("hello\n"); size = 0u; break;
        case C_EOF:      in_len = 0u; size = 16u; break;
        case C_ONE_OVER: COPY("abcdefghij\n"); size = 8u; break;
        case C_EXACT_FIT:COPY("abcdefg\n"); size = 8u; break;
        case C_CRLF:     COPY("hello\r\n"); size = 16u; break;
        default:         COPY("hello\n"); size = 16u; break;
        }
        if (!feed_stdin(id, in, in_len)) return 0;
        error_reset(&e);
        rc = read_line(cas == C_NULL ? (char *)0 : buf, size, &e);
        if (cas == C_NULL || cas == C_ZERO) {
            CHK(rc == STDLIB_FAIL);
            CHK(e.failed_now());
            CHK(e.category() == STDLIB_CATEGORY_INVALID);
        } else if (cas == C_EOF) {
            CHK(rc == STDLIB_EOF);
            CHK(e.ok());
        } else if (cas == C_ONE_OVER) {
            CHK(rc == STDLIB_FAIL);
            CHK(e.category() == STDLIB_CATEGORY_BOUNDS);
            CHK(buf[0] == '\0');
        } else {
            CHK(rc == STDLIB_OK);
            CHK(e.ok());
            CHK(memcmp(buf, cas == C_EXACT_FIT ? "abcdefg" : "hello",
                       cas == C_EXACT_FIT ? 7u : 5u) == 0);
        }
        CHK(buf_clean(size));
        return ok;
    }

    /* numeric readers */
    switch (cas) {
    case C_VALID:  COPY("42\n"); break;
    case C_ZERO:   COPY("0\n"); break;
    case C_ONE_OVER: {
        int i;
        for (i = 0; i < 70; i++) in[in_len++] = '9';
        COPY("\n");
        break;
    }
    case C_EOF:       in_len = 0u; break;
    case C_MALFORMED: COPY("abc\n"); break;
    case C_TRUNCATED: COPY("12x\n"); break;
    case C_EXACT_FIT:
        if (api == A_IO_READ_INT) COPY("2147483647\n");
        else if (api == A_IO_READ_LONG) COPY("-9223372036854775808\n");
        else {
            int i;
            in[in_len++] = '1'; in[in_len++] = '.';
            for (i = 0; i < 61; i++) in[in_len++] = '0';
            COPY("\n");
        }
        break;
    case C_CRLF:   COPY("42\r\n"); break;
    default:       COPY("42\n"); break;
    }
    if (!feed_stdin(id, in, in_len)) return 0;
    error_reset(&e);
    if (api == A_IO_READ_INT)
        rc = read_int(cas == C_NULL ? (int *)0 : &g_int_out, &e);
    else if (api == A_IO_READ_LONG)
        rc = read_long(cas == C_NULL ? (long *)0 : &g_long_out, &e);
    else if (api == A_IO_READ_FLOAT)
        rc = read_float(cas == C_NULL ? (float *)0 : &g_float_out, &e);
    else
        rc = read_double(cas == C_NULL ? (double *)0 : &g_double_out, &e);

    if (cas == C_NULL) { CHK(rc == STDLIB_FAIL); CHK(e.failed_now()); }
    else if (cas == C_EOF) { CHK(rc == STDLIB_EOF); CHK(e.ok()); }
    else if (cas == C_VALID || cas == C_ZERO || cas == C_EXACT_FIT || cas == C_CRLF) {
        CHK(rc == STDLIB_OK);
        CHK(e.ok());
        if (api == A_IO_READ_INT)
            CHK(g_int_out == (cas == C_ZERO ? 0 : (cas == C_EXACT_FIT ? INT_MAX : 42)));
        else if (api == A_IO_READ_LONG)
            CHK(g_long_out == (cas == C_ZERO ? 0L : (cas == C_EXACT_FIT ? LONG_MIN : 42L)));
        else if (api == A_IO_READ_FLOAT)
            CHK(g_float_out == (cas == C_ZERO ? 0.0f : (cas == C_EXACT_FIT ? 1.0f : 42.0f)));
        else
            CHK(g_double_out == (cas == C_ZERO ? 0.0 : (cas == C_EXACT_FIT ? 1.0 : 42.0)));
    } else {
        CHK(rc == STDLIB_FAIL);
        CHK(e.failed_now());
    }
    CHK(all_a5(g_nguard1, sizeof(g_nguard1)));
    CHK(all_a5(g_nguard2, sizeof(g_nguard2)));
    return ok;
}

/* ===================== file.hp cases ==================================== */
static int case_file(int api, int cas, int id) {
    error e = error();
    file f;
    char *buf = g_buf + GUARD;
    char *text = g_text + GUARD;
    int rc, ok = 1;
    size_t n;
    FILE *raw;

    case_path(id, "fl");
    path_copy(g_path, sizeof(g_path));
    fill_a5(g_buf, sizeof(g_buf));
    fill_a5(g_text, sizeof(g_text));
    fill_a5(g_fguard1, sizeof(g_fguard1));
    fill_a5(g_fguard2, sizeof(g_fguard2));

    switch (api) {
    case A_FILE_CTOR:
        if (cas == C_VALID) {
            if (!write_file(g_path, "abc", 3u)) return 0;
            f = file(g_path, "rb");
            CHK(f.is_open());
            CHK(f.native() != (FILE *)0);
            f.drop();
            (void)unlink(g_path);
        } else if (cas == C_NULL) {
            f = file((const char *)0, (const char *)0);
            CHK(!f.is_open());
            CHK(f.native() == (FILE *)0);
            f.drop();
        } else if (cas == C_ZERO) {
            f = file("", "rb");
            CHK(!f.is_open());
            f.drop();
        } else {
            f = file(g_path, "rb");
            CHK(!f.is_open());
            f.drop();
        }
        break;

    case A_FILE_IS_OPEN:
        if (cas == C_VALID) {
            if (!write_file(g_path, "x", 1u)) return 0;
            f = file(g_path, "rb");
            CHK(f.is_open() == 1);
            f.drop();
            CHK(f.is_open() == 0);
            (void)unlink(g_path);
        } else if (cas == C_NULL) {
            f = file_borrow((FILE *)0);
            CHK(f.is_open() == 0);
            f.drop();
        } else if (cas == C_INVALID_HANDLE) {
            f = file(g_path, "rb");
            CHK(f.is_open() == 0);
            f.drop();
        } else {
            if (!write_file(g_path, "x", 1u)) return 0;
            f = file(g_path, "rb");
            error_reset(&e);
            CHK(f.close(&e) == STDLIB_OK);
            CHK(f.is_open() == 0);
            (void)unlink(g_path);
        }
        break;

    case A_FILE_NATIVE:
        if (cas == C_VALID) {
            if (!write_file(g_path, "x", 1u)) return 0;
            f = file(g_path, "rb");
            CHK(f.native() != (FILE *)0);
            f.drop();
            CHK(f.native() == (FILE *)0);
            (void)unlink(g_path);
        } else if (cas == C_NULL) {
            f = file_borrow((FILE *)0);
            CHK(f.native() == (FILE *)0);
            f.drop();
        } else {
            if (!write_file(g_path, "x", 1u)) return 0;
            f = file(g_path, "rb");
            error_reset(&e);
            CHK(f.close(&e) == STDLIB_OK);
            CHK(f.native() == (FILE *)0);
            (void)unlink(g_path);
        }
        break;

    case A_FILE_READ:
        {
            const char *seed = (cas == C_CRLF) ? "a\r\n"
                             : (cas == C_EOF) ? "" : "0123456789";
            size_t seed_len = (cas == C_CRLF) ? 3u : (cas == C_EOF) ? 0u : 10u;
            if (cas != C_INVALID_HANDLE && !write_file(g_path, seed, seed_len)) return 0;
        }
        if (cas == C_INVALID_HANDLE)
            f = file((const char *)0, (const char *)0);
        else
            f = file(g_path, "rb");
        error_reset(&e);
        switch (cas) {
        case C_VALID:
            rc = f.read(buf, 10u, &e);
            CHK(rc == STDLIB_OK);
            CHK(memcmp(buf, "0123456789", 10u) == 0);
            CHK(buf_clean(10u));
            break;
        case C_NULL:
            rc = f.read((char *)0, 8u, &e);
            CHK(rc == STDLIB_FAIL);
            CHK(e.failed_now());
            CHK(buf_clean(0u));
            break;
        case C_ZERO:
            rc = f.read(buf, 0u, &e);
            CHK(rc == STDLIB_FAIL);
            CHK(buf_clean(0u));
            break;
        case C_ONE_OVER:
            rc = f.read(buf, 4u, &e);
            CHK(rc == STDLIB_OK);
            CHK(memcmp(buf, "0123", 4u) == 0);
            CHK(buf_clean(4u));
            break;
        case C_EOF:
            {
                file g = file(g_path, "rb");
                error_reset(&e);
                rc = g.read(buf, 8u, &e);
                CHK(rc == STDLIB_EOF);
                CHK(e.ok());
                CHK(buf_clean(0u));
                g.drop();
            }
            break;
        case C_INVALID_HANDLE:
            rc = f.read(buf, 8u, &e);
            CHK(rc == STDLIB_FAIL);
            CHK(e.failed_now());
            CHK(buf_clean(0u));
            break;
        case C_DOUBLE_CLOSE:
            CHK(f.close(&e) == STDLIB_OK);
            CHK(f.close(&e) == STDLIB_FAIL);
            CHK(e.category() == STDLIB_CATEGORY_LIFECYCLE);
            CHK(f.read(buf, 8u, &e) == STDLIB_FAIL);
            CHK(buf_clean(0u));
            break;
        case C_EXACT_FIT:
            rc = f.read(buf, 10u, &e);
            CHK(rc == STDLIB_OK);
            CHK(f.read(buf, 1u, &e) == STDLIB_EOF);
            CHK(buf_clean(10u));
            break;
        default: /* CRLF */
            rc = f.read(buf, 3u, &e);
            CHK(rc == STDLIB_OK);
            CHK(buf[0] == 'a' && buf[1] == '\r' && buf[2] == '\n');
            CHK(buf_clean(3u));
            break;
        }
        f.drop();
        (void)unlink(g_path);
        break;

    case A_FILE_READ_LINE:
        {
            char fin[64];
            size_t fin_len = 0u, size = 0u;
            switch (cas) {
            case C_NULL:      COPYF("hello\n"); size = 16u; break;
            case C_ZERO:      COPYF("hello\n"); size = 0u; break;
            case C_EOF:       fin_len = 0u; size = 16u; break;
            case C_ONE_OVER:  COPYF("abcdefghij\n"); size = 8u; break;
            case C_EXACT_FIT: COPYF("abcdefg\n"); size = 8u; break;
            case C_CRLF:      COPYF("hello\r\n"); size = 16u; break;
            case C_INVALID_HANDLE: COPYF("hello\n"); size = 16u; break;
            default:          COPYF("hello\n"); size = 16u; break;
            }
            if (!write_file(g_path, fin, fin_len)) return 0;
            if (cas == C_INVALID_HANDLE)
                f = file((const char *)0, (const char *)0);
            else
                f = file(g_path, "rb");
            error_reset(&e);
            rc = f.read_line(cas == C_NULL ? (char *)0 : buf, size, &e);
            if (cas == C_NULL || cas == C_ZERO) {
                CHK(rc == STDLIB_FAIL);
                CHK(e.failed_now());
            } else if (cas == C_EOF) {
                CHK(rc == STDLIB_EOF);
                CHK(e.ok());
            } else if (cas == C_ONE_OVER) {
                CHK(rc == STDLIB_FAIL);
                CHK(e.category() == STDLIB_CATEGORY_BOUNDS);
                CHK(buf[0] == '\0');
            } else if (cas == C_INVALID_HANDLE) {
                CHK(rc == STDLIB_FAIL);
                CHK(e.failed_now());
            } else {
                CHK(rc == STDLIB_OK);
                CHK(e.ok());
                CHK(memcmp(buf, cas == C_EXACT_FIT ? "abcdefg" : "hello",
                           cas == C_EXACT_FIT ? 7u : 5u) == 0);
            }
            CHK(buf_clean(size));
            f.drop();
            (void)unlink(g_path);
        }
        break;

    case A_FILE_WRITE:
        f = file(g_path, "wb");
        error_reset(&e);
        switch (cas) {
        case C_VALID:
            CHK(f.write("hello") == 1);
            f.drop();
            CHK(file_content_is(g_path, "hello", 5u) == 1);
            break;
        case C_NULL:     CHK(f.write((const char *)0) == 0); break;
        case C_ZERO:     CHK(f.write("") == 1); break;
        case C_INVALID_HANDLE:
            {
                file g = file((const char *)0, (const char *)0);
                CHK(g.write("x") == 0);
                g.drop();
            }
            break;
        case C_DOUBLE_CLOSE:
            CHK(f.close(&e) == STDLIB_OK);
            CHK(f.write("x") == 0);
            break;
        default: /* CRLF */
            CHK(f.write("a\r\n") == 1);
            f.drop();
            CHK(file_content_is(g_path, "a\r\n", 3u) == 1);
            break;
        }
        f.drop();
        (void)unlink(g_path);
        break;

    case A_FILE_WRITE_BYTES:
        f = file(g_path, "wb");
        error_reset(&e);
        if (cas == C_ONE_OVER)
            memcpy(text, "abcdefghijklmnopqrstuvwxyz012345", 32u);
        switch (cas) {
        case C_VALID:
            CHK(f.write_bytes("0123456789abcdef", 16u, &e) == STDLIB_OK);
            CHK(e.ok());
            break;
        case C_NULL:
            CHK(f.write_bytes((const char *)0, 16u, &e) == STDLIB_FAIL);
            CHK(e.failed_now());
            break;
        case C_ZERO:
            CHK(f.write_bytes((const char *)0, 0u, &e) == STDLIB_OK);
            CHK(e.ok());
            break;
        case C_ONE_OVER:
            CHK(f.write_bytes(text, 16u, &e) == STDLIB_OK);
            CHK(text_clean());
            break;
        case C_INVALID_HANDLE:
            {
                file g = file((const char *)0, (const char *)0);
                CHK(g.write_bytes("x", 1u, &e) == STDLIB_FAIL);
                g.drop();
            }
            break;
        default: /* DOUBLE_CLOSE */
            CHK(f.close(&e) == STDLIB_OK);
            CHK(f.write_bytes("x", 1u, &e) == STDLIB_FAIL);
            break;
        }
        f.drop();
        if (cas == C_VALID)
            CHK(file_content_is(g_path, "0123456789abcdef", 16u) == 1);
        else if (cas == C_ONE_OVER)
            CHK(file_content_is(g_path, "abcdefghijklmnop", 16u) == 1);
        (void)unlink(g_path);
        break;

    case A_FILE_SEEK:
        if (!write_file(g_path, "0123456789", 10u)) return 0;
        f = file(g_path, "rb+");
        error_reset(&e);
        switch (cas) {
        case C_VALID:
            CHK(f.seek(2L, SEEK_SET, &e) == STDLIB_OK);
            CHK(f.read(buf, 2u, &e) == STDLIB_OK);
            CHK(memcmp(buf, "23", 2u) == 0);
            CHK(buf_clean(2u));
            break;
        case C_ZERO:
            CHK(f.seek(0L, SEEK_SET, &e) == STDLIB_OK);
            CHK(f.read(buf, 1u, &e) == STDLIB_OK);
            CHK(buf[0] == '0');
            break;
        case C_INVALID_HANDLE:
            {
                file g = file((const char *)0, (const char *)0);
                CHK(g.seek(0L, SEEK_SET, &e) == STDLIB_FAIL);
                g.drop();
            }
            break;
        default: /* invalid whence */
            CHK(f.seek(0L, 99999, &e) == STDLIB_FAIL);
            CHK(e.failed_now());
            CHK(f.close(&e) == STDLIB_OK);
            CHK(f.seek(0L, SEEK_SET, &e) == STDLIB_FAIL);
            break;
        }
        f.drop();
        (void)unlink(g_path);
        break;

    case A_FILE_FLUSH:
        f = file(g_path, "wb+");
        error_reset(&e);
        if (cas == C_VALID) {
            CHK(f.write_bytes("x", 1u, &e) == STDLIB_OK);
            CHK(f.flush(&e) == STDLIB_OK);
            CHK(e.ok());
        } else if (cas == C_INVALID_HANDLE) {
            {
                file g = file((const char *)0, (const char *)0);
                CHK(g.flush(&e) == STDLIB_FAIL);
                g.drop();
            }
        } else {
            CHK(f.close(&e) == STDLIB_OK);
            CHK(f.flush(&e) == STDLIB_FAIL);
        }
        f.drop();
        (void)unlink(g_path);
        break;

    case A_FILE_CLOSE:
        if (cas != C_INVALID_HANDLE && !write_file(g_path, "x", 1u)) return 0;
        if (cas == C_INVALID_HANDLE)
            f = file((const char *)0, (const char *)0);
        else
            f = file(g_path, "rb");
        error_reset(&e);
        if (cas == C_VALID) {
            CHK(f.close(&e) == STDLIB_OK);
            CHK(e.ok());
        } else if (cas == C_NULL) {
            CHK(f.close((error *)0) == STDLIB_OK);
        } else if (cas == C_INVALID_HANDLE) {
            CHK(f.close(&e) == STDLIB_FAIL);
            CHK(e.failed_now());
        } else {
            CHK(f.close(&e) == STDLIB_OK);
            CHK(f.close(&e) == STDLIB_FAIL);
            CHK(e.category() == STDLIB_CATEGORY_LIFECYCLE);
        }
        f.drop();
        (void)unlink(g_path);
        break;

    case A_FILE_DROP:
        if (!write_file(g_path, "x", 1u)) return 0;
        f = file(g_path, "rb");
        f.drop();
        CHK(!f.is_open());
        if (cas == C_DOUBLE_CLOSE) f.drop();
        CHK(!f.is_open());
        CHK(all_a5(g_fguard1, sizeof(g_fguard1)));
        CHK(all_a5(g_fguard2, sizeof(g_fguard2)));
        (void)unlink(g_path);
        break;

    case A_FILE_BORROW:
        if (cas != C_NULL && !write_file(g_path, "ZY", 2u)) return 0;
        error_reset(&e);
        if (cas == C_NULL) {
            f = file_borrow((FILE *)0);
            CHK(!f.is_open());
            CHK(f.read(buf, 1u, &e) == STDLIB_FAIL);
            f.drop();
        } else {
            raw = fopen(g_path, "rb");
            if (raw == (FILE *)0) { CHK(0); break; }
            f = file_borrow(raw);
            CHK(f.is_open() == 1);
            if (cas == C_VALID) {
                CHK(f.read(buf, 1u, &e) == STDLIB_OK);
                CHK(buf[0] == 'Z');
                CHK(f.close(&e) == STDLIB_OK);
            } else {
                CHK(f.close(&e) == STDLIB_OK);
                CHK(f.close(&e) == STDLIB_FAIL);
            }
            /* Underlying stream must still be usable after borrowed close. */
            CHK(fgetc(raw) != EOF);
            (void)fclose(raw);
            (void)unlink(g_path);
        }
        break;

    default: /* A_FILE_TEMP */
        g_fail_tmpfile = (cas == C_ALLOC_FAIL) ? 1 : 0;
        error_reset(&e);
        f = cas == C_NULL ? file_temp((error *)0) : file_temp(&e);
        if (cas == C_ALLOC_FAIL) {
            CHK(!f.is_open());
            CHK(e.failed_now());
            CHK(e.category() == STDLIB_CATEGORY_IO);
        } else {
            CHK(f.is_open());
            if (cas == C_VALID) {
                CHK(f.write_bytes("temp", 4u, &e) == STDLIB_OK);
                CHK(f.seek(0L, SEEK_SET, &e) == STDLIB_OK);
                CHK(f.read(buf, 4u, &e) == STDLIB_OK);
                CHK(memcmp(buf, "temp", 4u) == 0);
                CHK(buf_clean(4u));
            }
        }
        f.drop();
        g_fail_tmpfile = 0;
        break;
    }
    CHK(all_a5(g_fguard1, sizeof(g_fguard1)));
    CHK(all_a5(g_fguard2, sizeof(g_fguard2)));
    return ok;
}

/* ===================== fs.hp cases ====================================== */
static int case_fs(int api, int cas, int id) {
    error e = error();
    long long sz;
    int rc, ok = 1;
    char tmpl[128];
    size_t n;

    case_path(id, "fs");
    path_copy(g_path, sizeof(g_path));
    case_path(id, "fz");
    path_copy(g_path2, sizeof(g_path2));

    if (api == A_FS_MKDTEMP) {
        fill_a5(g_buf, sizeof(g_buf));
        switch (cas) {
        case C_VALID:
        case C_CRLF:
            {
                char dir[512];
                int dn = snprintf(dir, sizeof(dir), "%s/dtXXXXXX", TMPDIR);
                if (dn < 0 || (size_t)dn >= sizeof(dir)) return 0;
                n = strlen(dir);
                memcpy(g_buf + GUARD, dir, n + 1u);
                rc = fs_mkdtemp(g_buf + GUARD, n + 1u, &e);
                CHK(rc == STDLIB_OK);
                CHK(fs_is_dir(g_buf + GUARD) == 1);
                CHK(buf_clean(n + 1u));
                if (rc == STDLIB_OK) (void)rmdir(g_buf + GUARD);
            }
            break;
        case C_NULL:
            rc = fs_mkdtemp((char *)0, 16u, &e);
            CHK(rc == STDLIB_FAIL);
            CHK(e.failed_now());
            break;
        case C_ZERO:
            rc = fs_mkdtemp(g_buf + GUARD, 0u, &e);
            CHK(rc == STDLIB_FAIL);
            break;
        case C_ONE_OVER:
            /* size 6 cannot hold "XXXXXX" + NUL: strnlen reaches size. */
            memcpy(g_buf + GUARD, "XXXXXX", 6u);
            rc = fs_mkdtemp(g_buf + GUARD, 6u, &e);
            CHK(rc == STDLIB_FAIL);
            break;
        case C_MALFORMED:
            memcpy(g_buf + GUARD, "abcdeY", 7u);
            rc = fs_mkdtemp(g_buf + GUARD, 7u, &e);
            CHK(rc == STDLIB_FAIL);
            memcpy(g_buf + GUARD, "/tmp/a\0XXXXXX", 12u);
            rc = fs_mkdtemp(g_buf + GUARD, 12u, &e);
            CHK(rc == STDLIB_FAIL);
            break;
        default: /* C_EXACT_FIT: size == strlen+1 */
            {
                char dir[512];
                int dn = snprintf(dir, sizeof(dir), "%s/dtXXXXXX", TMPDIR);
                if (dn < 0 || (size_t)dn >= sizeof(dir)) return 0;
                n = strlen(dir);
                memcpy(g_buf + GUARD, dir, n + 1u);
                rc = fs_mkdtemp(g_buf + GUARD, n + 1u, &e);
                CHK(rc == STDLIB_OK);
                if (rc == STDLIB_OK) (void)rmdir(g_buf + GUARD);
            }
            break;
        }
        return ok;
    }

    /* all other fs APIs operate on a regular file created here */
    if (!write_file(g_path, "hello", 5u)) return 0;

    switch (api) {
    case A_FS_EXISTS:
        if (cas == C_NULL)      CHK(fs_exists((const char *)0) == 0);
        else if (cas == C_ZERO) CHK(fs_exists("") == 0);
        else if (cas == C_MALFORMED) {
            char p[600];
            int pn = snprintf(p, sizeof(p), "%s\0tail", g_path);
            if (pn < 0) return 0;
            CHK(fs_exists(p) == 1); /* embedded NUL: prefix is the file */
        } else if (cas == C_CRLF) {
            char p[600];
            int pn = snprintf(p, sizeof(p), "%s\r", g_path);
            int fd;
            if (pn < 0 || (size_t)pn >= sizeof(p)) return 0;
            fd = open(p, O_CREAT | O_WRONLY, 0600);
            if (fd >= 0) (void)close(fd);
            CHK(fs_exists(p) == 1);
            (void)unlink(p);
        } else if (cas == C_EXACT_FIT) {
            char p[600];
            char name[256];
            int fd;
            memset(name, 'n', sizeof(name));
            name[255] = '\0';
            {
                int pn = snprintf(p, sizeof(p), "%s/%s", TMPDIR, name);
                if (pn < 0 || (size_t)pn >= sizeof(p)) return 0;
            }
            fd = open(p, O_CREAT | O_WRONLY, 0600);
            if (fd >= 0) (void)close(fd);
            CHK(fs_exists(p) == 1);
            (void)unlink(p);
        } else {
            CHK(fs_exists(g_path) == 1);
        }
        break;

    case A_FS_IS_FILE:
        if (cas == C_NULL)      CHK(fs_is_file((const char *)0) == 0);
        else if (cas == C_ZERO) CHK(fs_is_file("") == 0);
        else if (cas == C_MALFORMED) {
            char p[600];
            int pn = snprintf(p, sizeof(p), "%s\0tail", g_path);
            if (pn < 0) return 0;
            CHK(fs_is_file(p) == 1);
        } else if (cas == C_CRLF) {
            char p[600];
            int pn = snprintf(p, sizeof(p), "%s\r", g_path);
            int fd;
            if (pn < 0 || (size_t)pn >= sizeof(p)) return 0;
            fd = open(p, O_CREAT | O_WRONLY, 0600);
            if (fd >= 0) (void)close(fd);
            CHK(fs_is_file(p) == 1);
            (void)unlink(p);
        } else if (cas == C_EXACT_FIT) {
            char p[600];
            char name[256];
            int fd;
            memset(name, 'n', sizeof(name));
            name[255] = '\0';
            {
                int pn = snprintf(p, sizeof(p), "%s/%s", TMPDIR, name);
                if (pn < 0 || (size_t)pn >= sizeof(p)) return 0;
            }
            fd = open(p, O_CREAT | O_WRONLY, 0600);
            if (fd >= 0) (void)close(fd);
            CHK(fs_is_file(p) == 1);
            (void)unlink(p);
        } else {
            CHK(fs_is_file(g_path) == 1);
            CHK(fs_is_file(TMPDIR) == 0);
        }
        break;

    case A_FS_IS_DIR:
        if (cas == C_NULL)      CHK(fs_is_dir((const char *)0) == 0);
        else if (cas == C_ZERO) CHK(fs_is_dir("") == 0);
        else if (cas == C_MALFORMED) {
            char p[600];
            int pn = snprintf(p, sizeof(p), "%s\0tail", TMPDIR);
            if (pn < 0) return 0;
            CHK(fs_is_dir(p) == 1);
        } else if (cas == C_CRLF) {
            CHK(fs_is_dir(TMPDIR) == 1);
        } else if (cas == C_EXACT_FIT) {
            CHK(fs_is_dir(TMPDIR) == 1);
        } else {
            CHK(fs_is_dir(TMPDIR) == 1);
            CHK(fs_is_dir(g_path) == 0);
        }
        break;

    case A_FS_SIZE:
        if (cas == C_NULL) {
            sz = fs_size((const char *)0, &e);
            CHK(sz == -1);
            CHK(e.failed_now());
        } else if (cas == C_ZERO) {
            if (!write_file(g_path2, "", 0u)) return 0;
            sz = fs_size(g_path2, &e);
            CHK(sz == 0);
            CHK(e.ok());
            (void)unlink(g_path2);
        } else if (cas == C_MALFORMED) {
            sz = fs_size(g_path2, &e);
            CHK(sz == -1);
            CHK(e.failed_now());
        } else {
            sz = fs_size(g_path, &e);
            CHK(sz == 5);
            CHK(e.ok());
        }
        break;

    case A_FS_REMOVE:
        if (cas == C_NULL) {
            CHK(fs_remove((const char *)0, &e) == STDLIB_FAIL);
            CHK(e.failed_now());
        } else if (cas == C_ZERO) {
            CHK(fs_remove("", &e) == STDLIB_FAIL);
        } else if (cas == C_DOUBLE_CLOSE) {
            CHK(fs_remove(g_path, &e) == STDLIB_OK);
            CHK(fs_remove(g_path, &e) == STDLIB_FAIL);
            CHK(e.failed_now());
        } else if (cas == C_EXACT_FIT) {
            CHK(fs_remove(g_path, &e) == STDLIB_OK);
            CHK(fs_exists(g_path) == 0);
        } else {
            CHK(fs_remove(g_path, &e) == STDLIB_OK);
        }
        break;

    case A_FS_RENAME:
        if (cas == C_NULL) {
            CHK(fs_rename((const char *)0, g_path2, &e) == STDLIB_FAIL);
            CHK(fs_rename(g_path, (const char *)0, &e) == STDLIB_FAIL);
            CHK(e.failed_now());
        } else if (cas == C_ZERO) {
            CHK(fs_rename("", g_path2, &e) == STDLIB_FAIL);
        } else if (cas == C_INVALID_HANDLE) {
            CHK(fs_rename(g_path2, g_path, &e) == STDLIB_FAIL);
            CHK(e.failed_now());
        } else if (cas == C_EXACT_FIT) {
            CHK(fs_rename(g_path, g_path2, &e) == STDLIB_OK);
            CHK(fs_exists(g_path2) == 1);
            (void)unlink(g_path2);
        } else {
            if (!write_file(g_path2, "t", 1u)) return 0;
            CHK(fs_rename(g_path, g_path2, &e) == STDLIB_OK);
            CHK(fs_exists(g_path) == 0);
            CHK(fs_exists(g_path2) == 1);
            (void)unlink(g_path2);
        }
        break;

    default: /* A_FS_MKDIR */
        if (cas == C_NULL) {
            CHK(fs_mkdir((const char *)0, &e) == STDLIB_FAIL);
        } else if (cas == C_ZERO) {
            CHK(fs_mkdir("", &e) == STDLIB_FAIL);
        } else if (cas == C_DOUBLE_CLOSE) {
            CHK(fs_mkdir(g_path2, &e) == STDLIB_OK);
            CHK(fs_mkdir(g_path2, &e) == STDLIB_FAIL);
            (void)rmdir(g_path2);
        } else if (cas == C_EXACT_FIT) {
            char p[600];
            char name[256];
            memset(name, 'd', sizeof(name));
            name[255] = '\0';
            {
                int pn = snprintf(p, sizeof(p), "%s/%s", TMPDIR, name);
                if (pn < 0 || (size_t)pn >= sizeof(p)) return 0;
            }
            CHK(fs_mkdir(p, &e) == STDLIB_OK);
            (void)rmdir(p);
        } else {
            CHK(fs_mkdir(g_path2, &e) == STDLIB_OK);
            CHK(fs_is_dir(g_path2) == 1);
            (void)rmdir(g_path2);
        }
        break;
    }
    (void)unlink(g_path);
    (void)tmpl;
    return ok;
}

/* ===================== json.hp cases ==================================== */
static const char *JSON_DOC = "{\"arr\":[1,2,3],\"n\":1.5,\"b\":true,\"s\":\"hi\"}";

static int case_json(int api, int cas, int id) {
    error e = error();
    size_t len = 0u, olen = 0u, depth;
    int rc, ok = 1, within, ival = 0;
    double dval = 0.0;
    const char *sval = (const char *)0;
    char *out;
    char *text = g_text + GUARD;
    json j;
    json_view rv = json_view_of((const yyjson_val *)0);
    json_view v = json_view_of((const yyjson_val *)0);
    json_builder jb;
    FILE *fp;

    fill_a5(g_buf, sizeof(g_buf));
    fill_a5(g_text, sizeof(g_text));
    fill_a5(g_nguard1, sizeof(g_nguard1));
    fill_a5(g_nguard2, sizeof(g_nguard2));

    switch (api) {
    case A_JSON_BUDGET:
        switch (cas) {
        case C_VALID:      COPYT("[1]"); break;
        case C_NULL:
            error_reset(&e);
            CHK(json_input_within_budget((const char *)0, 4u, 16384u, &e) == 0);
            CHK(e.failed_now());
            return ok;
        case C_ZERO:
            error_reset(&e);
            CHK(json_input_within_budget("[1]", 0u, 16384u, &e) == 0);
            CHK(e.failed_now());
            return ok;
        case C_ONE_OVER:
            error_reset(&e);
            CHK(json_input_within_budget("[1]", SIZE_MAX, 16384u, &e) == 0);
            CHK(e.category() == STDLIB_CATEGORY_RANGE);
            return ok;
        case C_DEPTH_16384: build_nested(text, 16384u, &len); break;
        case C_DEPTH_16385: build_nested(text, 16385u, &len); break;
        case C_MALFORMED:  COPYT("[[}"); break;
        default:           COPYT("[\r\n 1\r\n]"); break; /* C_CRLF */
        }
        error_reset(&e);
        within = json_input_within_budget(text, len, 16384u, &e);
        if (cas == C_DEPTH_16385) {
            CHK(within == 0);
            CHK(e.category() == STDLIB_CATEGORY_RANGE);
        } else {
            CHK(within == 1);
            CHK(e.ok());
        }
        CHK(text_clean());
        return ok;

    case A_JSON_CTOR:
        switch (cas) {
        case C_NULL:
            error_reset(&e);
            j = json((const char *)0, &e);
            CHK(!j.valid());
            CHK(e.ok()); /* documented silent no-op */
            j.drop();
            return ok;
        case C_ZERO:       COPYT(""); break;
        case C_MALFORMED:  COPYT("{a:1}"); break;
        case C_TRUNCATED:  COPYT("{\"a\":[1,2"); break;
        case C_DEPTH_16384: build_nested(text, 16384u, &len); break;
        case C_DEPTH_16385: build_nested(text, 16385u, &len); break;
        case C_EXACT_FIT:  COPYT("[1,2,3]"); break;
        default:           COPYT("[\r\n1\r\n]"); break; /* C_CRLF */
        }
        text[len] = '\0'; /* json(text) uses strlen, unlike parse_budget */
        error_reset(&e);
        j = json(text, &e);
        if (cas == C_DEPTH_16385 || cas == C_ZERO || cas == C_MALFORMED ||
            cas == C_TRUNCATED) {
            CHK(!j.valid());
            CHK(e.failed_now());
        } else {
            CHK(j.valid());
            CHK(e.ok());
            CHK(j.read_size() == len);
            CHK(j.value_count() > 0u);
        }
        j.drop();
        CHK(text_clean());
        return ok;

    case A_JSON_VALID:
        error_reset(&e);
        if (cas == C_NULL) {
            j = json((const char *)0, &e);
            CHK(j.valid() == 0);
        } else if (cas == C_MALFORMED) {
            j = json("{a:1}", &e);
            CHK(j.valid() == 0);
        } else {
            j = json("[1]", &e);
            CHK(j.valid() == 1);
        }
        j.drop();
        return ok;

    case A_JSON_READ_SIZE:
    case A_JSON_VALUE_COUNT:
        error_reset(&e);
        if (cas == C_NULL) {
            j = json((const char *)0, &e);
            CHK(j.read_size() == 0u);
            CHK(j.value_count() == 0u);
        } else if (cas == C_MALFORMED) {
            j = json("{a:1}", &e);
            CHK(j.read_size() == 0u);
            CHK(j.value_count() == 0u);
        } else {
            j = json("[1,2,3]", &e);
            CHK(j.read_size() == 7u);
            CHK(j.value_count() >= 4u);
        }
        j.drop();
        return ok;

    case A_JSON_ROOT:
        error_reset(&e);
        if (cas == C_NULL) {
            j = json((const char *)0, &e);
            rv = j.root();
            CHK(rv.valid() == 0);
        } else if (cas == C_MALFORMED) {
            j = json("{a:1}", &e);
            rv = j.root();
            CHK(rv.valid() == 0);
        } else {
            j = json("[1,2]", &e);
            rv = j.root();
            CHK(rv.valid() == 1);
            CHK(rv.is_array() == 1);
            CHK(rv.size() == 2u);
        }
        j.drop();
        return ok;

    case A_JSON_DROP:
        error_reset(&e);
        j = json("[1,2]", &e);
        CHK(j.valid() == 1);
        j.drop();
        CHK(j.valid() == 0);
        if (cas == C_DOUBLE_CLOSE) {
            j.drop();
            CHK(j.valid() == 0);
            CHK(j.read_size() == 0u);
        }
        return ok;

    case A_JSON_PARSE_BUDGET:
        switch (cas) {
        case C_NULL:
            error_reset(&e);
            j = json_parse_budget((const char *)0, 4u, 16384u, &e);
            CHK(!j.valid());
            CHK(e.failed_now());
            return ok;
        case C_ZERO:
            error_reset(&e);
            j = json_parse_budget("[1]", 0u, 16384u, &e);
            CHK(!j.valid());
            CHK(e.failed_now());
            return ok;
        case C_ONE_OVER:
        case C_ALLOC_FAIL:
            error_reset(&e);
            j = json_parse_budget("[1]", SIZE_MAX, 16384u, &e);
            CHK(!j.valid());
            CHK(e.category() == STDLIB_CATEGORY_RANGE);
            return ok;
        case C_MALFORMED:  COPYT("{a:1}"); break;
        case C_TRUNCATED:  COPYT("[1,2"); break;
        case C_DEPTH_16384: build_nested(text, 16384u, &len); break;
        case C_DEPTH_16385: build_nested(text, 16385u, &len); break;
        case C_EXACT_FIT:  COPYT("[1,2,3]"); break;
        default:           COPYT("[\r\n1\r\n]"); break; /* C_CRLF */
        }
        error_reset(&e);
        j = json_parse_budget(text, len, 16384u, &e);
        if (cas == C_DEPTH_16385 || cas == C_MALFORMED || cas == C_TRUNCATED) {
            CHK(!j.valid());
            CHK(e.failed_now());
        } else {
            CHK(j.valid());
            CHK(e.ok());
            CHK(j.read_size() == len);
        }
        j.drop();
        CHK(text_clean());
        return ok;

    case A_JSON_PARSE_FILE:
        case_path(id, "js");
        path_copy(g_path, sizeof(g_path));
        g_fail_realloc = (cas == C_ALLOC_FAIL) ? 1 : 0;
        switch (cas) {
        case C_NULL:
            error_reset(&e);
            j = json_parse_file((const char *)0, &e);
            CHK(!j.valid());
            CHK(e.failed_now());
            g_fail_realloc = 0;
            return ok;
        case C_ZERO:       len = 0u; break;
        case C_MALFORMED:  COPYT("{a:1}"); break;
        case C_TRUNCATED:  COPYT("{\"a\":[1,2"); break;
        case C_DEPTH_16384: build_nested(text, 16384u, &len); break;
        case C_DEPTH_16385: build_nested(text, 16385u, &len); break;
        case C_EXACT_FIT:  COPYT("[1,2,3]"); break;
        case C_CRLF:       COPYT("[\r\n1\r\n]"); break;
        case C_ALLOC_FAIL: COPYT("[1,2,3]"); break;
        default:           COPYT("{\"a\":1,\"b\":[1,2,3]}"); break;
        }
        if (!write_file(g_path, text, len)) { g_fail_realloc = 0; return 0; }
        CHK(text_clean());
        error_reset(&e);
        j = json_parse_file(g_path, &e);
        if (cas == C_ALLOC_FAIL) {
            CHK(!j.valid());
            CHK(e.failed_now());
            CHK(e.category() == STDLIB_CATEGORY_MEMORY);
        } else if (cas == C_DEPTH_16385 || cas == C_ZERO || cas == C_MALFORMED ||
                   cas == C_TRUNCATED) {
            CHK(!j.valid());
            CHK(e.failed_now());
        } else {
            CHK(j.valid());
            CHK(e.ok());
        }
        j.drop();
        g_fail_realloc = 0;
        (void)unlink(g_path);
        return ok;

    case A_JSON_VIEW_OF:
        {
            yyjson_doc *doc;
            yyjson_read_err info;
            error_reset(&e);
            if (cas == C_NULL) {
                v = json_view_of((const yyjson_val *)0);
                CHK(v.valid() == 0);
                return ok;
            }
            doc = yyjson_read_opts("[1]", 3u, YYJSON_READ_NOFLAG, (yyjson_alc *)0, &info);
            if (doc == (yyjson_doc *)0) return 0;
            v = json_view_of(yyjson_doc_get_root(doc));
            CHK(v.valid() == 1);
            CHK(v.is_array() == 1);
            yyjson_doc_free(doc);
            (void)info;
        }
        return ok;

    case A_JSON_CLOSE_STREAM:
        error_reset(&e);
        if (cas == C_NULL) {
            json_close_stream((FILE *)0);
        } else {
            case_path(id, "cs");
            path_copy(g_path, sizeof(g_path));
            if (!write_file(g_path, "x", 1u)) return 0;
            fp = fopen(g_path, "rb");
            if (fp == (FILE *)0) return 0;
            json_close_stream(fp);
            (void)unlink(g_path);
        }
        return ok;

    case A_JSON_FREE:
        error_reset(&e);
        if (cas == C_NULL) {
            json_free((char *)0);
        } else {
            j = json("[1]", &e);
            rv = j.root();
            out = rv.write(&olen);
            CHK(out != (char *)0);
            if (out != (char *)0) json_free(out);
            j.drop();
        }
        return ok;

    /* ---- json_view methods --------------------------------------------- */
    case A_JSON_VIEW_VALID:
        error_reset(&e);
        if (cas == C_NULL) {
            v = json_view_of((const yyjson_val *)0);
            CHK(v.valid() == 0);
        } else {
            j = json("[1]", &e);
            v = j.root();
            CHK(v.valid() == 1);
            j.drop();
        }
        return ok;

    case A_JSON_VIEW_IS_OBJECT:
    case A_JSON_VIEW_IS_ARRAY:
    case A_JSON_VIEW_IS_NUMBER:
    case A_JSON_VIEW_IS_STRING:
    case A_JSON_VIEW_IS_BOOL:
        error_reset(&e);
        if (cas == C_NULL) {
            v = json_view_of((const yyjson_val *)0);
            CHK(v.is_object() == 0);
            CHK(v.is_array() == 0);
            CHK(v.is_number() == 0);
            CHK(v.is_string() == 0);
            CHK(v.is_bool() == 0);
            return ok;
        }
        j = json(JSON_DOC, &e);
        rv = j.root();
        if (api == A_JSON_VIEW_IS_OBJECT) {
            CHK(rv.is_object() == 1);
            CHK(rv.is_array() == 0);
        } else if (api == A_JSON_VIEW_IS_ARRAY) {
            v = rv.member("arr");
            CHK(v.is_array() == 1);
            CHK(v.is_object() == 0);
        } else if (api == A_JSON_VIEW_IS_NUMBER) {
            v = rv.member("n");
            CHK(v.is_number() == 1);
            CHK(v.is_string() == 0);
        } else if (api == A_JSON_VIEW_IS_STRING) {
            v = rv.member("s");
            CHK(v.is_string() == 1);
            CHK(v.is_bool() == 0);
        } else {
            v = rv.member("b");
            CHK(v.is_bool() == 1);
            CHK(v.is_number() == 0);
        }
        if (cas == C_MALFORMED) {
            /* wrong-type probes must be false */
            v = rv.member("s");
            CHK(v.is_number() == 0);
            CHK(v.is_bool() == 0);
        }
        j.drop();
        return ok;

    case A_JSON_VIEW_GET_INT:
        error_reset(&e);
        if (cas == C_NULL) {
            j = json("{\"i\":7}", &e);
            rv = j.root();
            v = rv.member("i");
            CHK(v.get_int((int *)0, &e) == STDLIB_FAIL);
            CHK(e.failed_now());
        } else if (cas == C_INVALID_HANDLE) {
            v = json_view_of((const yyjson_val *)0);
            CHK(v.get_int(&ival, &e) == STDLIB_FAIL);
        } else if (cas == C_ONE_OVER) {
            j = json("{\"i\":18446744073709551615}", &e);
            rv = j.root();
            v = rv.member("i");
            CHK(v.get_int(&ival, &e) == STDLIB_FAIL);
            CHK(e.category() == STDLIB_CATEGORY_RANGE);
        } else if (cas == C_MALFORMED) {
            j = json("{\"i\":1.5}", &e);
            rv = j.root();
            v = rv.member("i");
            CHK(v.get_int(&ival, &e) == STDLIB_FAIL);
        } else {
            j = json(cas == C_ZERO ? "{\"i\":0}" : "{\"i\":-42}", &e);
            rv = j.root();
            v = rv.member("i");
            CHK(v.get_int(&ival, &e) == STDLIB_OK);
            CHK(ival == (cas == C_ZERO ? 0 : -42));
        }
        if (j.valid()) j.drop();
        CHK(all_a5(g_nguard1, sizeof(g_nguard1)));
        CHK(all_a5(g_nguard2, sizeof(g_nguard2)));
        return ok;

    case A_JSON_VIEW_GET_DOUBLE:
        error_reset(&e);
        if (cas == C_NULL) {
            j = json("{\"n\":1.5}", &e);
            rv = j.root(); v = rv.member("n");
            CHK(v.get_double((double *)0, &e) == STDLIB_FAIL);
        } else if (cas == C_MALFORMED) {
            j = json("{\"n\":\"x\"}", &e);
            rv = j.root(); v = rv.member("n");
            CHK(v.get_double(&dval, &e) == STDLIB_FAIL);
        } else {
            j = json("{\"n\":1.5}", &e);
            rv = j.root(); v = rv.member("n");
            CHK(v.get_double(&dval, &e) == STDLIB_OK);
            CHK(dval == 1.5);
        }
        j.drop();
        return ok;

    case A_JSON_VIEW_GET_BOOL:
        error_reset(&e);
        if (cas == C_NULL) {
            j = json("{\"b\":true}", &e);
            rv = j.root(); v = rv.member("b");
            CHK(v.get_bool((int *)0, &e) == STDLIB_FAIL);
        } else if (cas == C_MALFORMED) {
            j = json("{\"b\":1}", &e);
            rv = j.root(); v = rv.member("b");
            CHK(v.get_bool(&ival, &e) == STDLIB_FAIL);
        } else {
            j = json("{\"b\":true}", &e);
            rv = j.root(); v = rv.member("b");
            CHK(v.get_bool(&ival, &e) == STDLIB_OK);
            CHK(ival == 1);
        }
        j.drop();
        return ok;

    case A_JSON_VIEW_GET_STRING:
        error_reset(&e);
        if (cas == C_NULL) {
            j = json("{\"s\":\"hi\"}", &e);
            rv = j.root(); v = rv.member("s");
            CHK(v.get_string((const char **)0, &e) == STDLIB_FAIL);
        } else if (cas == C_MALFORMED) {
            j = json("{\"s\":1}", &e);
            rv = j.root(); v = rv.member("s");
            CHK(v.get_string(&sval, &e) == STDLIB_FAIL);
        } else {
            j = json("{\"s\":\"hi\"}", &e);
            rv = j.root(); v = rv.member("s");
            CHK(v.get_string(&sval, &e) == STDLIB_OK);
            CHK(sval != (const char *)0);
            CHK(strcmp(sval, "hi") == 0);
        }
        j.drop();
        return ok;

    case A_JSON_VIEW_SIZE:
        error_reset(&e);
        if (cas == C_NULL) {
            v = json_view_of((const yyjson_val *)0);
            CHK(v.size() == 0u);
            return ok;
        }
        j = json(JSON_DOC, &e);
        rv = j.root();
        if (cas == C_ZERO) {
            v = rv.member("n");
            CHK(v.size() == 0u);
        } else {
            CHK(rv.size() == 4u);
            v = rv.member("arr");
            CHK(v.size() == 3u);
        }
        j.drop();
        return ok;

    case A_JSON_VIEW_AT:
        error_reset(&e);
        j = json("{\"arr\":[10,20,30]}", &e);
        v = j.root();
        rv = v.member("arr");
        if (cas == C_ONE_OVER) {
            v = rv.at(SIZE_MAX);
            CHK(v.valid() == 0);
        } else if (cas == C_INVALID_HANDLE) {
            rv = j.root();
            v = rv.at(0u);
            CHK(v.valid() == 0);
        } else {
            v = rv.at(cas == C_ZERO ? 0u : 2u);
            CHK(v.valid() == 1);
            CHK(v.get_int(&ival, &e) == STDLIB_OK);
            CHK(ival == (cas == C_ZERO ? 10 : 30));
        }
        j.drop();
        return ok;

    case A_JSON_VIEW_MEMBER:
        error_reset(&e);
        j = json(JSON_DOC, &e);
        rv = j.root();
        if (cas == C_NULL) {
            v = rv.member((const char *)0);
            CHK(v.valid() == 0);
        } else if (cas == C_INVALID_HANDLE) {
            v = rv.member("arr");
            v = v.member("nope");
            CHK(v.valid() == 0);
        } else {
            v = rv.member("n");
            CHK(v.valid() == 1);
            CHK(v.is_number() == 1);
        }
        j.drop();
        return ok;

    case A_JSON_VIEW_WRITE:
        error_reset(&e);
        if (cas == C_NULL) {
            v = json_view_of((const yyjson_val *)0);
            CHK(v.write(&olen) == (char *)0);
            return ok;
        }
        j = json("{\"a\":1}", &e);
        rv = j.root();
        out = rv.write(&olen);
        CHK(out != (char *)0);
        if (out != (char *)0) {
            CHK(olen > 0u);
            CHK(strstr(out, "\"a\"") != (char *)0);
            json_free(out);
        }
        j.drop();
        return ok;

    /* ---- json_builder methods ------------------------------------------ */
    case A_JSON_BUILDER_CTOR:
        jb = json_builder();
        CHK(jb.valid() == 1);
        jb.drop();
        return ok;

    case A_JSON_BUILDER_VALID:
        jb = json_builder();
        CHK(jb.valid() == 1);
        if (cas == C_DOUBLE_CLOSE) {
            jb.drop();
            CHK(jb.valid() == 0);
            jb.drop();
            CHK(jb.valid() == 0);
        } else {
            jb.drop();
        }
        return ok;

    case A_JSON_BUILDER_ADD_INT:
        error_reset(&e);
        jb = json_builder();
        if (cas == C_NULL) {
            CHK(jb.add_int((const char *)0, 1, &e) == STDLIB_FAIL);
            CHK(e.failed_now());
        } else if (cas == C_INVALID_HANDLE) {
            jb.drop();
            CHK(jb.add_int("i", 1, &e) == STDLIB_FAIL);
        } else {
            CHK(jb.add_int("i", cas == C_ZERO ? 0 : -7, &e) == STDLIB_OK);
            CHK(e.ok());
            out = jb.write(&olen);
            CHK(out != (char *)0);
            if (out != (char *)0) {
                CHK(strstr(out, "\"i\"") != (char *)0);
                json_free(out);
            }
        }
        jb.drop();
        return ok;

    case A_JSON_BUILDER_ADD_DOUBLE:
        error_reset(&e);
        jb = json_builder();
        if (cas == C_NULL) {
            CHK(jb.add_double((const char *)0, 1.5, &e) == STDLIB_FAIL);
        } else if (cas == C_MALFORMED) {
            CHK(jb.add_double("nan", (double)0.0 / (double)0.0, &e) == STDLIB_FAIL);
            CHK(e.category() == STDLIB_CATEGORY_RANGE);
            CHK(jb.add_double("inf", (double)1.0 / (double)0.0, &e) == STDLIB_FAIL);
        } else if (cas == C_INVALID_HANDLE) {
            jb.drop();
            CHK(jb.add_double("d", 1.5, &e) == STDLIB_FAIL);
        } else {
            CHK(jb.add_double("d", cas == C_ZERO ? 0.0 : 1.5, &e) == STDLIB_OK);
            CHK(e.ok());
        }
        jb.drop();
        return ok;

    case A_JSON_BUILDER_ADD_BOOL:
        error_reset(&e);
        jb = json_builder();
        if (cas == C_NULL) {
            CHK(jb.add_bool((const char *)0, 1, &e) == STDLIB_FAIL);
        } else if (cas == C_INVALID_HANDLE) {
            jb.drop();
            CHK(jb.add_bool("b", 1, &e) == STDLIB_FAIL);
        } else {
            CHK(jb.add_bool("b", cas == C_ZERO ? 0 : 1, &e) == STDLIB_OK);
            CHK(e.ok());
        }
        jb.drop();
        return ok;

    case A_JSON_BUILDER_ADD_STRING:
        error_reset(&e);
        jb = json_builder();
        if (cas == C_NULL) {
            CHK(jb.add_string((const char *)0, "v", &e) == STDLIB_FAIL);
            CHK(jb.add_string("k", (const char *)0, &e) == STDLIB_FAIL);
        } else if (cas == C_INVALID_HANDLE) {
            jb.drop();
            CHK(jb.add_string("k", "v", &e) == STDLIB_FAIL);
        } else {
            CHK(jb.add_string("k", cas == C_ZERO ? "" : "value", &e) == STDLIB_OK);
            CHK(e.ok());
        }
        jb.drop();
        return ok;

    case A_JSON_BUILDER_WRITE:
        error_reset(&e);
        jb = json_builder();
        if (cas == C_DOUBLE_CLOSE) {
            jb.drop();
            CHK(jb.write(&olen) == (char *)0);
        } else {
            CHK(jb.add_int("i", 1, &e) == STDLIB_OK);
            out = jb.write(&olen);
            CHK(out != (char *)0);
            CHK(olen > 0u);
            if (out != (char *)0) json_free(out);
        }
        jb.drop();
        return ok;

    default: /* A_JSON_BUILDER_DROP */
        jb = json_builder();
        CHK(jb.valid() == 1);
        jb.drop();
        CHK(jb.valid() == 0);
        if (cas == C_DOUBLE_CLOSE) {
            jb.drop();
            CHK(jb.valid() == 0);
        }
        return ok;
    }
}

/* ---- dispatch ---------------------------------------------------------- */
static int run_case(int api, int cas, int id) {
    if (api < 7) return case_io(api, cas, id);
    if (api < 20) return case_file(api, cas, id);
    if (api < 28) return case_fs(api, cas, id);
    return case_json(api, cas, id);
}

/* ---- parent / classifier ----------------------------------------------- */
struct case_result {
    int api;
    int cas;
    int outcome; /* 0 PASS, 1 FAIL */
};

static int selfcheck(void) {
    struct case_result r;
    int status = 0, passed = 0;
    r.api = 0; r.cas = 0; r.outcome = 0;
    if (r.outcome == 0) passed++;
    r.outcome = 1;
    if (r.outcome == 1) passed++;
    printf("SELFCHECK %d 2\n", passed);
    return passed == 2 ? 0 : 1;
}

int main(int argc, char **argv) {
    const char *sc = getenv("EDGE100_SELFCHECK");
    const char *logpath = (argc > 1) ? argv[1] : "tests/edge100/io/out/cases.log";
    int logfd;
    int counts[2] = {0, 0};
    int total = 0, passed = 0, failed = 0;
    int api, cas, id = 0, fds[2], status, line_len;
    pid_t pid;
    ssize_t got;
    struct case_result result;
    int outcome;

    if (sc != (const char *)0 && strcmp(sc, "1") == 0) return selfcheck();

    /* Raw fd logging: forked children must not inherit a buffered FILE* whose
     * contents could be duplicated when they flush on exit. */
    logfd = open(logpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (logfd < 0) {
        fprintf(stderr, "EDGE100 io: cannot open %s\n", logpath);
        printf("EDGE100 io %d 0 0 0\n", A_COUNT);
        return 2;
    }
    line_len = snprintf(g_line, sizeof(g_line), "# EDGE100 io apis=%d cases=%d\n",
                        A_COUNT, 0);
    if (line_len > 0) (void)write(logfd, g_line, (size_t)line_len);
    (void)mkdir(TMPDIR, 0700);

    for (api = 0; api < A_COUNT; api++) {
        for (cas = 0; cas < C_COUNT; cas++) {
            if ((API_CASES[api] & CS(cas)) == 0u) continue;
            if (pipe(fds) != 0) { fprintf(stderr, "pipe failed\n"); break; }
            fflush(stdout);
            pid = fork();
            if (pid < 0) {
                fprintf(stderr, "fork failed\n");
                (void)close(fds[0]);
                (void)close(fds[1]);
                break;
            }
            if (pid == 0) {
                struct case_result child;
                (void)close(fds[0]);
                (void)alarm(ALARM_SEC);
                (void)freopen("/dev/null", "w", stdout);
                g_fail_realloc = 0;
                g_fail_tmpfile = 0;
                child.api = api;
                child.cas = cas;
                child.outcome = run_case(api, cas, id) ? 0 : 1;
                (void)write(fds[1], &child, sizeof(child));
                (void)close(fds[1]);
                _exit(0);
            }
            (void)close(fds[1]);
            got = read(fds[0], &result, sizeof(result));
            (void)close(fds[0]);
            if (waitpid(pid, &status, 0) != pid ||
                got != (ssize_t)sizeof(result) || result.api != api ||
                result.cas != cas || (result.outcome != 0 && result.outcome != 1)) {
                outcome = 1;
                line_len = snprintf(g_line, sizeof(g_line), "%s %s CRASH\n",
                                    API_NAMES[api], CASE_NAMES[cas]);
            } else {
                outcome = result.outcome;
                line_len = snprintf(g_line, sizeof(g_line), "%s %s %s\n",
                                    API_NAMES[api], CASE_NAMES[cas],
                                    outcome == 0 ? "PASS" : "FAIL");
            }
            if (line_len > 0) (void)write(logfd, g_line, (size_t)line_len);
            counts[outcome]++;
            total++;
            id++;
        }
    }
    passed = counts[0];
    failed = counts[1];
    line_len = snprintf(g_line, sizeof(g_line),
                        "# SUMMARY apis=%d cases=%d passed=%d failed=%d\n",
                        A_COUNT, total, passed, failed);
    if (line_len > 0) (void)write(logfd, g_line, (size_t)line_len);
    (void)close(logfd);
    printf("EDGE100 io %d %d %d %d\n", A_COUNT, total, passed, failed);
    return 0;
}
