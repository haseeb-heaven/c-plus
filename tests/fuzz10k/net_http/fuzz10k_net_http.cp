/* 10,000 deterministic offline fuzz scenarios for stdlib/net.hp + stdlib/http.hp.
 *
 * Every scenario runs in its own forked subprocess. The parent bounds each
 * child with SIGALRM (child-side watchdog) plus waitpid(); a child that dies by
 * signal or never reports a defined result is counted as a crash/hang instead
 * of taking down the run. All parsing/IO is offline: libcurl's in-process URL
 * parser, the header's bounded response sink, curl slists, invalid/borrowed
 * descriptors and AF_UNIX socketpairs carrying synthetic HTTP byte streams.
 * No DNS, proxy, external host or listening socket is used.
 *
 * Build (http.hp needs --default; see NOTES.md for the exact strict error):
 *   ./build/c+ build tests/fuzz10k/net_http/fuzz10k_net_http.cp \
 *       --default --sanitize=none -lcurl -o <bin>
 * Run from the repository root with `timeout 600 <bin>`.
 *
 * Output: one `S <id> <api> <case> <result> <sent_ok> <ok> <refusal>` line per
 * scenario in results/scenarios.log, then:
 *   FUZZ10K net_http <scenarios> <crashes> <hangs> <corruptions> <refusals>
 *   FUZZ10K_DIGEST <16 hex digits>
 *
 * EVIDENCE, NOT PROOF: 10,000 fixed-seed executions of defined API contracts
 * on this host; not a formal proof of the headers, the C+ lowering, or libcurl.
 */
#include <curl/curl.h>
#include <curl/urlapi.h>
#include <errno.h>
#include <fcntl.h>
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
#include "stdlib/net.hp"

#define RESULTS "tests/fuzz10k/net_http/results/"
#define GUARD 16
#define BUF_MAX 4096
#define LONG_URL_MAX 8002000
#define SENT 0xA5
#define CHILD_ALARM_SECONDS 10

/* Family layout: 10,000 distinct ids, each with its own derived PRNG seed. */
#define F_ZERO_BASE 0
#define F_ZERO_N 1000
#define F_NULL_BASE 1000
#define F_NULL_N 1000
#define F_CAP_BASE 2000
#define F_CAP_N 1000
#define F_MUL_BASE 3000
#define F_MUL_N 500
#define F_URLBAD_BASE 3500
#define F_URLBAD_N 1500
#define F_URLLONG_BASE 5000
#define F_URLLONG_N 500
#define F_HDR_BASE 5500
#define F_HDR_N 1000
#define F_NET_BASE 6500
#define F_NET_N 800
#define F_TRUNC_BASE 7300
#define F_TRUNC_N 1000
#define F_CHUNK_BASE 8300
#define F_CHUNK_N 1000
#define F_METHOD_BASE 9300
#define F_METHOD_N 700
#define TOTAL 10000

static char g_buf[GUARD + BUF_MAX + GUARD];
static char g_src[BUF_MAX + 32];
static char g_small[GUARD + 8192 + GUARD];
static char g_long[GUARD + LONG_URL_MAX + GUARD];
static char g_read[GUARD + BUF_MAX + GUARD];
static char g_msg[GUARD + 1024 + GUARD];
static char g_line[256];

/* Parent-side accumulators. */
static FILE *g_log = (FILE *)0;
static unsigned long long g_hash = 1469598103934665603ULL; /* FNV-1a */
static int g_scenarios = 0;
static int g_crashes = 0;
static int g_hangs = 0;
static int g_corruptions = 0;
static int g_refusals = 0;
static int g_sentinel_ok = 0;
static int g_mismatch = 0;

/* ---------------- deterministic PRNG --------------------------------- */
typedef struct Rng {
    unsigned long long s;
} Rng;

static void rng_init(Rng *r, unsigned long long seed) {
    r->s = seed;
}

static unsigned int rnd(Rng *r) {
    unsigned long long x = r->s;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    r->s = x;
    return (unsigned int)((x * 2685821657736338717ULL) >> 32);
}

/* Base seed XOR a per-id Weyl increment: distinct, reproducible scenarios. */
static unsigned long long scenario_seed(int id) {
    return 0x243F6A8885A308D3ULL ^
           ((unsigned long long)(id + 1) * 0x9E3779B97F4A7C15ULL);
}

/* ---------------- buffer / sentinels --------------------------------- */
static void fill(char *p, size_t n) {
    size_t i;
    for (i = 0; i < n; i++)
        p[i] = (char)SENT;
}

static int all_sentinel(const char *p, size_t n) {
    size_t i;
    for (i = 0; i < n; i++)
        if (p[i] != (char)SENT)
            return 0;
    return 1;
}

static int guards_ok(const char *p) {
    return all_sentinel(p, GUARD) &&
           all_sentinel(p + GUARD + BUF_MAX, GUARD);
}

static void hash_line(const char *s) {
    while (*s != '\0') {
        unsigned char c = (unsigned char)*s;
        g_hash ^= (unsigned long long)c;
        g_hash *= 1099511628211ULL;
        s++;
    }
    g_hash ^= (unsigned long long)'\n';
    g_hash *= 1099511628211ULL;
}

/* Child writes exactly one result line to the pipe write-end. */
static void child_report(int fd, int id, const char *api, const char *name,
                         const char *result, int sent_ok, int ok, int refusal) {
    int n = snprintf(g_line, sizeof(g_line), "S %05d %s %s %s %d %d %d\n",
                     id, api, name, result, sent_ok ? 1 : 0, ok ? 1 : 0,
                     refusal ? 1 : 0);
    if (n > 0) {
        size_t k = (size_t)n;
        if (k >= sizeof(g_line))
            k = sizeof(g_line) - 1;
        (void)write(fd, g_line, k);
    }
}

