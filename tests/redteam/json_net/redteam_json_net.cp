/* RED-TEAM agent 5 -- adversarial harness for stdlib/json.hp, net.hp, http.hp.
 *
 * 3,000 deterministic cases (fixed SplitMix64 seed 0x5EED5005A5A50005,
 * per-case seed = SEED ^ (id * GOLDEN)). Every case runs in a forked child
 * with an alarm() watchdog; the parent classifies signals as CRASH/HANG and
 * inspects 32-byte canaries around each work buffer for CORRUPT.
 *
 * All network work is OFFLINE: libcurl's in-process URL parser, a loopback
 * TCP connection to a closed port (127.0.0.1:1), AF_UNIX socketpairs carrying
 * synthetic HTTP byte streams, and borrowed/invalid descriptors. No DNS,
 * proxy, external host or listening socket is used.
 *
 * Build (from repo root):
 *   ./build/c+ build tests/redteam/json_net/redteam_json_net.cp \
 *     --default --sanitize=none -lcurl \
 *     -o tests/redteam/json_net/results/redteam_json_net
 * Run:  timeout 600 <bin> <log-path>
 *
 * Output per case: "S <id> <api> <name> <result> <sentinel> <ok> <promise>"
 * Final line: "REDTEAM_PART json_net <cases> <refused> <crashes> <corrupt> <unexpected_ok>"
 */
#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/http.hp"
#include "stdlib/json.hp"
#include "stdlib/net.hp"

#define N_CASES 3000
#define SEED 0x5EED5005A5A50005ULL
#define GOLDEN 0x9E3779B97F4A7C15ULL
#define GUARD 32
#define JCAP ((size_t)96u * 1024u + 16u)
#define SUF ((size_t)8192u)
#define ALARM_SEC 10

static char gj[GUARD + JCAP + GUARD];
static char gs[GUARD + SUF + GUARD];
static char g_src[GUARD + SUF + GUARD];
static char gr[GUARD + SUF + GUARD];
static char gout[256];

static FILE *g_log = (FILE *)0;
static unsigned long long g_hash = 1469598103934665603ULL;
static int g_cases, g_refused, g_crashes, g_corrupt, g_unexpected;
static int g_sent_ok, g_mismatch;

typedef struct Rng { unsigned long long s; } Rng;
static void rinit(Rng *r, unsigned long long seed) { r->s = seed; }
static unsigned int rnd(Rng *r) {
    unsigned long long x = r->s;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    r->s = x;
    return (unsigned int)((x * 2685821657736338717ULL) >> 32);
}
static unsigned long long case_seed(int id) {
    return SEED ^ ((unsigned long long)(id + 1) * GOLDEN);
}

static void fill(char *p, size_t n) { memset(p, 0xA5, n); }
static int all_a5(const char *p, size_t n) {
    size_t i; for (i = 0; i < n; i++) if (p[i] != (char)0xA5) return 0; return 1;
}
static int guard_ok(const char *p) {
    return all_a5(p, GUARD) && all_a5(p + GUARD + JCAP, GUARD);
}
static int guard_s_ok(const char *p) {
    return all_a5(p, GUARD) && all_a5(p + GUARD + SUF, GUARD);
}
static void hash_line(const char *s) {
    while (*s) { unsigned char c = (unsigned char)*s; g_hash ^= c; g_hash *= 1099511628211ULL; s++; }
    g_hash ^= '\n'; g_hash *= 1099511628211ULL;
}
static void report(int fd, int id, const char *api, const char *name,
                   const char *result, int sent, int ok, int promise) {
    int n = snprintf(gout, sizeof(gout), "S %05d %s %s %s %d %d %d\n",
                     id, api, name, result, sent ? 1 : 0, ok ? 1 : 0, promise ? 1 : 0);
    if (n > 0) {
        size_t k = (size_t)n;
        if (k >= sizeof(gout)) k = sizeof(gout) - 1;
        (void)write(fd, gout, k);
    }
}

/* ---------------------------------------------------------------- JSON -- */
static void f_json_null_zero(int id, int fd, Rng *r) {
    error e = error();
    char name[32];
    int mode = (int)(rnd(r) % 6u);
    int ok = 0, sent;
    error_reset(&e);
    if (mode == 0) {
        ok = json_input_within_budget((const char *)0, 16u, 16384u, &e) == 0 && e.failed_now();
    } else if (mode == 1) {
        ok = json_input_within_budget("1", 0u, 16384u, &e) == 0 && e.failed_now();
    } else if (mode == 2) {
        ok = json_input_within_budget("1", SIZE_MAX, 16384u, &e) == 0 && e.failed_now();
    } else if (mode == 3) {
        ok = json_input_within_budget("1", SIZE_MAX - 8u, 16384u, &e) == 0 && e.failed_now();
    } else if (mode == 4) {
        json j = json((const char *)0, &e);
        ok = !j.valid(); j.drop();
    } else {
        /* zero max_depth with a single container must be refused */
        ok = json_input_within_budget("[]", 2u, 0u, &e) == 0 && e.failed_now();
    }
    sent = guard_ok(gj);
    snprintf(name, sizeof(name), "jnull_%04d", id);
    report(fd, id, "json_input_within_budget", name, "REFUSED", sent, ok, 1);
}

