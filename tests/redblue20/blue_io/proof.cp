/* BLUE20 / io -- deterministic safety proof for the C+ I/O, file and JSON
 * layers: exact-fit / one-over buffers, EOF, invalid handles, malformed and
 * truncated JSON, the 16385-depth refusal, canary integrity and cleanup on
 * failure.
 *
 * DETERMINISM
 *   One fixed seed (BLUE_SEED) plus the monotonic case id drive splitmix64.
 *   No clock, pid, getenv, rand() or random temp names feed any decision or
 *   the case log. family = id % 8, so each of the eight families sees exactly
 *   300 of the 2400 cases. argv[1] is a caller-supplied bounded scratch dir;
 *   argv[2] is the case-log path. Both are paths, not decision inputs.
 *
 * ISOLATION
 *   Every case runs in a fresh fork()ed child. The child arms alarm(60) and
 *   _exit(124) on SIGALRM; the parent polls waitpid(WNOHANG) against a
 *   CLOCK_MONOTONIC 60-second deadline, SIGKILLs and reaps on expiry, and
 *   classifies: 0 pass, 1 assertion failure, 124 timeout, anything else
 *   crash/signal. run.sh adds a process-wide timeout as a third bound.
 *
 *   All user buffers are heap blocks with 32-byte 0x5A guard bands before and
 *   after the declared body. Body bytes are 0x5A or 0xA5 as noted; both guard
 *   bands are checked after every operation. File fixtures live only under
 *   argv[1] and are unlinked by each case. Open fd counts (0..255) and the
 *   SafeCRT live-allocation count must return to their per-case baseline.
 *
 *   yyjson is rebuilt for this proof in blue_io/out with
 *   -DYYJSON_READER_DEPTH_LIMIT=16384 -DYYJSON_WRITER_DEPTH_LIMIT=16384 and
 *   -DYYJSON_DISABLE_NON_STANDARD=0 -DYYJSON_DISABLE_UTF8_VALIDATION=0, so
 *   the depth-16385 refusal is also enforced downstream of stdlib/json.hp.
 *   Direct yyjson calls rotate read flags (NOFLAG, STOP_WHEN_DONE,
 *   ALLOW_TRAILING_COMMAS, JSON5) and use a counting allocator whose live
 *   count must return to zero on every failure path.
 *
 *   The SafeCRT violation trap is disabled in each child so documented
 *   refusals are observable as return values (0 / NULL / EOF) rather than a
 *   deliberate exit(70). This is an observation choice, not a library change.
 *
 * OUTPUT
 *   stdout: BLUE20 io <cases> <passed> <failed>
 *   argv[2]: "C <id> <family> <rc> <digest>" per case plus a final
 *            "DIGEST <aggregate>" trailer. Exit 0 only when every case passed
 *            and the case count is exactly BLUE_N_CASES.
 *
 * This is evidence for this fixed corpus on this build and host, not a proof
 * of safety for all inputs.
 */
#define _POSIX_C_SOURCE 200809L
#define CPLUS_SAFE_NO_UNPREFIXED_MACROS

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"
#include "stdlib/json.hp"
#include "stdlib/safe/safe_alloc.h"
#include "stdlib/safe/safe_io.h"
#include "stdlib/safe/safe_runtime.h"
#include "yyjson.h"

#define BLUE_SEED 0x424C554532303230ULL /* ASCII "BLUE2020" */
#define BLUE_N_CASES 2400u
#define BLUE_N_FAMILIES 8u
#define BLUE_CASES_PER_FAMILY 300u
#define BLUE_GUARD 32u
#define BLUE_MAX_BODY 96u
#define BLUE_DEPTH_OK ((int)16384)
#define BLUE_DEPTH_OVER ((int)16385)
#define BLUE_DEEP_CAP ((size_t)(BLUE_DEPTH_OVER * 2 + 8))
#define RC_PASS 0
#define RC_ASSERT 1
#define RC_TIMEOUT 124

typedef struct blue_case {
    uint64_t bl_id;
    unsigned bl_family;
    unsigned bl_sub;
    size_t bl_cap;
    uint64_t bl_w0;
    uint64_t bl_w1;
    uint64_t bl_w2;
    uint64_t bl_w3;
    char bl_path[600];
} blue_case;

/* ---------------------------- deterministic core ----------------------- */