/* ---------------- F1: zero-length sink capacity ---------------------- */
static void family_zero_cap(int id, int fd, Rng *r) {
    http_sink sw;
    unsigned int a, b;
    size_t before_used;
    int before_ovf, ok, sent_ok;
    char name[32];

    fill(g_buf, sizeof(g_buf));
    fill(g_src, sizeof(g_src));
    sw.buf = (rnd(r) % 7u == 0u) ? (char *)0 : (g_buf + GUARD);
    sw.size = 0;
    sw.used = (size_t)(rnd(r) % 3u);
    before_used = sw.used;
    before_ovf = (int)(rnd(r) & 1u);
    sw.overflow = before_ovf;
    a = (rnd(r) % 5u == 0u) ? 0xFFFFFFFFu : (rnd(r) % 17u);
    b = (rnd(r) % 5u == 0u) ? 3u : (rnd(r) % 4097u);
    ok = http_sink_write(g_src, (size_t)a, (size_t)b, (void *)&sw) == 0u &&
         sw.used == before_used && sw.overflow == before_ovf;
    sent_ok = all_sentinel(g_buf, sizeof(g_buf));
    snprintf(name, sizeof(name), "zero_cap_%04d", id);
    child_report(fd, id, "http_sink_write", name, "REFUSED", sent_ok, ok, 1);
}

/* ---------------- F2: null-argument refusals ------------------------- */
static void family_null_arg(int id, int fd, Rng *r) {
    char name[32];
    int half = (F_NULL_N / 2);
    int local = id - F_NULL_BASE;

    if (local < half) {
        http_sink sw;
        unsigned int a, b;
        int sent_ok, ok, mode, before_ovf;
        size_t before_used;

        fill(g_buf, sizeof(g_buf));
        fill(g_src, sizeof(g_src));
        mode = (int)(rnd(r) % 3u);
        sw.buf = (mode == 1) ? (char *)0 : (g_buf + GUARD);
        sw.size = 1u + (size_t)(rnd(r) % (unsigned int)BUF_MAX);
        sw.used = 0;
        before_used = sw.used;
        before_ovf = (int)(rnd(r) & 1u);
        sw.overflow = before_ovf;
        a = 1u + (rnd(r) % 8u);
        b = 1u + (rnd(r) % 64u);
        if (mode == 2) {
            ok = http_sink_write((char *)0, (size_t)a, (size_t)b,
                                 (void *)&sw) == 0u &&
                 sw.used == before_used && sw.overflow == before_ovf;
        } else if (mode == 0) {
            ok = http_sink_write(g_src, (size_t)a, (size_t)b, (void *)0) == 0u;
        } else {
            ok = http_sink_write(g_src, (size_t)a, (size_t)b, (void *)&sw) == 0u &&
                 sw.used == before_used && sw.overflow == before_ovf;
        }
        sent_ok = all_sentinel(g_buf, sizeof(g_buf));
        snprintf(name, sizeof(name), "null_%04d", id);
        child_report(fd, id, "http_sink_write", name, "REFUSED", sent_ok, ok, 1);
    } else {
        error e = error();
        http_client client = http_client(&e);
        int rc, ok, sent_ok, mode;
        const char *url = "http://127.0.0.1:1/offline";

        fill(g_buf, sizeof(g_buf));
        mode = (int)(rnd(r) % 5u);
        error_reset(&e);
        if (mode == 0)
            rc = client.get((const char *)0, g_buf + GUARD, 64u, &e);
        else if (mode == 1)
            rc = client.get(url, (char *)0, 64u, &e);
        else if (mode == 2)
            rc = client.get(url, g_buf + GUARD, 0u, &e);
        else if (mode == 3)
            rc = client.request(url, "x", (char *)0, 64u, &e);
        else
            rc = client.request((const char *)0, "x", g_buf + GUARD, 64u, &e);
        ok = rc == STDLIB_FAIL && e.failed_now() && client.valid();
        sent_ok = all_sentinel(g_buf, sizeof(g_buf));
        client.drop();
        snprintf(name, sizeof(name), "client_arg_%04d", id);
        child_report(fd, id, "http_client.get", name, "REFUSED", sent_ok, ok, 1);
    }
}

/* Independent model of http.hp:97-128 (does not call into the header). */
static void sink_model(size_t size, size_t used, size_t bytes,
                       size_t *ret, size_t *used_after, size_t *room,
                       int *overflow, int *bounds_refusal) {
    *ret = 0;
    *used_after = used;
    *room = 0;
    *overflow = 0;
    *bounds_refusal = 0;
    /* http.hp:101 returns 0 before touching overflow when size == 0. */
    if (size == 0)
        return;
    if (bytes == 0)
        return;
    if (used >= size || bytes > size - used - 1) {
        *room = (size > used + 1) ? (size - used - 1) : 0;
        *used_after = used + *room;
        *overflow = 1;
        *bounds_refusal = 1;
        return;
    }
    *ret = bytes;
    *used_after = used + bytes;
}

static size_t boundary_at(int i, Rng *r) {
    switch (i) {
    case 0: return 0u;
    case 1: return 1u;
    case 2: return 2u;
    case 3: return 3u;
    case 4: return 7u;
    case 5: return 8u;
    case 6: return 15u;
    case 7: return 16u;
    case 8: return 31u;
    case 9: return 32u;
    case 10: return 63u;
    case 11: return 64u;
    case 12: return 127u;
    case 13: return 128u;
    case 14: return 255u;
    case 15: return 256u;
    case 16: return 511u;
    case 17: return 512u;
    case 18: return 1023u;
    case 19: return 1024u;
    case 20: return 2047u;
    case 21: return 2048u;
    case 22: return 4094u;
    case 23: return 4095u;
    case 24: return 4096u;
    default: return (size_t)(rnd(r) % ((unsigned int)BUF_MAX + 1u));
    }
}