static void f_json_depth(int id, int fd, Rng *r) {
    error e = error();
    size_t depth, i, len = 0;
    int mode = (int)(rnd(r) % 5u);
    int within, ok, sent;
    char name[32], result[32];

    if (mode == 0) depth = 16384u;
    else if (mode == 1) depth = 16385u;
    else if (mode == 2) depth = 20000u + (rnd(r) % 1000u);
    else if (mode == 3) depth = (size_t)(rnd(r) % 20000u);
    else depth = 4096u;

    if (len + 2u * depth + 1u < JCAP) {
        for (i = 0; i < depth; i++) gj[GUARD + len++] = '[';
        for (i = 0; i < depth; i++) gj[GUARD + len++] = ']';
        gj[GUARD + len] = '\0';
    }
    error_reset(&e);
    within = json_input_within_budget(gj + GUARD, len, 16384u, &e);
    ok = (depth <= 16384u) ? (within == 1 && e.ok())
                           : (within == 0 && e.failed_now());
    sent = guard_ok(gj);
    snprintf(result, sizeof(result), "%s", (within == 0) ? "REFUSED" : "DEFINED_OK");
    snprintf(name, sizeof(name), "jdepth_%04d", id);
    report(fd, id, "json_depth_budget", name, result, sent, ok, (int)(depth > 16384u));
}

static void f_json_escaped_budget(int id, int fd, Rng *r) {
    /* Brackets hidden inside strings must not be counted; escaped quotes
     * must not be mistaken for the end of a string. */
    error e = error();
    static const char *cases[] = {
        "[\"[[[[[\"]",
        "[\"a\\\\\"[[[\"]",
        "\"[[[[[[[[[[[[[[[\"",
        "{\"key\":\"value with [ and { and ] and }\"}",
        "[\"\\\\\\\"[[[[\"]"
    };
    const char *s = cases[rnd(r) % (sizeof(cases)/sizeof(cases[0]))];
    size_t len = strlen(s);
    int within, ok, sent;
    char name[32];
    memcpy(gj + GUARD, s, len + 1u);
    error_reset(&e);
    within = json_input_within_budget(gj + GUARD, len, 16384u, &e);
    ok = (within == 1 && e.ok());
    sent = guard_ok(gj);
    snprintf(name, sizeof(name), "jesc_%04d", id);
    report(fd, id, "json_escaped_budget", name, "DEFINED_OK", sent, ok, 0);
}