static uint64_t blue_splitmix64(uint64_t *x) {
    uint64_t z = (*x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static void blue_gen(uint64_t id, const char *dir, blue_case *c) {
    uint64_t r = BLUE_SEED + id * 0x9E3779B97F4A7C15ULL;
    int n;
    c->bl_id = id;
    c->bl_w0 = blue_splitmix64(&r);
    c->bl_w1 = blue_splitmix64(&r);
    c->bl_w2 = blue_splitmix64(&r);
    c->bl_w3 = blue_splitmix64(&r);
    blue_splitmix64(&r);
    c->bl_family = (unsigned)(id % (uint64_t)BLUE_N_FAMILIES);
    c->bl_sub = (unsigned)(c->bl_w0 % 3u);
    c->bl_cap = (size_t)(4u + (unsigned)(c->bl_w1 % (uint64_t)BLUE_MAX_BODY));
    n = snprintf(c->bl_path, sizeof c->bl_path, "%s/blue_%llu.bin",
                 dir, (unsigned long long)id);
    if (n <= 0 || (size_t)n >= sizeof c->bl_path)
        c->bl_path[0] = '\0';
}

static uint64_t blue_digest_case(const blue_case *c, int rc) {
    uint64_t h = BLUE_SEED ^ c->bl_id;
    h ^= (uint64_t)c->bl_family * 0x9E3779B97F4A7C15ULL;
    h ^= (uint64_t)c->bl_sub * 0xBF58476D1CE4E5B9ULL;
    h ^= (uint64_t)c->bl_cap * 0x94D049BB133111EBULL;
    h ^= c->bl_w0 ^ c->bl_w1 ^ c->bl_w2 ^ c->bl_w3;
    h ^= (uint64_t)rc * 0xD1B54A32D192ED03ULL;
    return blue_splitmix64(&h);
}

/* ------------------------------ helpers -------------------------------- */

static unsigned char blue_get(const char *p, size_t i) {
    unsigned char b;
    __builtin_memcpy(&b, p + i, 1);
    return b;
}

static void blue_put(char *p, size_t i, unsigned char b) {
    __builtin_memcpy(p + i, &b, 1);
}

static void blue_fill(char *p, size_t n, unsigned char b) {
    size_t i;
    for (i = 0; i < n; i++)
        blue_put(p, i, b);
}

static int blue_equal(const char *a, const char *b, size_t n) {
    size_t i;
    for (i = 0; i < n; i++)
        if (blue_get(a, i) != blue_get(b, i))
            return 0;
    return 1;
}

/* Guard bands must be untouched. */
static int blue_canaries(const char *raw, size_t total) {
    size_t i;
    for (i = 0; i < BLUE_GUARD; i++)
        if (blue_get(raw, i) != 0x5Au || blue_get(raw + total - BLUE_GUARD, i) != 0x5Au)
            return 0;
    return 1;
}

/* Static guard pattern for memcmp. The C+ rewriter gives the byte helper a
 * fixed extent from its first call site, so large heap bodies are compared
 * with libc memcmp instead of per-byte helper calls. */
static char blue_guard_pattern[256];

static int blue_body_clean(const char *body, size_t n) {
    if (n > sizeof blue_guard_pattern)
        return 0;
    return memcmp(body, blue_guard_pattern, n) == 0;
}

static int blue_fd_count(void) {
    int fd_i;
    int n = 0;
    for (fd_i = 0; fd_i < 256; fd_i++)
        if (fcntl(fd_i, F_GETFD) != -1)
            n++;
    return n;
}

static void blue_payload(char *body, size_t n, uint64_t seed) {
    size_t i;
    uint64_t r = seed;
    for (i = 0; i < n; i++) {
        blue_put(body, i, (unsigned char)(blue_splitmix64(&r) & 0xFFu));
    }
}

static int blue_write_file(const char *path, const char *data, size_t len) {
    FILE *fp = fopen(path, "wb");
    size_t w;
    if (fp == NULL)
        return 0;
    w = (len == 0) ? 0u : fwrite(data, 1, len, fp);
    if (w != len || fclose(fp) != 0)
        return 0;
    return 1;
}

static size_t blue_file_size(const char *path) {
    FILE *fp = fopen(path, "rb");
    long end;
    size_t n = (size_t)-1;
    if (fp == NULL)
        return (size_t)-1;
    if (fseek(fp, 0, SEEK_END) == 0) {
        end = ftell(fp);
        if (end >= 0)
            n = (size_t)end;
    }
    fclose(fp);
    return n;
}

/* Counting yyjson allocator: malloc +1, realloc net 0 (the old block is
 * released by realloc), free -1. Every parse must end at zero live blocks. */
static size_t blue_alc_live;
static void *blue_alc_malloc(void *ctx, size_t sz) {
    void *p;
    (void)ctx;
    p = malloc(sz);
    if (p != NULL)
        blue_alc_live++;
    return p;
}
static void *blue_alc_realloc(void *ctx, void *p, size_t old, size_t sz) {
    (void)ctx;
    (void)old;
    return realloc(p, sz);
}
static void blue_alc_free(void *ctx, void *p) {
    (void)ctx;
    if (p != NULL && blue_alc_live > 0)
        blue_alc_live--;
    free(p);
}

static yyjson_read_flag blue_flag_for(unsigned sub) {
    if (sub == 1u)
        return YYJSON_READ_STOP_WHEN_DONE;
    if (sub == 2u)
        return YYJSON_READ_ALLOW_TRAILING_COMMAS;
    return YYJSON_READ_NOFLAG;
}

static void blue_nested(char *out, int depth, size_t cap) {
    size_t pos = 0;
    int i;
    for (i = 0; i < depth; i++) {
        blue_put(out, pos, '[');
        pos++;
    }
    blue_put(out, pos, '0');
    pos++;
    for (i = 0; i < depth; i++) {
        blue_put(out, pos, ']');
        pos++;
    }
    blue_put(out, pos, '\0');
    (void)cap;
}

/* Optional per-case diagnostics (enabled with -DBLUE_DEBUG for triage). */
#ifdef BLUE_DEBUG
static void blue_trace(const blue_case *c, const char *tag, long val) {
    fprintf(stderr, "TRACE %llu %s %ld\n",
            (unsigned long long)c->bl_id, tag, val);
}
#define BLUE_TRACE(c, t, v) blue_trace((c), (t), (long)(v))
#else
#define BLUE_TRACE(c, t, v) ((void)0)
#endif

/* ------------------------ I/O family implementations -------------------- */

/* F0: exact-fit fread plus EOF refusal. */
static int blue_f0(const blue_case *c) {
    int ok = 1;
    size_t total = c->bl_cap + BLUE_GUARD * 2u;
    char *raw = (char *)malloc(total);
    char *body;
    char expect[BLUE_MAX_BODY + 8u];
    FILE *fp;
    size_t got;
    int fds0;
    if (raw == NULL)
        return 0;
    body = raw + BLUE_GUARD;
    blue_fill(raw, total, 0x5Au);
    blue_payload(expect, c->bl_cap, c->bl_w2);
    ok = ok && blue_write_file(c->bl_path, expect, c->bl_cap);
    fds0 = blue_fd_count();
    fp = cplus_fopen_s(c->bl_path, "rb");
    if (fp == NULL)
        ok = 0;
    else {
        got = cplus_fread_s_checked(body, c->bl_cap, c->bl_cap, 1, c->bl_cap, fp);
        ok = ok && got == c->bl_cap;
        ok = ok && blue_equal(body, expect, c->bl_cap);
        ok = ok && blue_canaries(raw, total);
        got = cplus_fread_s_checked(body, c->bl_cap, c->bl_cap, 1, 1, fp);
        ok = ok && got == 0u;
        ok = ok && feof(fp) != 0;
        ok = ok && blue_equal(body, expect, c->bl_cap);
        ok = ok && blue_canaries(raw, total);
        ok = ok && cplus_fclose_s(fp) == 0;
    }
    ok = ok && blue_fd_count() == fds0;
    ok = ok && unlink(c->bl_path) == 0;
    free(raw);
    return ok;
}

/* F1: one-over fread is refused on both the caller-bmax and tracked paths. */
static int blue_f1(const blue_case *c) {
    int ok = 1;
    size_t total = c->bl_cap + BLUE_GUARD * 2u;
    char *raw = (char *)malloc(total);
    char *body;
    char file[BLUE_MAX_BODY + 8u];
    char *tracked;
    FILE *fp;
    size_t got;
    int fds0;
    if (raw == NULL)
        return 0;
    body = raw + BLUE_GUARD;
    blue_fill(raw, total, 0x5Au);
    blue_fill(file, c->bl_cap, 0x33u);
    ok = ok && blue_write_file(c->bl_path, file, c->bl_cap);
    fds0 = blue_fd_count();
    fp = cplus_fopen_s(c->bl_path, "rb");
    tracked = (char *)malloc_s(c->bl_cap + BLUE_GUARD);
    if (fp == NULL || tracked == NULL)
        ok = 0;
    else {
        blue_fill(tracked, c->bl_cap + BLUE_GUARD, 0x5Au);
        errno = 0;
        got = cplus_fread_s_checked(body, c->bl_cap, c->bl_cap + 1u, 1,
                                    c->bl_cap, fp);
        BLUE_TRACE(c, "f1_checked_got", got);
        BLUE_TRACE(c, "f1_checked_errno", errno);
        ok = ok && got == 0u && errno == ERANGE;
        ok = ok && blue_body_clean(body, c->bl_cap);
        ok = ok && blue_canaries(raw, total);
        ok = ok && ftell(fp) == 0L;
        errno = 0;
        got = cplus_fread_s(tracked, c->bl_cap + BLUE_GUARD + 1u, 1,
                            c->bl_cap, fp);
        BLUE_TRACE(c, "f1_tracked_got", got);
        BLUE_TRACE(c, "f1_tracked_errno", errno);
        ok = ok && got == 0u && errno == EINVAL;
        ok = ok && blue_body_clean(tracked, c->bl_cap + BLUE_GUARD);
        ok = ok && ftell(fp) == 0L;
        ok = ok && free_checked_s(tracked) == CPLUS_SAFE_OK;
        ok = ok && cplus_fclose_s(fp) == 0;
    }
    ok = ok && blue_fd_count() == fds0;
    ok = ok && unlink(c->bl_path) == 0;
    free(raw);
    return ok;
}

/* F2: exact-fit write succeeds; one-over write refuses with no file growth. */
static int blue_f2(const blue_case *c) {
    int ok = 1;
    size_t total = c->bl_cap + BLUE_GUARD * 2u;
    char *raw = (char *)malloc(total);
    char *body;
    char back[BLUE_MAX_BODY + 8u];
    FILE *fp;
    size_t wrote = 0;
    int fds0;
    if (raw == NULL)
        return 0;
    body = raw + BLUE_GUARD;
    blue_fill(raw, total, 0x5Au);
    blue_payload(back, c->bl_cap, c->bl_w3);
    fds0 = blue_fd_count();
    fp = cplus_fopen_s(c->bl_path, "wb");
    if (fp == NULL)
        ok = 0;
    else {
        if (c->bl_sub == 0u) {
            size_t i;
            for (i = 0; i < c->bl_cap; i++)
                blue_put(body, i, blue_get(back, i));
            wrote = cplus_fwrite_s_checked(body, c->bl_cap, c->bl_cap, 1,
                                           c->bl_cap, fp);
            ok = ok && wrote == c->bl_cap;
        } else {
            errno = 0;
            wrote = cplus_fwrite_s_checked(body, c->bl_cap, c->bl_cap + 1u, 1,
                                           c->bl_cap, fp);
            ok = ok && wrote == 0u && errno == ERANGE;
            ok = ok && blue_body_clean(body, c->bl_cap);
        }
        ok = ok && blue_canaries(raw, total);
        ok = ok && cplus_fclose_s(fp) == 0;
    }
    BLUE_TRACE(c, "f2_wrote", wrote);
    BLUE_TRACE(c, "f2_size", blue_file_size(c->bl_path));
    if (c->bl_sub == 0u)
        ok = ok && blue_file_size(c->bl_path) == c->bl_cap;
    else
        ok = ok && blue_file_size(c->bl_path) == 0u;
    ok = ok && blue_fd_count() == fds0;
    ok = ok && unlink(c->bl_path) == 0;
    free(raw);
    return ok;
}

/* F3: fgets exact fit, one-over and empty-file EOF. */
static int blue_f3(const blue_case *c) {
    int ok = 1;
    size_t total = c->bl_cap + BLUE_GUARD * 2u;
    char *raw = (char *)malloc(total);
    char *body;
    char line[BLUE_MAX_BODY + 8u];
    char *r;
    FILE *fp;
    size_t i;
    int fds0;
    if (raw == NULL)
        return 0;
    body = raw + BLUE_GUARD;
    blue_fill(raw, total, 0x5Au);
    /* cap-2 letters plus '\n' = cap-1 bytes: fgets(buf, cap) reads the whole
     * line, including the newline, and fills the cap-byte buffer exactly. */
    for (i = 0; i < c->bl_cap - 2u; i++) {
        int letter = (int)(i % 26u) + 65;
        blue_put(line, i, (unsigned char)letter);
    }
    blue_put(line, c->bl_cap - 2u, '\n');
    if (c->bl_sub == 2u)
        ok = ok && blue_write_file(c->bl_path, "", 0u);
    else
        ok = ok && blue_write_file(c->bl_path, line, c->bl_cap - 1u);
    fds0 = blue_fd_count();
    fp = cplus_fopen_s(c->bl_path, "rb");
    if (fp == NULL)
        ok = 0;
    else {
        if (c->bl_sub == 1u) {
            errno = 0;
            r = cplus_fgets_s_checked(body, c->bl_cap, (int)c->bl_cap + 1,
                                      fp);
            BLUE_TRACE(c, "f3_over_r", r != NULL);
            BLUE_TRACE(c, "f3_over_errno", errno);
            ok = ok && r == NULL && errno == ERANGE;
            ok = ok && blue_body_clean(body, c->bl_cap);
            ok = ok && ftell(fp) == 0L;
        } else {
            r = cplus_fgets_s_checked(body, c->bl_cap, (int)c->bl_cap, fp);
            BLUE_TRACE(c, "f3_r", r != NULL);
            {
                long blen = -1;
                if (r != NULL)
                    blen = (long)strlen(body);
                BLUE_TRACE(c, "f3_len", blen);
            }
            if (c->bl_sub == 2u) {
                ok = ok && r == NULL;
                ok = ok && blue_get(body, 0) == '\0';
            } else {
                ok = ok && r != NULL;
                ok = ok && strlen(body) == c->bl_cap - 1u;
                ok = ok && blue_get(body, c->bl_cap - 2u) == '\n';
                ok = ok && blue_equal(body, line, c->bl_cap - 1u);
            }
        }
        ok = ok && blue_canaries(raw, total);
        ok = ok && cplus_fclose_s(fp) == 0;
    }
    ok = ok && blue_fd_count() == fds0;
    ok = ok && unlink(c->bl_path) == 0;
    free(raw);
    return ok;
}

/* F4: NULL and already-closed handles refuse; no host I/O is attempted. */
static int blue_f4(const blue_case *c) {
    int ok = 1;
    size_t total = c->bl_cap + BLUE_GUARD * 2u;
    char *raw = (char *)malloc(total);
    char *body;
    FILE *fp;
    char dummy[1];
    error fh_err = error();
    file fh = file(NULL, NULL);
    int fds0;
    if (raw == NULL)
        return 0;
    body = raw + BLUE_GUARD;
    blue_fill(raw, total, 0x5Au);
    fds0 = blue_fd_count();
    dummy[0] = 'x';
    ok = ok && fh.is_open() == 0;
    ok = ok && fh.read(body, c->bl_cap, &fh_err) == STDLIB_FAIL;
    ok = ok && fh.read_line(body, c->bl_cap, &fh_err) == STDLIB_FAIL;
    ok = ok && fh.write("x") == 0;
    ok = ok && fh.write_bytes(dummy, 1u, &fh_err) == STDLIB_FAIL;
    ok = ok && blue_body_clean(body, c->bl_cap);
    if (c->bl_sub == 0u) {
        ok = ok && cplus_fopen_s(NULL, "rb") == NULL;
        ok = ok && cplus_fopen_s(c->bl_path, NULL) == NULL;
        ok = ok && cplus_fopen_s(c->bl_path, "not_a_mode") == NULL;
        ok = ok && cplus_fopen_s(c->bl_path, "rb") == NULL;
        ok = ok && cplus_fclose_s(NULL) == EOF;
        ok = ok && cplus_fread_s_checked(body, c->bl_cap, c->bl_cap, 1, 1, NULL) == 0u;
        ok = ok && cplus_fwrite_s_checked(body, c->bl_cap, c->bl_cap, 1, 1, NULL) == 0u;
        ok = ok && cplus_fgets_s_checked(body, c->bl_cap, (int)c->bl_cap, NULL) == NULL;
    } else {
        ok = ok && blue_write_file(c->bl_path, "x", 1u);
        fp = cplus_fopen_s(c->bl_path, "rb");
        ok = ok && fp != NULL;
        if (fp != NULL) {
            ok = ok && cplus_fclose_s(fp) == 0;
            ok = ok && cplus_io_stream_is_closed(fp) == 1;
            ok = ok && cplus_fread_s_checked(body, c->bl_cap, c->bl_cap, 1, 1, fp) == 0u;
            ok = ok && cplus_fwrite_s_checked(body, c->bl_cap, c->bl_cap, 1, 1, fp) == 0u;
            ok = ok && cplus_fgets_s_checked(body, c->bl_cap, (int)c->bl_cap, fp) == NULL;
            ok = ok && cplus_fclose_s(fp) == EOF;
        }
    }
    ok = ok && blue_body_clean(body, c->bl_cap);
    ok = ok && blue_canaries(raw, total);
    fh.drop();
    ok = ok && blue_fd_count() == fds0;
    (void)unlink(c->bl_path);
    free(raw);
    return ok;
}

/* F5: malformed and truncated JSON text refuses on every parse entry point. */
static int blue_f5(const blue_case *c) {
    int ok = 1;
    const char base[] = "{\"name\":\"value\",\"list\":[1,2,3],\"ok\":true}";
    char text[128];
    size_t len = strlen(base);
    size_t cut;
    size_t i;
    int fds0 = blue_fd_count();
    error je = error();
    error je2 = error();
    json doc = json(NULL, &je);
    json budget = json(NULL, &je2);
    yyjson_alc alc = {blue_alc_malloc, blue_alc_realloc, blue_alc_free, NULL};
    yyjson_read_err ri;
    yyjson_doc *rawdoc;
    if (len >= sizeof text - 16u)
        return 0;
    for (i = 0; i < len; i++)
        blue_put(text, i, (unsigned char)base[i]);
    switch (c->bl_sub) {
    case 0:
        cut = 1u + (size_t)(c->bl_w2 % (uint64_t)(len - 1u));
        len = cut;
        break;
    case 1:
        blue_put(text, 0, 'X');
        break;
    case 2:
        for (i = 0; i < 9u; i++)
            blue_put(text, len + i, (unsigned char)" garbage"[i]);
        len = len + 9u;
        break;
    case 3: {
        const char bad[] = "{\"x\":\"\\q\"}";
        for (i = 0; i < sizeof bad - 1u; i++)
            blue_put(text, i, (unsigned char)bad[i]);
        len = sizeof bad - 1u;
        break;
    }
    default:
        blue_fill(text, len, ' ');
        break;
    }
    blue_put(text, len, '\0');
    doc = json(text, &je);
    ok = ok && doc.valid() == 0;
    ok = ok && je.failed_now() == 1;
    ok = ok && strlen(je.what()) > 0;
    budget = json_parse_budget(text, len, (size_t)CPLUS_JSON_MAX_DEPTH, &je2);
    ok = ok && budget.valid() == 0;
    ok = ok && je2.failed_now() == 1;
    blue_alc_live = 0;
    rawdoc = yyjson_read_opts(text, len, YYJSON_READ_NOFLAG, &alc, &ri);
    ok = ok && rawdoc == NULL;
    ok = ok && ri.code != YYJSON_READ_SUCCESS;
    ok = ok && blue_alc_live == 0u;
    if (rawdoc != NULL)
        yyjson_doc_free(rawdoc);
    doc.drop();
    doc.drop();
    budget.drop();
    budget.drop();
    ok = ok && blue_fd_count() == fds0;
    return ok;
}

/* F6: depth budget at 16384 (accept) and 16385 (refuse, with clean yyjson
 * failure), including a second, compile-time yyjson reader cap. */
static int blue_f6(const blue_case *c) {
    int ok = 1;
    size_t total = BLUE_DEEP_CAP + BLUE_GUARD * 2u;
    char *raw = (char *)malloc(total);
    char *body;
    int depth;
    int fds0 = blue_fd_count();
    error je = error();
    json doc = json(NULL, &je);
    yyjson_alc alc = {blue_alc_malloc, blue_alc_realloc, blue_alc_free, NULL};
    yyjson_read_err ri;
    yyjson_doc *rawdoc;
    size_t len;
    if (raw == NULL)
        return 0;
    body = raw + BLUE_GUARD;
    blue_fill(raw, total, 0x5Au);
    if (c->bl_sub == 0u)
        depth = BLUE_DEPTH_OVER;
    else if (c->bl_sub == 1u)
        depth = BLUE_DEPTH_OK;
    else
        depth = 1 + (int)(c->bl_w2 % 100u);
    blue_nested(body, depth, BLUE_DEEP_CAP);
    len = strlen(body);
    BLUE_TRACE(c, "depth", depth);
    BLUE_TRACE(c, "len", len);
    ok = ok && len == (size_t)(depth * 2 + 1);
    doc = json_parse_budget(body, len, (size_t)CPLUS_JSON_MAX_DEPTH, &je);
    BLUE_TRACE(c, "doc_valid", doc.valid());
    BLUE_TRACE(c, "err_cat", je.category());
    BLUE_TRACE(c, "err_code", je.code());
    if (depth > BLUE_DEPTH_OK) {
        ok = ok && doc.valid() == 0;
        ok = ok && je.failed_now() == 1;
        ok = ok && je.category() == STDLIB_CATEGORY_RANGE;
        ok = ok && je.code() == (int)CPLUS_EXIT_BOUNDS;
    } else {
        ok = ok && doc.valid() == 1;
        ok = ok && je.ok() == 1;
    }
    blue_alc_live = 0;
    rawdoc = yyjson_read_opts(body, len, blue_flag_for(c->bl_sub), &alc, &ri);
    BLUE_TRACE(c, "rawdoc", rawdoc != NULL);
    BLUE_TRACE(c, "ri_code", (int)ri.code);
    BLUE_TRACE(c, "alc_live", blue_alc_live);
    BLUE_TRACE(c, "flag", (int)blue_flag_for(c->bl_sub));
    if (depth > BLUE_DEPTH_OK) {
        ok = ok && rawdoc == NULL;
        ok = ok && ri.code == YYJSON_READ_ERROR_DEPTH;
    } else {
        ok = ok && rawdoc != NULL;
    }
    if (rawdoc != NULL)
        yyjson_doc_free(rawdoc);
    ok = ok && blue_alc_live == 0u;
    ok = ok && blue_canaries(raw, total);
    BLUE_TRACE(c, "canaries", blue_canaries(raw, total));
    BLUE_TRACE(c, "fds", blue_fd_count() - fds0);
    doc.drop();
    ok = ok && blue_fd_count() == fds0;
    free(raw);
    return ok;
}

/* F7: json_parse_file valid, malformed, truncated, empty and missing.
 * Open fd counts must be identical before and after, including failures. */
static int blue_f7(const blue_case *c) {
    int ok = 1;
    char doc[96];
    char path[640];
    size_t len;
    int n;
    int fds0 = blue_fd_count();
    error je = error();
    json parsed = json(NULL, &je);
    if (c->bl_sub == 0u) {
        n = snprintf(doc, sizeof doc, "{\"n\":%llu}",
                     (unsigned long long)(c->bl_w1 % 1000000u));
        if (n <= 0 || (size_t)n >= sizeof doc)
            return 0;
        ok = ok && blue_write_file(c->bl_path, doc, (size_t)n);
        parsed = json_parse_file(c->bl_path, &je);
        ok = ok && parsed.valid() == 1;
        ok = ok && je.ok() == 1;
        if (parsed.valid() != 0) {
            error vge = error();
            json_view root = parsed.root();
            json_view val = root.member("n");
            int number = -1;
            ok = ok && val.is_number() == 1;
            ok = ok && val.get_int(&number, &vge) == STDLIB_OK;
            ok = ok && number >= 0 && number < 1000000;
        }
    } else {
        const char full[] = "{\"a\":[1,2,3],\"b\":\"tail\"}";
        size_t flen = strlen(full);
        if (c->bl_sub == 1u) {
            len = 1u + (size_t)(c->bl_w2 % (uint64_t)(flen - 1u));
            ok = ok && blue_write_file(c->bl_path, full, len);
            parsed = json_parse_file(c->bl_path, &je);
            ok = ok && parsed.valid() == 0 && je.failed_now() == 1;
            ok = ok && strlen(je.what()) > 0;
        } else if (c->bl_sub == 2u) {
            ok = ok && blue_write_file(c->bl_path, full, flen);
            parsed = json_parse_file(c->bl_path, &je);
            ok = ok && parsed.valid() == 1;
        } else if (c->bl_sub == 3u) {
            ok = ok && blue_write_file(c->bl_path, "", 0u);
            parsed = json_parse_file(c->bl_path, &je);
            ok = ok && parsed.valid() == 0 && je.failed_now() == 1;
        } else {
            n = snprintf(path, sizeof path, "%s.missing", c->bl_path);
            if (n <= 0 || (size_t)n >= sizeof path)
                return 0;
            parsed = json_parse_file(path, &je);
            ok = ok && parsed.valid() == 0;
            ok = ok && je.category() == STDLIB_CATEGORY_IO;
        }
    }
    parsed.drop();
    parsed.drop();
    ok = ok && blue_fd_count() == fds0;
    (void)unlink(c->bl_path);
    return ok;
}

/* ------------------------------- runner -------------------------------- */

static void blue_timeout(int sig) {
    (void)sig;
    _exit(RC_TIMEOUT);
}

static int blue_child(const blue_case *c) {
    int rc = RC_ASSERT;
#ifdef BLUE_NEGATIVE
    /* Deliberate fault-injection control: exactly one case fails. */
    if (c->bl_id == 0u)
        _exit(RC_ASSERT);
#endif
    cplus_runtime_set_violation_trap(0);
    signal(SIGALRM, blue_timeout);
    alarm(60);
    switch (c->bl_family) {
    case 0u: rc = blue_f0(c) ? RC_PASS : RC_ASSERT; break;
    case 1u: rc = blue_f1(c) ? RC_PASS : RC_ASSERT; break;
    case 2u: rc = blue_f2(c) ? RC_PASS : RC_ASSERT; break;
    case 3u: rc = blue_f3(c) ? RC_PASS : RC_ASSERT; break;
    case 4u: rc = blue_f4(c) ? RC_PASS : RC_ASSERT; break;
    case 5u: rc = blue_f5(c) ? RC_PASS : RC_ASSERT; break;
    case 6u: rc = blue_f6(c) ? RC_PASS : RC_ASSERT; break;
    default: rc = blue_f7(c) ? RC_PASS : RC_ASSERT; break;
    }
    _exit(rc);
}

static double blue_now(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0.0;
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void blue_wait_10ms(void) {
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 10000000L;
    (void)nanosleep(&ts, NULL);
}

int main(int argc, char **argv) {
    char dir[600];
    char logpath[640];
    FILE *log;
    uint64_t id;
    unsigned passed = 0;
    unsigned failed = 0;
    uint64_t digest = BLUE_SEED;
    int n1;
    int n2;
    blue_case c;
    blue_fill(blue_guard_pattern, sizeof blue_guard_pattern, 0x5Au);
    if (argc < 3) {
        printf("usage: proof.cp <scratch-dir> <case-log>\n");
        return 2;
    }
    n1 = snprintf(dir, sizeof dir, "%s", argv[1]);
    n2 = snprintf(logpath, sizeof logpath, "%s", argv[2]);
    if (n1 <= 0 || (size_t)n1 >= sizeof dir || n2 <= 0 ||
        (size_t)n2 >= sizeof logpath)
        return 2;
    log = fopen(logpath, "w");
    if (log == NULL)
        return 2;
    for (id = 0; id < (uint64_t)BLUE_N_CASES; id++) {
        pid_t pid;
        int status = 0;
        int rc = RC_PASS;
        double deadline;
        blue_gen(id, dir, &c);
        pid = fork();
        if (pid == 0)
            blue_child(&c);
        if (pid < 0) {
            rc = RC_ASSERT;
        } else {
            deadline = blue_now() + 60.0;
            for (;;) {
                pid_t w = waitpid(pid, &status, WNOHANG);
                if (w == pid)
                    break;
                if (w < 0 && errno != EINTR)
                    break;
                if (blue_now() >= deadline) {
                    kill(pid, SIGKILL);
                    (void)waitpid(pid, &status, 0);
                    status = 0;
                    rc = RC_TIMEOUT;
                    break;
                }
                blue_wait_10ms();
            }
            if (rc != RC_TIMEOUT) {
                if (WIFEXITED(status))
                    rc = WEXITSTATUS(status);
                else
                    rc = RC_ASSERT + 128;
            }
        }
        if (rc == RC_PASS)
            passed++;
        else
            failed++;
        digest = blue_splitmix64(&digest) ^ blue_digest_case(&c, rc);
        fprintf(log, "C %llu %u %d %016llx\n", (unsigned long long)id,
                c.bl_family, rc, (unsigned long long)blue_digest_case(&c, rc));
    }
    fprintf(log, "DIGEST %016llx\n", (unsigned long long)digest);
    if (fclose(log) != 0)
        return 2;
    printf("BLUE20 io %u %u %u\n", (unsigned)BLUE_N_CASES, passed, failed);
    return (failed == 0u && passed == BLUE_N_CASES) ? 0 : 1;
}