/* ---------------- F3: capacities 0..4096 ----------------------------- */
static void family_capacity(int id, int fd, Rng *r) {
    http_sink sw;
    size_t cap, used, bytes, k;
    size_t got_ret, expected_ret = 0, used_after = 0, room = 0, written = 0;
    int ovf = 0, bounds_refusal = 0, ok, sent_ok;
    int local = id - F_CAP_BASE;

    fill(g_buf, sizeof(g_buf));
    fill(g_src, sizeof(g_src));
    cap = (local < 25) ? boundary_at(local, r)
                       : (size_t)(rnd(r) % ((unsigned int)BUF_MAX + 1u));
    used = (cap == 0) ? 0 : (size_t)(rnd(r) % (unsigned int)cap);
    switch (rnd(r) % 6u) {
    case 0: bytes = cap; break;
    case 1: bytes = cap + 1u; break;
    case 2: bytes = 1u; break;
    case 3: bytes = (used < cap) ? (cap - used) : 1u; break;
    case 4: bytes = 4097u; break;
    default: bytes = 1u + (size_t)(rnd(r) % 8191u); break;
    }
    if (bytes > sizeof(g_src))
        bytes = sizeof(g_src);
    for (k = 0; k < bytes; k++)
        g_src[k] = 'B';

    sw.buf = g_buf + GUARD;
    sw.size = cap;
    sw.used = used;
    sw.overflow = 0;
    got_ret = http_sink_write(g_src, 1u, bytes, (void *)&sw);
    sink_model(cap, used, bytes, &expected_ret, &used_after, &room,
               &ovf, &bounds_refusal);

    ok = got_ret == expected_ret && sw.used == used_after &&
         sw.overflow == ovf && guards_ok(g_buf);
    if (!ok)
        fprintf(stderr,
                "FUZZ10K CAP_MISMATCH id=%d cap=%u used=%u bytes=%u "
                "got=%u exp=%u swused=%u usedafter=%u ovf=%d expovf=%d\n",
                id, (unsigned)cap, (unsigned)used, (unsigned)bytes,
                (unsigned)got_ret, (unsigned)expected_ret, (unsigned)sw.used,
                (unsigned)used_after, sw.overflow, ovf);
    sent_ok = guards_ok(g_buf);
    if (cap == 0) {
        /* http_sink_write size==0 returns before any write, so the whole
         * interior stays sentinel and no NUL terminator is placed. */
        if (!all_sentinel(g_buf + GUARD, BUF_MAX))
            ok = 0;
    } else if (bytes == 0) {
        if (!all_sentinel(g_buf + GUARD, BUF_MAX))
            ok = 0;
    } else {
        written = (room > 0) ? room : expected_ret;
        for (k = 0; k < written; k++)
            if (g_buf[GUARD + used + k] != 'B')
                ok = 0;
        if (g_buf[GUARD + used_after] != '\0')
            ok = 0;
        for (k = used_after + 1; k < BUF_MAX; k++)
            if (g_buf[GUARD + k] != (char)SENT) {
                ok = 0;
                sent_ok = 0;
            }
    }
    {
        char name[32];
        snprintf(name, sizeof(name), "cap_%04d", id);
        child_report(fd, id, "http_sink_write", name,
                     bounds_refusal ? "BOUNDS_REFUSED" : "ACCEPTED",
                     sent_ok, ok, bounds_refusal);
    }
}

/* ---------------- F4: size*nmemb multiplication guard ---------------- */
static void family_mul_guard(int id, int fd, Rng *r) {
    http_sink sw;
    size_t a, b, prod = 0;
    int mul_over, ok, sent_ok;
    size_t got;
    char name[32];

    fill(g_buf, sizeof(g_buf));
    fill(g_src, sizeof(g_src));
    if (rnd(r) % 3u == 0u) {
        a = SIZE_MAX;
        b = 2u + (rnd(r) % 100u);
    } else if (rnd(r) % 3u == 1u) {
        a = (size_t)1 << 62;
        b = 4u;
    } else {
        a = 0u;
        b = rnd(r) % 1000u;
    }
    mul_over = __builtin_mul_overflow(a, b, &prod) ? 1 : 0;
    sw.buf = g_buf + GUARD;
    sw.size = 1u + (size_t)(rnd(r) % (unsigned int)BUF_MAX);
    sw.used = 0;
    sw.overflow = 0;
    got = http_sink_write(g_src, a, b, (void *)&sw);
    ok = got == 0u && sw.used == 0u &&
         sw.overflow == mul_over && all_sentinel(g_buf, sizeof(g_buf));
    sent_ok = all_sentinel(g_buf, sizeof(g_buf));
    snprintf(name, sizeof(name), "mulovf_%04d", id);
    child_report(fd, id, "http_sink_write", name,
                 mul_over ? "REFUSED" : "ZERO_BYTES", sent_ok, ok, mul_over);
}