static void f_json_parse_budget(int id, int fd, Rng *r) {
    error e = error();
    size_t len, d, k;
    int mode = (int)(rnd(r) % 8u);
    int valid, ok, sent, promise = 0;
    char name[32], result[32];

    len = 0;
    if (mode == 0) {
        /* valid small document */
        const char *s = "{\"a\":1,\"b\":[true,false,null],\"c\":\"x\"}";
        len = strlen(s); memcpy(gj + GUARD, s, len + 1u);
    } else if (mode == 1) {
        /* truncated */
        const char *s = "{\"a\":[1,2,3";
        len = strlen(s); memcpy(gj + GUARD, s, len + 1u); promise = 1;
    } else if (mode == 2) {
        /* malformed UTF-8 inside a string */
        len = 0; memcpy(gj + GUARD, "[\"", 2u); len = 2u;
        gj[GUARD + len++] = (char)0xC3;
        gj[GUARD + len++] = (char)0x28;
        memcpy(gj + GUARD + len, "\"]", 3u); len += 2u;
        gj[GUARD + len] = '\0'; promise = 1;
    } else if (mode == 3) {
        /* lone continuation byte */
        memcpy(gj + GUARD, "[\"", 2u); len = 2u;
        gj[GUARD + len++] = (char)0x80;
        memcpy(gj + GUARD + len, "\"]", 3u); len += 2u;
        gj[GUARD + len] = '\0'; promise = 1;
    } else if (mode == 4) {
        /* overlong UTF-8 sequence */
        memcpy(gj + GUARD, "[\"", 2u); len = 2u;
        gj[GUARD + len++] = (char)0xE0;
        gj[GUARD + len++] = (char)0x80;
        gj[GUARD + len++] = (char)0x80;
        memcpy(gj + GUARD + len, "\"]", 3u); len += 2u;
        gj[GUARD + len] = '\0'; promise = 1;
    } else if (mode == 5) {
        /* over budget depth with default cap */
        d = 17000u;
        for (k = 0; k < d; k++) gj[GUARD + len++] = '[';
        for (k = 0; k < d; k++) gj[GUARD + len++] = ']';
        gj[GUARD + len] = '\0'; promise = 1;
    } else if (mode == 6) {
        /* huge explicit length, tiny buffer: budget must refuse before read */
        error_reset(&e);
        ok = json_input_within_budget("1", SIZE_MAX, 16384u, &e) == 0 && e.failed_now();
        sent = guard_ok(gj);
        snprintf(name, sizeof(name), "jpb_%04d", id);
        report(fd, id, "json_parse_budget", name, "REFUSED", sent, ok, 1);
        return;
    } else {
        /* random bytes, NUL-terminated inside object */
        len = 1u + (rnd(r) % 4096u);
        gj[GUARD] = '[';
        for (k = 1; k < len; k++) gj[GUARD + k] = (char)(1u + (rnd(r) % 255u));
        gj[GUARD + len++] = ']';
        gj[GUARD + len] = '\0'; promise = 1;
    }

    error_reset(&e);
    {
        json j = json_parse_budget(gj + GUARD, len, 16384u, &e);
        valid = j.valid();
        if (valid) {
            json_view rv = j.root();
            (void)rv.size();
            j.drop();
        }
    }
    ok = (mode == 0) ? (valid == 1 && e.ok())
                     : (valid == 0 && e.failed_now());
    sent = guard_ok(gj);
    snprintf(result, sizeof(result), "%s", valid ? "DEFINED_OK" : "REFUSED");
    if (valid && promise) {
        snprintf(result, sizeof(result), "UNEXPECTED_OK");
        ok = 0;
    }
    snprintf(name, sizeof(name), "jpb_%04d", id);
    report(fd, id, "json_parse_budget", name, result, sent, ok, promise);
}

static void f_json_view_builder(int id, int fd, Rng *r) {
    error e = error();
    const char *s = "{\"arr\":[1,2,3],\"n\":1.5,\"b\":true,\"s\":\"hi\"}";
    size_t len = strlen(s);
    int ok = 1, sent, val;
    double dv;
    const char *strv;
    char *out; size_t olen = 0;
    json_builder jb = json_builder();
    char name[32];

    memcpy(gj + GUARD, s, len + 1u);
    error_reset(&e);
    {
        json j = json_parse_budget(gj + GUARD, len, 16384u, &e);
        json_view root = j.root();
        json_view arr = root.member("arr");
        json_view big = arr.at(SIZE_MAX);
        json_view far = arr.at(999999u);
        json_view nonull = root.member((const char *)0);
        json_view vn = root.member("n");
        json_view vb = root.member("b");
        json_view vs = root.member("s");
        json_view vi = root.member("arr");
        if (!arr.is_array()) ok = 0;
        (void)arr.size();
        if (big.valid()) ok = 0;                       /* huge index -> invalid */
        if (far.valid()) ok = 0;
        if (nonull.valid()) ok = 0;
        if (vn.get_double(&dv, &e) != STDLIB_OK) ok = 0;
        if (vb.get_bool(&val, &e) != STDLIB_OK || val != 1) ok = 0;
        if (vs.get_string(&strv, &e) != STDLIB_OK) ok = 0;
        if (vi.get_int(&val, &e) == STDLIB_OK) ok = 0;
        out = root.write(&olen);
        if (out != (char *)0) json_free(out);
        j.drop();
    }
    if (!jb.valid()) ok = 0;
    error_reset(&e);
    if (jb.add_int("i", 42, &e) != STDLIB_OK) ok = 0;
    if (jb.add_string("k", "v", &e) != STDLIB_OK) ok = 0;
    if (jb.add_double("nan", (double)0.0 / (double)0.0, &e) != STDLIB_FAIL) ok = 0;
    if (jb.add_double("inf", (double)1.0 / (double)0.0, &e) != STDLIB_FAIL) ok = 0;
    if (jb.add_string("null", (const char *)0, &e) != STDLIB_FAIL) ok = 0;
    error_reset(&e);
    out = jb.write(&olen);
    if (out != (char *)0) json_free(out);
    jb.drop();
    sent = guard_ok(gj);
    snprintf(name, sizeof(name), "jvb_%04d", id);
    report(fd, id, "json_view_builder", name, "DEFINED_OK", sent, ok, 0);
}