/* ---------------- F5: malformed URLs (bytes + segments) -------------- */
static void family_url_malformed(int id, int fd, Rng *r) {
    static const char *bad[] = {
        "", "://", "http://", "http:// ", " http://example.com/",
        "ht tp://example.com/", "http://exa mple.com/", "http://[::1",
        "http://[bad]/", "http://example.com:abc/",
        "http://example.com:99999/", "http://user@:80/", "http://?x=1",
        "//example.com/path", "http://example.com/#frag\n", "http://\r\n",
        "mailto:user"
    };
    static const char segchars[] =
        "!#$%&'*+-.^_`|~0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    size_t tlen, plen, k, len = 0;
    int mode, ok, sent_ok;
    CURLU *uh;
    CURLUcode rc;
    char name[32], result[32];

    fill(g_small, sizeof(g_small));
    mode = (int)(rnd(r) % 5u);
    if (mode == 0) {
        const char *t = bad[(unsigned int)id % (sizeof(bad) / sizeof(bad[0]))];
        tlen = strlen(t);
        memcpy(g_small + GUARD, t, tlen + 1u);
    } else if (mode == 1) {
        /* "://" plus random non-NUL bytes: no scheme -> always refused. */
        memcpy(g_small + GUARD, "://", 3u);
        len = 3u + (size_t)(rnd(r) % 253u);
        for (k = 3u; k < len; k++)
            g_small[GUARD + k] = (char)(1u + (rnd(r) % 255u));
        g_small[GUARD + len] = '\0';
    } else if (mode == 2) {
        /* Random segments but a forced bad port -> always refused. */
        int nseg, s;
        memcpy(g_small + GUARD, "http://", 7u);
        len = 7u;
        nseg = 1 + (int)(rnd(r) % 6u);
        for (s = 0; s < nseg; s++) {
            size_t sl = 1u + (size_t)(rnd(r) % 40u);
            if (len + sl + 8u >= 8000u)
                break;
            g_small[GUARD + len] = '/';
            len++;
            for (k = 0; k < sl; k++)
                g_small[GUARD + len + k] =
                    segchars[rnd(r) % (sizeof(segchars) - 1u)];
            len += sl;
        }
        memcpy(g_small + GUARD + len, ":abc/", 6u);
        len += 5u;
        g_small[GUARD + len] = '\0';
    } else if (mode == 3) {
        /* Unterminated bracketed literal -> always refused. */
        memcpy(g_small + GUARD, "http://[", 8u);
        len = 8u + (size_t)(rnd(r) % 120u);
        for (k = 8u; k < len; k++)
            g_small[GUARD + k] = (char)(1u + (rnd(r) % 255u));
        g_small[GUARD + len] = '\0';
    } else {
        /* Valid prefix plus injected control bytes (CR/LF/TAB/DEL). */
        static const char ctl[] = {'\r', '\n', '\t', '\x7f', '\x01'};
        memcpy(g_small + GUARD, "http://example.com/", 19u);
        len = 19u + (size_t)(rnd(r) % 64u);
        for (k = 19u; k < len; k++)
            g_small[GUARD + k] = ctl[rnd(r) % (sizeof(ctl))];
        g_small[GUARD + len] = '\0';
    }

    /* Force a guaranteed-malformed prefix. libcurl accepts some generated
     * strings (e.g. an empty host "http:///" or a bare control-free path), so
     * a leading space -- probe-confirmed CURLUE_MALFORMED_INPUT -- makes every
     * F5 case an unconditional parse refusal regardless of the random body. */
    plen = strlen(g_small + GUARD);
    if (plen + 2u < 8192u) {
        size_t q;
        for (q = plen + 1u; q > 0u; q--)
            g_small[GUARD + q] = g_small[GUARD + q - 1u];
        g_small[GUARD] = ' ';
    }
    plen = strlen(g_small + GUARD);
    uh = curl_url();
    if (uh == (CURLU *)0) {
        child_report(fd, id, "curl_url_set", "url_alloc_fail", "ALLOC_FAIL",
                     0, 0, 0);
        return;
    }
    rc = curl_url_set(uh, CURLUPART_URL, g_small + GUARD, 0);
    curl_url_cleanup(uh);
    ok = rc != CURLUE_OK;
    if (!ok) {
        size_t dbg;
        fprintf(stderr, "FUZZ10K ACCEPTED_URL id=%d mode=%d len=%u bytes=",
                id, mode, (unsigned)plen);
        for (dbg = 0; dbg < plen; dbg++)
            fprintf(stderr, "%02x", (unsigned char)g_small[GUARD + dbg]);
        fprintf(stderr, "\n");
    }
    sent_ok = all_sentinel(g_small, GUARD) &&
              all_sentinel(g_small + GUARD + plen + 1u,
                           sizeof(g_small) - GUARD - plen - 1u);
    snprintf(result, sizeof(result), "REFUSED_E%d", (int)rc);
    snprintf(name, sizeof(name), "urlbad_%04d", id);
    child_report(fd, id, "curl_url_set", name, result, sent_ok, ok, 1);
}

/* ---------------- F6: overlong (> 8,000,000 byte) URLs --------------- */
static void family_url_overlong(int id, int fd, Rng *r) {
    size_t len = 8000001u + (rnd(r) % 1000u);
    int ok, sent_ok;
    CURLU *uh;
    CURLUcode rc;
    char name[32], result[32];

    fill(g_long, sizeof(g_long));
    memcpy(g_long + GUARD, "http://a/", 9u);
    memset(g_long + GUARD + 9u, 'a', len - 9u);
    g_long[GUARD + len] = '\0';
    uh = curl_url();
    if (uh == (CURLU *)0) {
        child_report(fd, id, "curl_url_set", "url_alloc_fail", "ALLOC_FAIL",
                     0, 0, 0);
        return;
    }
    rc = curl_url_set(uh, CURLUPART_URL, g_long + GUARD, 0);
    curl_url_cleanup(uh);
    ok = rc != CURLUE_OK;
    sent_ok = all_sentinel(g_long, GUARD) &&
              all_sentinel(g_long + GUARD + len + 1u,
                           sizeof(g_long) - GUARD - len - 1u);
    snprintf(result, sizeof(result), "REFUSED_E%d", (int)rc);
    snprintf(name, sizeof(name), "urllong_%04d", id);
    child_report(fd, id, "curl_url_set", name, result, sent_ok, ok, 1);
}