/* -------------------------------------------------------------- HTTP -- */
static void f_sink_bounds(int id, int fd, Rng *r) {
    http_sink s;
    size_t cap, used, bytes, k, ret, exp_ret, room, exp_used, i;
    int mode = (int)(rnd(r) % 8u);
    int ovf, ok = 1, sent, promise = 0;
    char name[32], result[32];

    fill(gs, sizeof(gs));
    fill(g_src, sizeof(g_src));
    for (i = 0; i < (int)sizeof(g_src); i++) g_src[i] = 'B';

    cap = 1u + (size_t)(rnd(r) % (unsigned int)SUF);
    used = (size_t)(rnd(r) % (unsigned int)cap);
    bytes = 1u + (size_t)(rnd(r) % 512u);
    if (mode == 0) bytes = cap;            /* exact fill -> reserved NUL => refuse */
    else if (mode == 1) bytes = cap + 1u;  /* one over */
    else if (mode == 2) { cap = 0; used = 0; }
    else if (mode == 3) bytes = 4097u;
    else if (mode == 4) { used = 0; bytes = cap - 1u; } /* exactly size-1 accepted */

    /* Clamp bytes so the model's write window stays inside g_src. */
    if (bytes > SUF) bytes = SUF;
    s.buf = gs + GUARD; s.size = cap; s.used = used; s.overflow = 0;
    ret = http_sink_write(g_src + GUARD, 1u, bytes, (void *)&s);

    /* independent model */
    exp_ret = 0; exp_used = used; room = 0; ovf = 0;
    if (bytes != 0u && cap != 0u) {
        if (used >= cap || bytes > cap - used - 1u) {
            room = (cap > used + 1u) ? (cap - used - 1u) : 0u;
            exp_used = used + room; ovf = 1;
        } else { exp_ret = bytes; exp_used = used + bytes; }
    }
    if (ret != exp_ret || s.used != exp_used || s.overflow != ovf) ok = 0;
    /* full-buffer canaries + physical guard check (final NUL at index cap-1) */
    sent = guard_s_ok(gs);
    if (cap > 0 && gs[GUARD + cap - 1u] != '\0') sent = 0;
    for (k = exp_used; k < cap; k++)
        if (gs[GUARD + k] != (char)0xA5) sent = 0;
    promise = (mode == 0 || mode == 1 || mode == 3);
    snprintf(result, sizeof(result), "%s", ovf ? "REFUSED" : "DEFINED_OK");
    snprintf(name, sizeof(name), "sbnd_%04d", id);
    report(fd, id, "http_sink_write", name, result, sent, ok, promise);
}

static void f_sink_mul(int id, int fd, Rng *r) {
    http_sink s;
    size_t a, b, prod, got;
    int mulovf, ok, sent;
    char name[32];
    fill(gs, sizeof(gs));
    if ((rnd(r) % 3u) == 0u) { a = SIZE_MAX; b = 2u + (rnd(r) % 100u); }
    else if ((rnd(r) % 3u) == 1u) { a = (size_t)1 << 62; b = 4u; }
    else { a = 0u; b = rnd(r) % 1000u; }
    mulovf = __builtin_mul_overflow(a, b, &prod) ? 1 : 0;
    s.buf = gs + GUARD; s.size = SUF; s.used = 0; s.overflow = 0;
    got = http_sink_write(gs + GUARD, a, b, (void *)&s);
    ok = got == 0u && s.used == 0u && s.overflow == mulovf && guard_s_ok(gs);
    sent = guard_s_ok(gs);
    snprintf(name, sizeof(name), "smul_%04d", id);
    report(fd, id, "http_sink_write", name, mulovf ? "REFUSED" : "DEFINED_OK",
           sent, ok, mulovf);
}

static void f_sink_null(int id, int fd, Rng *r) {
    http_sink s;
    size_t got, used_before;
    int mode = (int)(rnd(r) % 5u);
    int ovf_before = (int)(rnd(r) & 1u);
    int ok, sent;
    char name[32];
    fill(gs, sizeof(gs));
    s.buf = (mode == 2) ? (char *)0 : (gs + GUARD);
    s.size = (mode == 3) ? 0u : SUF;
    s.used = (mode == 4) ? SUF : 0u;
    used_before = s.used;
    s.overflow = ovf_before;
    if (mode == 1)
        got = http_sink_write((char *)0, 1u, 8u, (void *)&s);
    else
        got = http_sink_write(gs + GUARD, 1u, 8u,
                              (mode == 0) ? (void *)0 : (void *)&s);
    /* mode 4 has used == size: the full branch sets overflow=1.
     * modes 0/1 return before touching overflow; mode 2 returns at buf==NULL;
     * mode 3 has size==0 and returns at the first branch. */
    ok = got == 0u && s.used == used_before &&
         ((mode == 4) ? (s.overflow == 1) : (s.overflow == ovf_before));
    sent = guard_s_ok(gs);
    snprintf(name, sizeof(name), "snull_%04d", id);
    report(fd, id, "http_sink_write", name, "REFUSED", sent, ok, 1);
}