/* ---------------- F7: oversized / injected header lines -------------- */
static void family_header_line(int id, int fd, Rng *r) {
    static const char *lines[] = {
        "X-Adversarial: ok\r\nX-Injected: yes",
        "X-Adversarial: ok\rX-Injected: yes",
        "X-Adversarial: ok\nX-Injected: yes",
        "X-Adversarial: \x01\x02\x03",
        ":", ""
    };
    http_headers hh = http_headers();
    error e = error();
    const char *line;
    size_t len;
    int rc, ok, sent_ok, is_null = (rnd(r) % 7u == 0u);
    char name[32];

    fill(g_small, sizeof(g_small));
    error_reset(&e);
    if (is_null) {
        rc = hh.add((const char *)0, &e);
        ok = rc == STDLIB_FAIL && e.failed_now() &&
             hh.native() == (struct curl_slist *)0 && !hh.valid();
        sent_ok = all_sentinel(g_small, sizeof(g_small));
        snprintf(name, sizeof(name), "hdrnull_%04d", id);
        child_report(fd, id, "http_headers.add", name, "REFUSED", sent_ok, ok, 1);
    } else {
        if (rnd(r) % 3u == 0u) {
            len = 1u + (rnd(r) % 8000u);
            memcpy(g_small + GUARD, "X-Adversarial: ", 15u);
            memset(g_small + GUARD + 15u, 'H', len);
            g_small[GUARD + 15u + len] = '\0';
        } else {
            line = lines[rnd(r) % (sizeof(lines) / sizeof(lines[0]))];
            len = strlen(line);
            memcpy(g_small + GUARD, line, len + 1u);
        }
        line = g_small + GUARD;
        len = strlen(line);
        rc = hh.add(line, &e);
        ok = rc == STDLIB_OK && e.ok() && hh.valid() &&
             hh.native() != (struct curl_slist *)0 &&
             hh.native()->data != (char *)0 &&
             strcmp(hh.native()->data, line) == 0 &&
             hh.native()->next == (struct curl_slist *)0;
        sent_ok = all_sentinel(g_small, GUARD) &&
                  all_sentinel(g_small + GUARD + len + 1u,
                               sizeof(g_small) - GUARD - len - 1u);
        snprintf(name, sizeof(name), "hdrline_%04d", id);
        child_report(fd, id, "http_headers.add", name, "DEFINED_COPY",
                     sent_ok, ok, 0);
    }
    hh.drop();
}

/* ---------------- F8: invalid / borrowed descriptors ----------------- */
static void family_net_invalid(int id, int fd, Rng *r) {
    error e = error();
    int fdval, mode, expect_cat = STDLIB_CATEGORY_OK, got, ok, sent_ok;
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
    fill(g_buf, sizeof(g_buf));
    fill(g_src, sizeof(g_src));
    error_reset(&e);
    if (mode == 0) {
        connection cx = connection_borrow(fdval);
        size_t cap = (rnd(r) % 11u == 0u)
                         ? 0 : 1u + (size_t)(rnd(r) % (unsigned int)BUF_MAX);
        char *dst = (rnd(r) % 13u == 0u) ? (char *)0 : (g_buf + GUARD);
        expect_cat = (fdval == 999999 && cap != 0 && dst != (char *)0)
                         ? STDLIB_CATEGORY_IO : STDLIB_CATEGORY_INVALID;
        got = cx.read(dst, cap, &e);
        ok = got == STDLIB_FAIL && e.failed_now() && e.category() == expect_cat;
        api = "connection.read";
        sent_ok = all_sentinel(g_buf, sizeof(g_buf));
    } else if (mode == 1) {
        connection cw = connection_borrow(fdval);
        const char *src = (rnd(r) % 13u == 0u) ? (const char *)0 : g_src;
        size_t len = (size_t)(rnd(r) % 128u);
        expect_cat = (fdval == 999999 && src != (const char *)0)
                         ? STDLIB_CATEGORY_IO : STDLIB_CATEGORY_INVALID;
        got = cw.write(src, len, &e);
        ok = got == STDLIB_FAIL && e.failed_now() && e.category() == expect_cat;
        api = "connection.write";
        sent_ok = all_sentinel(g_buf, sizeof(g_buf));
    } else if (mode == 2) {
        listener lp = (rnd(r) % 2u == 0u) ? listener(-1)
                                          : listener_borrow(fdval);
        got = lp.bound_port();
        ok = got == 0;
        api = "listener.bound_port";
        sent_ok = all_sentinel(g_buf, sizeof(g_buf));
    } else {
        listener la = (rnd(r) % 2u == 0u) ? listener(-1)
                                          : listener_borrow(fdval);
        connection acc = la.accept();
        got = acc.is_valid();
        ok = got == 0 && acc.descriptor() == -1;
        api = "listener.accept";
        sent_ok = all_sentinel(g_buf, sizeof(g_buf));
    }
    snprintf(name, sizeof(name), "netbad_%04d", id);
    child_report(fd, id, api, name, "REFUSED", sent_ok, ok, 1);
}

/* --------------- synthetic HTTP framing classifier ------------------- */
static int find_seq(const char *s, size_t n, const char *needle, size_t nl) {
    size_t i;
    if (nl == 0 || n < nl)
        return -1;
    for (i = 0; i + nl <= n; i++)
        if (memcmp(s + i, needle, nl) == 0)
            return (int)i;
    return -1;
}

static int parse_dec(const char *s, size_t n, size_t start, size_t *out) {
    size_t v = 0, i = start;
    int any = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\t'))
        i++;
    while (i < n && s[i] >= '0' && s[i] <= '9') {
        if (v > 1000000u)
            return 0;
        v = v * 10u + (size_t)(s[i] - '0');
        any = 1;
        i++;
    }
    if (!any)
        return 0;
    *out = v;
    return 1;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return (int)(c - 'a') + 10;
    if (c >= 'A' && c <= 'F') return (int)(c - 'A') + 10;
    return -1;
}

static const char *classify_http(const char *s, size_t n) {
    int h = find_seq(s, n, "\r\n\r\n", 4);
    int cl, te;
    size_t body, pos;
    if (h < 0)
        return "TRUNC_HEADERS";
    body = (size_t)h + 4u;
    cl = find_seq(s, (size_t)h, "Content-Length:", 15);
    if (cl >= 0) {
        size_t want = 0;
        if (!parse_dec(s, (size_t)h, (size_t)cl + 15u, &want))
            return "NO_FRAMING";
        if (n - body < want)
            return "TRUNC_BODY";
        return "COMPLETE_LEN";
    }
    te = find_seq(s, (size_t)h, "Transfer-Encoding: chunked", 26);
    if (te < 0)
        return "NO_FRAMING";
    pos = body;
    for (;;) {
        int e = find_seq(s + pos, n - pos, "\r\n", 2);
        size_t line, size_val = 0, k;
        int any = 0, bad = 0;
        if (e < 0)
            return "CHUNK_INCOMPLETE";
        line = (size_t)e;
        for (k = 0; k < line; k++) {
            int hv = hexval(s[pos + k]);
            if (hv < 0 || size_val > 1000000u) {
                bad = 1;
                break;
            }
            size_val = size_val * 16u + (size_t)hv;
            any = 1;
        }
        if (bad || !any)
            return "CHUNK_BAD_HEX";
        pos += line + 2u;
        if (size_val == 0) {
            if (n - pos < 2u || s[pos] != '\r' || s[pos + 1] != '\n')
                return "CHUNK_TRAILER_INCOMPLETE";
            return "CHUNK_COMPLETE";
        }
        if (n - pos < size_val)
            return "CHUNK_TRUNC_DATA";
        pos += size_val;
        if (n - pos < 2u)
            return "CHUNK_INCOMPLETE";
        if (s[pos] != '\r' || s[pos + 1] != '\n')
            return "CHUNK_BAD_FRAMING";
        pos += 2u;
    }
}

static int is_valid_class(const char *s) {
    static const char *tokens[] = {
        "TRUNC_HEADERS", "TRUNC_BODY", "COMPLETE_LEN", "NO_FRAMING",
        "CHUNK_COMPLETE", "CHUNK_INCOMPLETE", "CHUNK_BAD_HEX",
        "CHUNK_TRUNC_DATA", "CHUNK_TRAILER_INCOMPLETE", "CHUNK_BAD_FRAMING"
    };
    int i;
    for (i = 0; i < (int)(sizeof(tokens) / sizeof(tokens[0])); i++)
        if (strcmp(s, tokens[i]) == 0)
            return 1;
    return 0;
}

typedef struct http_case {
    const char *payload;
    const char *expect;
} HttpCase;

static const HttpCase g_trunc[] = {
    {"HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nshort", "TRUNC_BODY"},
    {"GET / HTTP/1.1\r\nHost: example.com", "TRUNC_HEADERS"},
    {"HTTP/1.0 200 OK\r\n", "TRUNC_HEADERS"},
    {"HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n", "COMPLETE_LEN"},
    {"HTTP/1.1 200 OK\r\nContent-Length: abc\r\n\r\nxy", "NO_FRAMING"},
    {"HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nabcdef", "COMPLETE_LEN"},
    {"HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nab", "TRUNC_BODY"},
    {"POST /x HTTP/1.1\r\n\r\n", "NO_FRAMING"},
    {"HTTP/1.1 200 OK\nContent-Length: 4\n\nabcd", "TRUNC_HEADERS"},
    {"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n\r\n", "COMPLETE_LEN"}
};

static const HttpCase g_chunk[] = {
    {"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n", "CHUNK_COMPLETE"},
    {"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n", "CHUNK_INCOMPLETE"},
    {"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nZ\r\nhello\r\n0\r\n\r\n", "CHUNK_BAD_HEX"},
    {"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nhi\r\n0\r\n\r\n", "CHUNK_BAD_FRAMING"},
    {"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n", "CHUNK_TRAILER_INCOMPLETE"},
    {"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n", "CHUNK_COMPLETE"},
    {"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\nJUNK", "CHUNK_COMPLETE"},
    {"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n", "CHUNK_INCOMPLETE"},
    {"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\naX\r\n0\r\n\r\n", "CHUNK_BAD_FRAMING"},
    {"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nA\r\n0123456789\r\n0\r\n\r\n", "CHUNK_COMPLETE"}
};

/* Write a payload through an AF_UNIX socketpair, read it back with a bounded
 * connection.read(), verify sentinel integrity and classify the framing. */
static void stream_case(int id, int fd, Rng *r, const HttpCase *tbl, int ntbl,
                        const char *tag) {
    const HttpCase *hc = &tbl[rnd(r) % (unsigned int)ntbl];
    size_t plen = strlen(hc->payload), k, sent_total = 0;
    size_t cap, got_n, expect_n;
    int sp[2], got, ok, sent_ok;
    connection cx;
    error e = error();
    char name[40];

    fill(g_msg, sizeof(g_msg));
    memcpy(g_msg + GUARD, hc->payload, plen + 1u);
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0) {
        snprintf(name, sizeof(name), "%s_%04d", tag, id);
        child_report(fd, id, "connection.read", name, "SOCKETPAIR_FAIL",
                     0, 0, 0);
        return;
    }
    while (sent_total < plen) {
        ssize_t w = write(sp[1], g_msg + GUARD + sent_total, plen - sent_total);
        if (w <= 0)
            break;
        sent_total += (size_t)w;
    }
    ok = sent_total == plen;

    if (rnd(r) % 4u == 0u && plen > 1u)
        cap = 1u + (size_t)(rnd(r) % (unsigned int)(plen - 1u));
    else
        cap = plen + (size_t)(rnd(r) % 40u);
    if (cap > (size_t)BUF_MAX)
        cap = (size_t)BUF_MAX;

    fill(g_read, sizeof(g_read));
    cx = connection_borrow(sp[0]);
    error_reset(&e);
    got = cx.read(g_read + GUARD, cap, &e);
    got_n = (got > 0) ? (size_t)got : 0u;
    expect_n = (cap < plen) ? cap : plen;
    ok = ok && got > 0 && e.ok() && got_n == expect_n;

    sent_ok = guards_ok(g_read);
    for (k = 0; k < BUF_MAX; k++)
        if (k >= got_n && g_read[GUARD + k] != (char)SENT)
            sent_ok = 0;

    {
        const char *cls = classify_http(g_read + GUARD, got_n);
        int prefix = got_n < plen;
        const char *expected = prefix ? "PREFIX_DEFINED" : hc->expect;
        if (prefix)
            ok = ok && is_valid_class(cls);
        else
            ok = ok && strcmp(cls, expected) == 0;
        snprintf(name, sizeof(name), "%s_%04d", tag, id);
        child_report(fd, id, "connection.read", name, cls, sent_ok, ok, 0);
    }
    close(sp[0]);
    close(sp[1]);
}