/* Poisoned `used` near SIZE_MAX: documented FORTHCOMING FINDING reproducer.
 * The overflow branch computes room = size - used - 1 and used + 1, both of
 * which wrap, then memcpy(buf + used, ptr, room) writes BEFORE the buffer. */
static void f_sink_poison(int id, int fd, Rng *r) {
    http_sink s;
    char src[8];
    size_t got;
    int i, corrupt = 0, ok;
    char name[32];
    for (i = 0; i < (int)sizeof(src); i++) src[i] = 'B';
    fill(gs, sizeof(gs));
    s.buf = gs + GUARD; s.size = 64u; s.used = SIZE_MAX; s.overflow = 0;
    got = http_sink_write(src, 1u, 8u, (void *)&s);
    /* pre-guard byte immediately before buf is written OOB */
    if ((unsigned char)gs[GUARD - 1] != 0xA5u) corrupt = 1;
    for (i = 0; i < 32; i++)
        if ((unsigned char)gs[i] != 0xA5u) corrupt = 1;
    ok = (got == 0u && s.overflow == 1u);
    snprintf(name, sizeof(name), "spoison_%04d", id);
    report(fd, id, "http_sink_write", name,
           corrupt ? "CORRUPT" : "REFUSED", !corrupt, ok, 1);
}

static void f_headers(int id, int fd, Rng *r) {
    http_headers hh = http_headers();
    error e = error();
    size_t len = 0, k;
    int mode = (int)(rnd(r) % 8u);
    int rc, ok, sent, promise = 0;
    char name[32], result[32];

    fill(gs, sizeof(gs));
    error_reset(&e);
    if (mode == 0) {
        rc = hh.add((const char *)0, &e);
        ok = rc == STDLIB_FAIL && e.failed_now() && !hh.valid() &&
             hh.native() == (struct curl_slist *)0;
        promise = 1;
        snprintf(result, sizeof(result), "REFUSED");
    } else {
        if (mode == 1) {
            memcpy(gs + GUARD, "X: ok\r\nInjected: yes", 21u);
            len = 21u;
        } else if (mode == 2) {
            memcpy(gs + GUARD, "X: ok\rX: bad", 12u); len = 12u;
        } else if (mode == 3) {
            memcpy(gs + GUARD, "X: ok\nX: bad", 12u); len = 12u;
        } else if (mode == 4) {
            len = 1u + (rnd(r) % (unsigned int)(SUF - 64u));
            memcpy(gs + GUARD, "X-Long: ", 8u);
            for (k = 8u; k < len; k++) gs[GUARD + k] = 'H';
        } else if (mode == 5) {
            memcpy(gs + GUARD, ":", 1u); len = 1u;
        } else if (mode == 6) {
            memcpy(gs + GUARD, "X: \x01\x02\x03", 6u); len = 6u;
        } else {
            memcpy(gs + GUARD, "X-Ok: value", 11u); len = 11u;
        }
        gs[GUARD + len] = '\0';
        rc = hh.add(gs + GUARD, &e);
        ok = rc == STDLIB_OK && e.ok() && hh.valid() &&
             hh.native() != (struct curl_slist *)0 &&
             hh.native()->data != (char *)0 &&
             strcmp(hh.native()->data, gs + GUARD) == 0 &&
             hh.native()->next == (struct curl_slist *)0;
        /* CRLF / bare-CR / bare-LF are copied verbatim: documented finding */
        snprintf(result, sizeof(result),
                 (mode == 1 || mode == 2 || mode == 3) ? "DEFINED_COPY" : "DEFINED_OK");
    }
    sent = guard_s_ok(gs);
    hh.drop();
    snprintf(name, sizeof(name), "hdr_%04d", id);
    report(fd, id, "http_headers.add", name, result, sent, ok, promise);
}