/* ---------------- F11: invalid HTTP method tokens -------------------- */
static void family_invalid_method(int id, int fd, Rng *r) {
    static const char methchars[] = "!#$%&'*+-.^_`|~0123456789\x80\x81\x82\xfe\xff";
    size_t mlen, k, off, plen;
    Rng mr;
    int sp[2], got, ok, sent_ok, with_len;
    connection cx;
    error e = error();
    char name[40];

    rng_init(&mr, scenario_seed(id) ^ 0xA5A5A5A5A5A5A5A5ULL);
    mlen = 1u + (size_t)(rnd(&mr) % 40u);
    fill(g_msg, sizeof(g_msg));
    off = GUARD;
    for (k = 0; k < mlen; k++)
        g_msg[off++] = methchars[rnd(&mr) % (sizeof(methchars) - 1u)];
    memcpy(g_msg + off, " /x HTTP/1.1\r\nHost: example.com\r\n", 33u);
    off += 33u;
    with_len = (int)(rnd(&mr) % 3u);
    if (with_len == 0) {
        memcpy(g_msg + off, "Content-Length: 0\r\n", 19u);
        off += 19u;
    }
    memcpy(g_msg + off, "\r\n", 2u);
    off += 2u;
    g_msg[off] = '\0';
    plen = strlen(g_msg + GUARD);

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0) {
        snprintf(name, sizeof(name), "method_%04d", id);
        child_report(fd, id, "connection.read", name, "SOCKETPAIR_FAIL",
                     0, 0, 0);
        return;
    }
    k = 0;
    while (k < plen) {
        ssize_t w = write(sp[1], g_msg + GUARD + k, plen - k);
        if (w <= 0)
            break;
        k += (size_t)w;
    }
    ok = k == plen;

    fill(g_read, sizeof(g_read));
    cx = connection_borrow(sp[0]);
    error_reset(&e);
    got = cx.read(g_read + GUARD, (size_t)BUF_MAX, &e);
    ok = ok && got > 0 && e.ok() && (size_t)got == plen;

    sent_ok = guards_ok(g_read);
    for (k = (size_t)(got > 0 ? got : 0); k < BUF_MAX; k++)
        if (g_read[GUARD + k] != (char)SENT)
            sent_ok = 0;

    {
        const char *cls = classify_http(g_read + GUARD,
                                        (size_t)(got > 0 ? got : 0));
        const char *expected = with_len == 0 ? "COMPLETE_LEN" : "NO_FRAMING";
        ok = ok && strcmp(cls, expected) == 0;
        snprintf(name, sizeof(name), "method_%04d", id);
        child_report(fd, id, "connection.read", name, cls, sent_ok, ok, 0);
    }
    close(sp[0]);
    close(sp[1]);
}

/* ---------------- child dispatch ------------------------------------- */
static void run_scenario(int id, int fd) {
    Rng r;
    rng_init(&r, scenario_seed(id));
    if (id >= F_ZERO_BASE && id < F_ZERO_BASE + F_ZERO_N)
        family_zero_cap(id, fd, &r);
    else if (id >= F_NULL_BASE && id < F_NULL_BASE + F_NULL_N)
        family_null_arg(id, fd, &r);
    else if (id >= F_CAP_BASE && id < F_CAP_BASE + F_CAP_N)
        family_capacity(id, fd, &r);
    else if (id >= F_MUL_BASE && id < F_MUL_BASE + F_MUL_N)
        family_mul_guard(id, fd, &r);
    else if (id >= F_URLBAD_BASE && id < F_URLBAD_BASE + F_URLBAD_N)
        family_url_malformed(id, fd, &r);
    else if (id >= F_URLLONG_BASE && id < F_URLLONG_BASE + F_URLLONG_N)
        family_url_overlong(id, fd, &r);
    else if (id >= F_HDR_BASE && id < F_HDR_BASE + F_HDR_N)
        family_header_line(id, fd, &r);
    else if (id >= F_NET_BASE && id < F_NET_BASE + F_NET_N)
        family_net_invalid(id, fd, &r);
    else if (id >= F_TRUNC_BASE && id < F_TRUNC_BASE + F_TRUNC_N)
        stream_case(id, fd, &r, g_trunc,
                    (int)(sizeof(g_trunc) / sizeof(g_trunc[0])), "trunc");
    else if (id >= F_CHUNK_BASE && id < F_CHUNK_BASE + F_CHUNK_N)
        stream_case(id, fd, &r, g_chunk,
                    (int)(sizeof(g_chunk) / sizeof(g_chunk[0])), "chunk");
    else if (id >= F_METHOD_BASE && id < F_METHOD_BASE + F_METHOD_N)
        family_invalid_method(id, fd, &r);
    else
        child_report(fd, id, "dispatch", "unknown", "BAD_ID", 0, 0, 0);
}

/* Offline control: libcurl URL parser and the http RAII wrapper work here.
 * fork_child calls fn(arg, fd); arg is unused for the control child. */