static void f_url_offline(int id, int fd, Rng *r) {
    static const char *bad[] = {
        "", "://", "http://", "http:// ", " http://example.com/",
        "ht tp://example.com/", "http://exa mple.com/", "http://[::1",
        "http://[bad]/", "http://example.com:abc/",
        "http://example.com:99999/", "http://?x=1",
        "http://example.com/#frag\n", "http://\r\n", "mailto:user"
    };
    static const char segchars[] =
        "!#$%&'*+-.^_`|~0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    CURLU *uh; CURLUcode crc;
    size_t len = 0, k, seg, s;
    int mode = (int)(rnd(r) % 6u);
    int ok, sent;
    char name[32], result[32];

    fill(gs, sizeof(gs));
    if (mode == 0) {
        const char *t = bad[rnd(r) % (sizeof(bad)/sizeof(bad[0]))];
        len = strlen(t); memcpy(gs + GUARD, t, len + 1u);
    } else if (mode == 1) {
        memcpy(gs + GUARD, "://", 3u); len = 3u + (rnd(r) % 200u);
        for (k = 3u; k < len; k++) gs[GUARD + k] = (char)(1u + (rnd(r) % 255u));
        gs[GUARD + len] = '\0';
    } else if (mode == 2) {
        memcpy(gs + GUARD, "http://", 7u); len = 7u;
        seg = 1u + (rnd(r) % 6u);
        for (s = 0; s < seg; s++) {
            size_t sl = 1u + (rnd(r) % 40u);
            if (len + sl + 8u >= SUF - 32u) break;
            gs[GUARD + len++] = '/';
            for (k = 0; k < sl; k++) gs[GUARD + len + k] = segchars[rnd(r) % (sizeof(segchars)-1u)];
            len += sl;
        }
        memcpy(gs + GUARD + len, ":abc/", 6u); len += 5u;
        gs[GUARD + len] = '\0';
    } else if (mode == 3) {
        memcpy(gs + GUARD, "http://[", 8u); len = 8u + (rnd(r) % 120u);
        for (k = 8u; k < len; k++) gs[GUARD + k] = (char)(1u + (rnd(r) % 255u));
        gs[GUARD + len] = '\0';
    } else if (mode == 4) {
        static const char ctl[] = {'\r','\n','\t','\x7f','\x01'};
        memcpy(gs + GUARD, "http://example.com/", 19u); len = 19u + (rnd(r) % 64u);
        for (k = 19u; k < len; k++) gs[GUARD + k] = ctl[rnd(r) % sizeof(ctl)];
        gs[GUARD + len] = '\0';
    } else {
        /* valid offline URL */
        memcpy(gs + GUARD, "http://127.0.0.1:1/path?q=1", 27u);
        len = 27u; gs[GUARD + len] = '\0';
    }
    uh = curl_url();
    if (uh == (CURLU *)0) { report(fd, id, "curl_url_set", "alloc_fail", "ALLOC_FAIL", 0, 0, 0); return; }
    crc = curl_url_set(uh, CURLUPART_URL, gs + GUARD, 0u);
    curl_url_cleanup(uh);
    /* libcurl's own acceptance policy is not a C+ contract: report the
     * returned CURLUcode verbatim instead of predicting refusal. */
    ok = 1;
    sent = guard_s_ok(gs);
    snprintf(result, sizeof(result), "%s_E%d", (crc == CURLUE_OK) ? "OK" : "REFUSED", (int)crc);
    snprintf(name, sizeof(name), "url_%04d", id);
    report(fd, id, "curl_url_set", name, result, sent, ok, 0);
}

static void f_http_client(int id, int fd, Rng *r) {
    error e = error();
    http_client c = http_client(&e);
    int mode = (int)(rnd(r) % 7u);
    int rc, ok, sent;
    char name[32], result[32];
    const char *url = "http://127.0.0.1:1/offline";
    fill(gs, sizeof(gs));
    error_reset(&e);
    if (mode == 0) rc = c.get((const char *)0, gs + GUARD, 64u, &e);
    else if (mode == 1) rc = c.get(url, (char *)0, 64u, &e);
    else if (mode == 2) rc = c.get(url, gs + GUARD, 0u, &e);
    else if (mode == 3) rc = c.post(url, (const char *)0, gs + GUARD, 64u, &e);
    else if (mode == 4) rc = c.set_timeout(-1, &e);
    else if (mode == 5) rc = c.follow_redirects(1, &e);
    else rc = c.get(url, gs + GUARD, 64u, &e);
    ok = (mode == 5 || mode == 6) ? 1 : (rc == STDLIB_FAIL && e.failed_now());
    if (mode == 6) ok = (rc == STDLIB_FAIL && e.failed_now()); /* closed port */
    sent = guard_s_ok(gs);
    c.drop();
    snprintf(result, sizeof(result), "%s", (rc == STDLIB_FAIL) ? "REFUSED" : "DEFINED_OK");
    snprintf(name, sizeof(name), "hcli_%04d", id);
    report(fd, id, "http_client", name, result, sent, ok, (int)(mode != 5));
}