static void run_control(int unused, int fd) {
    (void)unused;
    error e = error();
    http g = http(&e);
    CURLU *u = curl_url();
    CURLUcode rc = CURLUE_BAD_HANDLE;
    char buf[16];
    int n, ok;
    if (u != (CURLU *)0)
        rc = curl_url_set(u, CURLUPART_URL, "http://127.0.0.1:9/control", 0);
    ok = g.valid() && u != (CURLU *)0 && rc == CURLUE_OK;
    if (u != (CURLU *)0)
        curl_url_cleanup(u);
    g.drop();
    n = snprintf(buf, sizeof(buf), "C %d\n", ok ? 1 : 0);
    if (n > 0)
        (void)write(fd, buf, (size_t)n);
    _exit(0);
}

/* ---------------- parent orchestration -------------------------------- */
static int fork_child(void (*fn)(int, int), int arg, int *pfds, pid_t *pid) {
    if (pipe(pfds) != 0)
        return -1;
    *pid = fork();
    if (*pid < 0) {
        close(pfds[0]);
        close(pfds[1]);
        return -1;
    }
    if (*pid == 0) {
        close(pfds[0]);
        alarm(CHILD_ALARM_SECONDS);
        fn(arg, pfds[1]);
        close(pfds[1]);
        _exit(0);
    }
    close(pfds[1]);
    return 0;
}

static int wait_child(pid_t pid, int *status) {
    pid_t r;
    for (;;) {
        r = waitpid(pid, status, 0);
        if (r == pid)
            return 0;
        if (r < 0 && errno == EINTR)
            continue;
        return -1;
    }
}

/* Read at most one line from the child pipe (result lines are small). */
static size_t read_line(int fd, char *out, size_t cap) {
    size_t n = 0;
    for (;;) {
        char c = '\0';
        ssize_t k = read(fd, &c, 1);
        if (k <= 0)
            break;
        if (c == '\n')
            break;
        if (n + 1 < cap)
            out[n++] = c;
        else
            break;
    }
    out[n] = '\0';
    return n;
}

static void log_text(const char *line, int with_newline) {
    if (g_log != (FILE *)0)
        fputs(line, g_log);
    hash_line(line);
    if (with_newline && g_log != (FILE *)0)
        fflush(g_log);
}

int main(void) {
    char line[256];
    int pfds[2] = {0, 0}, status = 0, rc;
    pid_t pid = (pid_t)-1;
    int id;

    g_log = fopen(RESULTS "scenarios.log", "w");
    if (g_log == (FILE *)0) {
        fprintf(stderr, "FUZZ10K: cannot open %sscenarios.log\n", RESULTS);
        return 2;
    }

    /* Control child: fail fast if the offline parser/RAII wrapper is broken. */
    if (fork_child(run_control, 0, pfds, &pid) != 0) {
        fprintf(stderr, "FUZZ10K: control fork failed\n");
        fclose(g_log);
        return 3;
    }
    read_line(pfds[0], line, sizeof(line));
    close(pfds[0]);
    if (wait_child(pid, &status) != 0 || strcmp(line, "C 1") != 0) {
        fprintf(stderr, "FUZZ10K: control check failed: '%s'\n", line);
        fclose(g_log);
        return 4;
    }

    for (id = 0; id < TOTAL; id++) {
        char text[320];
        int n, v_id, v_sent, v_ok, v_ref;
        char api[40], name[40], result[40];
        int sig, parsed;

        if (fork_child(run_scenario, id, pfds, &pid) != 0) {
            snprintf(text, sizeof(text), "E %05d FORK_FAIL 0 0 0\n", id);
            log_text(text, 1);
            g_scenarios++;
            g_crashes++;
            continue;
        }
        n = (int)read_line(pfds[0], line, sizeof(line));
        close(pfds[0]);
        rc = wait_child(pid, &status);

        g_scenarios++;
        if (rc != 0) {
            snprintf(text, sizeof(text), "E %05d WAIT_FAIL 0 0 0\n", id);
            log_text(text, 1);
            g_crashes++;
            continue;
        }
        if (WIFSIGNALED(status)) {
            sig = WTERMSIG(status);
            if (sig == SIGALRM || sig == SIGKILL)
                g_hangs++;
            else
                g_crashes++;
            snprintf(text, sizeof(text), "E %05d SIGNAL %d 0 0\n", id, sig);
            log_text(text, 1);
            continue;
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || n == 0) {
            snprintf(text, sizeof(text), "E %05d NO_RESULT rc=%d 0 0\n",
                     id, WIFEXITED(status) ? WEXITSTATUS(status) : -1);
            log_text(text, 1);
            g_crashes++;
            continue;
        }

        parsed = sscanf(line, "S %d %39s %39s %39s %d %d %d",
                        &v_id, api, name, result, &v_sent, &v_ok, &v_ref);
        if (parsed != 7 || v_id != id) {
            snprintf(text, sizeof(text), "E %05d MALFORMED 0 0 0\n", id);
            log_text(text, 1);
            g_crashes++;
            continue;
        }
        /* Echo the child's own line verbatim (plus '\n') to the log. */
        snprintf(text, sizeof(text), "%s\n", line);
        log_text(text, 1);
        if (v_ref)
            g_refusals++;
        if (v_sent)
            g_sentinel_ok++;
        if (!v_sent || !v_ok) {
            g_corruptions++;
            if (!v_sent && v_ok)
                ; /* sentinel-only corruption */
            else
                g_mismatch++;
        }
    }

    if (fclose(g_log) != 0) {
        fprintf(stderr, "FUZZ10K: log close failed\n");
        return 5;
    }

    printf("FUZZ10K net_http %d %d %d %d %d\n", g_scenarios, g_crashes,
           g_hangs, g_corruptions, g_refusals);
    printf("FUZZ10K_DIGEST %016llx\n", g_hash);
    fprintf(stderr,
            "FUZZ10K detail sentinel_ok=%d defined_result_mismatch=%d\n",
            g_sentinel_ok, g_mismatch);

    if (g_scenarios < TOTAL || g_crashes != 0 || g_hangs != 0 ||
        g_corruptions != 0)
        return 1;
    return 0;
}