static void f_net_invalid(int id, int fd, Rng *r) {
    error e = error();
    int fdval, mode, expect, got, ok, sent;
    char name[32];
    const char *api;
    switch (rnd(r) % 5u) {
    case 0: fdval = -1; break;
    case 1: fdval = 0; break;
    case 2: fdval = 1; break;
    case 3: fdval = 2; break;
    default: fdval = 999999; break;
    }
    mode = (int)(rnd(r) % 4u);
    fill(gs, sizeof(gs));
    error_reset(&e);
    if (mode == 0) {
        connection cx = connection_borrow(fdval);
        size_t cap = (rnd(r) % 11u == 0u) ? 0u : 1u + (rnd(r) % (unsigned int)SUF);
        char *dst = (rnd(r) % 13u == 0u) ? (char *)0 : (gs + GUARD);
        expect = (fdval == 999999 && cap != 0u && dst != (char *)0)
                     ? STDLIB_CATEGORY_IO : STDLIB_CATEGORY_INVALID;
        got = cx.read(dst, cap, &e);
        ok = got == STDLIB_FAIL && e.failed_now() && e.category() == expect;
        api = "connection.read";
        sent = guard_s_ok(gs);
    } else if (mode == 1) {
        connection cw = connection_borrow(fdval);
        const char *src = (rnd(r) % 13u == 0u) ? (const char *)0 : gs;
        size_t len = (size_t)(rnd(r) % 128u);
        expect = (fdval == 999999 && src != (const char *)0)
                     ? STDLIB_CATEGORY_IO : STDLIB_CATEGORY_INVALID;
        got = cw.write(src, len, &e);
        ok = got == STDLIB_FAIL && e.failed_now() && e.category() == expect;
        api = "connection.write";
        sent = guard_s_ok(gs);
    } else if (mode == 2) {
        listener lp = (rnd(r) % 2u == 0u) ? listener(-1) : listener_borrow(fdval);
        got = lp.bound_port();
        ok = got == 0;
        api = "listener.bound_port";
        sent = guard_s_ok(gs);
    } else {
        listener la = (rnd(r) % 2u == 0u) ? listener(-1) : listener_borrow(fdval);
        connection acc = la.accept();
        got = acc.is_valid();
        ok = got == 0 && acc.descriptor() == -1;
        api = "listener.accept";
        sent = guard_s_ok(gs);
    }
    snprintf(name, sizeof(name), "net_%04d", id);
    report(fd, id, api, name, "REFUSED", sent, ok, 1);
}

static void f_utf8_stream(int id, int fd, Rng *r) {
    /* Feed truncated / non-ASCII HTTP byte streams through a bounded read. */
    static const unsigned char payloads[][24] = {
        {0x48,0x54,0x54,0x50,0x2f,0x31,0x2e,0x31,0x20,0x32,0x30,0x30,0x0d,0x0a,0x0d,0x0a,0,0,0,0,0,0,0,0},
        {0x47,0x45,0x54,0x20,0x2f,0x20,0x48,0x54,0x54,0x50,0x2f,0x31,0x2e,0x31,0x0d,0x0a,0,0,0,0,0,0,0,0},
        {0x80,0xC0,0xC3,0x28,0xE0,0x80,0x80,0xF0,0x80,0x80,0x80,0x0d,0x0a,0x0d,0x0a,0,0,0,0,0,0,0,0},
        {0xff,0xfe,0x00,0x01,0x7f,0x0d,0x0a,0x0d,0x0a,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
    };
    const unsigned char *p = payloads[rnd(r) % 4u];
    size_t plen = 16u, k;
    int sp[2], ok, sent;
    ssize_t got;
    ssize_t w;
    connection cx;
    error e = error();
    char name[32];
    fill(gr, sizeof(gr));
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0) {
        report(fd, id, "connection.read", "sockfail", "SOCKFAIL", 0, 0, 0); return;
    }
    w = write(sp[1], p, plen);
    ok = w == (ssize_t)plen;
    cx = connection_borrow(sp[0]);
    error_reset(&e);
    got = cx.read(gr + GUARD, 128u, &e);
    ok = ok && got > 0 && e.ok();
    sent = guard_s_ok(gr);
    for (k = (size_t)(got > 0 ? got : 0); k < SUF; k++)
        if (gr[GUARD + k] != (char)0xA5) sent = 0;
    close(sp[0]); close(sp[1]);
    snprintf(name, sizeof(name), "utf8_%04d", id);
    report(fd, id, "connection.read", name, "DEFINED_OK", sent, ok, 0);
}

/* ------------------------------------------------------------ dispatch -- */
static void dispatch(int id, int fd) {
    Rng r;
    rinit(&r, case_seed(id));
    if (id < 400) f_json_null_zero(id, fd, &r);
    else if (id < 800) f_json_depth(id, fd, &r);
    else if (id < 1100) f_json_escaped_budget(id, fd, &r);
    else if (id < 1500) f_json_parse_budget(id, fd, &r);
    else if (id < 1650) f_json_view_builder(id, fd, &r);
    else if (id < 1850) f_sink_bounds(id, fd, &r);
    else if (id < 1950) f_sink_mul(id, fd, &r);
    else if (id < 2050) f_sink_null(id, fd, &r);
    else if (id < 2150) f_sink_poison(id, fd, &r);
    else if (id < 2350) f_headers(id, fd, &r);
    else if (id < 2600) f_url_offline(id, fd, &r);
    else if (id < 2750) f_http_client(id, fd, &r);
    else if (id < 2900) f_net_invalid(id, fd, &r);
    else f_utf8_stream(id, fd, &r);
}

static int fork_case(void (*fn)(int, int), int arg, int pfds[2], pid_t *pid) {
    if (pipe(pfds) != 0) return -1;
    *pid = fork();
    if (*pid < 0) { close(pfds[0]); close(pfds[1]); return -1; }
    if (*pid == 0) {
        close(pfds[0]);
        alarm(ALARM_SEC);
        fn(arg, pfds[1]);
        close(pfds[1]);
        _exit(0);
    }
    close(pfds[1]);
    return 0;
}
static size_t read_line(int fd, char *out, size_t cap) {
    size_t n = 0;
    for (;;) {
        char c = '\0'; ssize_t k = read(fd, &c, 1);
        if (k <= 0 || c == '\n') break;
        if (n + 1 < cap) out[n++] = c; else break;
    }
    out[n] = '\0'; return n;
}
static void log_text(const char *s) {
    if (g_log != (FILE *)0) { fputs(s, g_log); fflush(g_log); }
    hash_line(s);
}

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "tests/redteam/json_net/results/scenarios_cplus.log";
    char line[256];
    int pfds[2], status, id;
    pid_t pid;
    g_log = fopen(path, "w");
    if (g_log == (FILE *)0) { fprintf(stderr, "REDTEAM: cannot open %s\n", path); return 2; }

    for (id = 0; id < N_CASES; id++) {
        char text[320];
        int n, v_id, v_sent, v_ok, v_prom;
        char api[48], name[48], result[48];
        int parsed, rc;
        if (fork_case(dispatch, id, pfds, &pid) != 0) {
            snprintf(text, sizeof(text), "E %05d FORK_FAIL 0 0 0\n", id);
            log_text(text); g_cases++; g_crashes++; continue;
        }
        n = (int)read_line(pfds[0], line, sizeof(line));
        close(pfds[0]);
        rc = waitpid(pid, &status, 0);
        g_cases++;
        if (rc != pid) {
            snprintf(text, sizeof(text), "E %05d WAIT_FAIL\n", id);
            log_text(text); g_crashes++; continue;
        }
        if (WIFSIGNALED(status)) {
            int sig = WTERMSIG(status);
            snprintf(text, sizeof(text), "E %05d SIGNAL %d 0 0\n", id, sig);
            log_text(text); g_crashes++; continue;
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || n == 0) {
            snprintf(text, sizeof(text), "E %05d NO_RESULT rc=%d 0 0\n",
                     id, WIFEXITED(status) ? WEXITSTATUS(status) : -1);
            log_text(text); g_crashes++; continue;
        }
        parsed = sscanf(line, "S %d %47s %47s %47s %d %d %d",
                        &v_id, api, name, result, &v_sent, &v_ok, &v_prom);
        if (parsed != 7 || v_id != id) {
            snprintf(text, sizeof(text), "E %05d MALFORMED 0 0 0\n", id);
            log_text(text); g_crashes++; continue;
        }
        snprintf(text, sizeof(text), "%s\n", line);
        log_text(text);
        if (v_sent) g_sent_ok++; else g_corrupt++;
        if (!v_ok) g_mismatch++;
        if (strncmp(result, "CORRUPT", 7) == 0) g_corrupt++;
        if (strncmp(result, "UNEXPECTED_OK", 13) == 0) g_unexpected++;
        else if (v_prom && (strncmp(result, "REFUSED", 7) == 0 ||
                            strncmp(result, "REFUSED_", 8) == 0)) g_refused++;
    }
    fclose(g_log);
    printf("REDTEAM_PART json_net %d %d %d %d %d\n",
           g_cases, g_refused, g_crashes, g_corrupt, g_unexpected);
    printf("REDTEAM_DIGEST_CPLUS %016llx\n", g_hash);
    fprintf(stderr, "REDTEAM detail cplus: sentinel_ok=%d defined_mismatch=%d\n",
            g_sent_ok, g_mismatch);
    return 0;
}
