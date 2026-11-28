/* 1000 deterministic adversarial scenarios for stdlib/net.hp + stdlib/http.hp.
 *
 * Offline by construction: libcurl's in-process URL parser, the header's own
 * bounded response sink, slist handling, invalid/borrowed descriptors, and
 * AF_UNIX socketpairs carrying synthetic HTTP byte streams. No DNS, no proxy,
 * no external host and no 127.0.0.1 listener is contacted by this binary.
 *
 * Build (http.hp needs --default; see NOTES.md for the exact reason):
 *   ./build/c+ build tests/adversarial_1000/net_http/adv1000_net_http.cp \
 *       --default --sanitize=none -lcurl -o <bin>
 * Run from the repository root with `timeout 120 <bin>`.
 *
 * Output: one `S <id> <api> <case> <result> <sentinel_ok>` line per scenario,
 * then `ADV1000 net_http 1000 <refused> <sentinel_ok> <failures>` and an
 * in-process FNV-1a digest. All 1000 S lines are also written to
 * tests/adversarial_1000/net_http/results/scenarios.log.
 *
 * EVIDENCE, NOT PROOF: 1000 fixed-seed executions of defined API contracts,
 * not a formal verification of the headers, the compiler, or libcurl.
 */
#include <curl/curl.h>
#include <curl/urlapi.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/http.hp"
#include "stdlib/net.hp"

#define RESULTS "tests/adversarial_1000/net_http/results/"
#define GUARD 16
#define BUF_MAX 4096
#define LONG_URL_MAX 8002000
#define SENT 0xA5

static char g_buf[GUARD + BUF_MAX + GUARD];       /* sink / read buffer       */
static char g_src[8192];                           /* source bytes for sink    */
static char g_small[GUARD + 2048 + GUARD];         /* URL / header scratch     */
static char g_long[GUARD + LONG_URL_MAX + GUARD];  /* overlong URL scratch     */
static char g_read[GUARD + BUF_MAX + GUARD];       /* socketpair read buffer   */
static char g_msg[GUARD + 512 + GUARD];            /* synthetic HTTP payload   */
static char g_line[160];

static unsigned long long g_rng = 0x243F6A8885A308D3ULL; /* fixed seed        */
static unsigned long long g_hash = 1469598103934665603ULL; /* FNV-1a offset   */
static int g_refused = 0;
static int g_sentinel = 0;
static int g_failures = 0;
static int g_count = 0;
static FILE *g_log = (FILE *)0;

/* xorshift64*: deterministic, no external entropy. */
static unsigned int rnd_u32(void) {
    unsigned long long x = g_rng;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    g_rng = x;
    return (unsigned int)((x * 2685821657736338717ULL) >> 32);
}

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

/* Emit one scenario line to stdout and to the (gitignored) log. */
static void emit(int id, const char *api, const char *name,
                 const char *result, int sent_ok, int expected_refusal,
                 int ok) {
    if (!ok || !sent_ok) g_failures++;
    if (expected_refusal) g_refused++;
    if (sent_ok) g_sentinel++;
    g_count++;
    snprintf(g_line, sizeof(g_line), "S %04d %s %s %s %d\n",
             id, api, name, result, sent_ok);
    hash_line(g_line);
    fputs(g_line, stdout);
    if (g_log != (FILE *)0)
        fputs(g_line, g_log);
}

/* ---------------- F1: zero-length sink capacity (100) -------------------- */
static void family_zero_cap(int base) {
    int i;
    for (i = 0; i < 100; i++) {
        http_sink sw;
        unsigned int a, b;
        size_t before_used;
        int before_ovf, ok, sent_ok;
        char name[32];

        fill(g_buf, sizeof(g_buf));
        fill(g_src, sizeof(g_src));
        sw.buf = (i % 7 == 0) ? (char *)0 : (g_buf + GUARD);
        sw.size = 0; /* the adversarial capacity under test */
        sw.used = (size_t)(rnd_u32() % 3u);
        before_used = sw.used;
        before_ovf = (int)(rnd_u32() & 1u);
        sw.overflow = before_ovf;
        a = (i % 5 == 0) ? 0xFFFFFFFFu : (rnd_u32() % 17u);
        b = (i % 5 == 0) ? 3u : (rnd_u32() % 4097u);
        ok = http_sink_write(g_src, (size_t)a, (size_t)b, (void *)&sw) == 0u &&
             sw.used == before_used && sw.overflow == before_ovf;
        sent_ok = all_sentinel(g_buf, sizeof(g_buf));
        snprintf(name, sizeof(name), "zero_cap_%03d", i);
        emit(base + i, "http_sink_write", name, "REFUSED", sent_ok, 1, ok);
    }
}

/* ------- F2: null-argument refusals (sink + http_client) (100) ----------- */
static void family_null_arg(int base) {
    int i;
    for (i = 0; i < 100; i++) {
        char name[32];
        if (i < 50) {
            http_sink sw;
            unsigned int a, b;
            int sent_ok, ok, mode, before_ovf;
            size_t before_used;

            fill(g_buf, sizeof(g_buf));
            fill(g_src, sizeof(g_src));
            mode = i % 3;
            sw.buf = (mode == 1) ? (char *)0 : (g_buf + GUARD);
            sw.size = 1u + (rnd_u32() % (unsigned int)BUF_MAX);
            sw.used = 0;
            before_used = sw.used;
            before_ovf = (int)(rnd_u32() & 1u);
            sw.overflow = before_ovf;
            a = 1u + (rnd_u32() % 8u);
            b = 1u + (rnd_u32() % 64u);
            if (mode == 2) {
                /* NULL ptr: the header reaches the ptr check, no write. */
                ok = http_sink_write((char *)0, a, b, (void *)&sw) == 0u &&
                     sw.used == before_used && sw.overflow == before_ovf;
            } else if (mode == 0) {
                /* NULL sink pointer: no sink fields can be touched. */
                ok = http_sink_write(g_src, a, b, (void *)0) == 0u;
            } else {
                /* NULL buf: early refusal, no write. */
                ok = http_sink_write(g_src, a, b, (void *)&sw) == 0u &&
                     sw.used == before_used && sw.overflow == before_ovf;
            }
            sent_ok = all_sentinel(g_buf, sizeof(g_buf));
            snprintf(name, sizeof(name), "null_%03d", i);
            emit(base + i, "http_sink_write", name, "REFUSED", sent_ok, 1, ok);
        } else {
            error e = error();
            http_client client = http_client(&e);
            int rc, ok, sent_ok, mode;
            const char *url = "http://127.0.0.1:1/offline";

            fill(g_buf, sizeof(g_buf));
            mode = (i - 50) % 5;
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
            snprintf(name, sizeof(name), "client_arg_%03d", i);
            emit(base + i, "http_client.get", name, "REFUSED", sent_ok, 1, ok);
        }
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

/* Explicit boundary table: a switch avoids a variable array index that the
 * C+ checker cannot prove bounded. Covers 0,1,2,3,7,8,...,4095,4096. */
static size_t boundary_at(int i) {
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
    default: return (size_t)(rnd_u32() % ((unsigned int)BUF_MAX + 1u));
    }
}

/* --------- F3: capacities 0..4096 and boundary lengths (100) ------------ */
static void family_capacity(int base) {
    int i;
    for (i = 0; i < 100; i++) {
        http_sink sw;
        size_t cap, used, bytes, k;
        size_t got_ret, expected_ret = 0, used_after = 0, room = 0, written = 0;
        int ovf = 0, bounds_refusal = 0, ok, sent_ok;

        fill(g_buf, sizeof(g_buf));
        fill(g_src, sizeof(g_src));
        cap = (i < 25) ? boundary_at(i)
                       : (size_t)(rnd_u32() % ((unsigned int)BUF_MAX + 1u));
        used = (cap == 0) ? 0 : (size_t)(rnd_u32() % (unsigned int)cap);
        switch (i % 6) {
        case 0: bytes = cap; break;
        case 1: bytes = cap + 1u; break;
        case 2: bytes = 1u; break;
        case 3: bytes = (used < cap) ? (cap - used) : 1u; break;
        case 4: bytes = 4097u; break;
        default: bytes = 1u + (size_t)(rnd_u32() % 8191u); break;
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
        sent_ok = guards_ok(g_buf);
        if (bytes == 0) {
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
            snprintf(name, sizeof(name), "cap_%03d", i);
            emit(base + i, "http_sink_write", name,
                 bounds_refusal ? "BOUNDS_REFUSED" : "ACCEPTED",
                 sent_ok, bounds_refusal, ok);
        }
    }
}

/* -------- F4: size*nmemb multiplication-overflow guard (100) ------------ */
static void family_mul_guard(int base) {
    int i;
    for (i = 0; i < 100; i++) {
        http_sink sw;
        size_t a, b, prod = 0;
        int mul_over, ok, sent_ok;
        size_t got;
        char name[32];

        fill(g_buf, sizeof(g_buf));
        fill(g_src, sizeof(g_src));
        if (i % 3 == 0) {
            a = SIZE_MAX;
            b = 2u + (rnd_u32() % 100u);
        } else if (i % 3 == 1) {
            a = (size_t)1 << 62;
            b = 4u;
        } else {
            a = 0u;
            b = rnd_u32() % 1000u;
        }
        mul_over = __builtin_mul_overflow(a, b, &prod) ? 1 : 0;
        sw.buf = g_buf + GUARD;
        sw.size = 1u + (rnd_u32() % (unsigned int)BUF_MAX);
        sw.used = 0;
        sw.overflow = 0;
        got = http_sink_write(g_src, a, b, (void *)&sw);
        ok = got == 0u && sw.used == 0u &&
             sw.overflow == mul_over && all_sentinel(g_buf, sizeof(g_buf));
        sent_ok = all_sentinel(g_buf, sizeof(g_buf));
        snprintf(name, sizeof(name), "mulovf_%03d", i);
        emit(base + i, "http_sink_write", name,
             mul_over ? "REFUSED" : "ZERO_BYTES", sent_ok, mul_over, ok);
    }
}

/* --------- F5: malformed URL parse refusals (100) ----------------------- */
static void family_url_malformed(int base) {
    static const char *bad[] = {
        "", "://", "http://", "http:// ", " http://example.com/",
        "ht tp://example.com/", "http://exa mple.com/", "http://[::1",
        "http://[bad]/", "http://example.com:abc/",
        "http://example.com:99999/", "http://user@:80/", "http://?x=1",
        "//example.com/path", "http://example.com/#frag\n", "http://\r\n",
        "mailto:user"
    };
    int i;
    for (i = 0; i < 100; i++) {
        const char *t = bad[i % (int)(sizeof(bad) / sizeof(bad[0]))];
        size_t tlen = strlen(t), plen;
        int mode = i % 4, ok, sent_ok;
        CURLU *uh;
        CURLUcode rc;
        char name[32], result[32];

        fill(g_small, sizeof(g_small));
        if (mode == 0) {
            memcpy(g_small + GUARD, t, tlen + 1u);
        } else {
            g_small[GUARD] = (mode == 1) ? ' ' : ((mode == 2) ? '\r' : '\t');
            memcpy(g_small + GUARD + 1u, t, tlen + 1u);
        }
        plen = strlen(g_small + GUARD);
        uh = curl_url();
        if (uh == (CURLU *)0) {
            emit(base + i, "curl_url_set", "url_alloc_fail", "ALLOC_FAIL", 0, 0, 0);
            continue;
        }
        rc = curl_url_set(uh, CURLUPART_URL, g_small + GUARD, 0);
        curl_url_cleanup(uh);
        ok = rc != CURLUE_OK;
        sent_ok = all_sentinel(g_small, GUARD) &&
                  all_sentinel(g_small + GUARD + plen + 1u,
                               sizeof(g_small) - GUARD - plen - 1u);
        snprintf(result, sizeof(result), "REFUSED_E%d", (int)rc);
        snprintf(name, sizeof(name), "urlbad_%03d", i);
        emit(base + i, "curl_url_set", name, result, sent_ok, 1, ok);
    }
}

/* --------- F6: overlong (> 8,000,000 byte) URL parse refusals (100) ----- */
static void family_url_overlong(int base) {
    int i;
    for (i = 0; i < 100; i++) {
        size_t len = 8000001u + (rnd_u32() % 1000u);
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
            emit(base + i, "curl_url_set", "url_alloc_fail", "ALLOC_FAIL", 0, 0, 0);
            continue;
        }
        rc = curl_url_set(uh, CURLUPART_URL, g_long + GUARD, 0);
        curl_url_cleanup(uh);
        ok = rc != CURLUE_OK;
        sent_ok = all_sentinel(g_long, GUARD) &&
                  all_sentinel(g_long + GUARD + len + 1u,
                               sizeof(g_long) - GUARD - len - 1u);
        snprintf(result, sizeof(result), "REFUSED_E%d", (int)rc);
        snprintf(name, sizeof(name), "urllong_%03d", i);
        emit(base + i, "curl_url_set", name, result, sent_ok, 1, ok);
    }
}

/* --------- F7: malformed / injected / oversized header lines (100) ------ */
static void family_header_line(int base) {
    static const char *lines[] = {
        "X-Adversarial: ok\r\nX-Injected: yes",
        "X-Adversarial: ok\rX-Injected: yes",
        "X-Adversarial: ok\nX-Injected: yes",
        "X-Adversarial: \x01\x02\x03",
        ":", ""
    };
    int i;
    for (i = 0; i < 100; i++) {
        http_headers hh = http_headers();
        error e = error();
        const char *line;
        size_t len;
        int rc, ok, sent_ok, is_null = (i % 7 == 0);
        char name[32];

        fill(g_small, sizeof(g_small));
        error_reset(&e);
        if (is_null) {
            rc = hh.add((const char *)0, &e);
            ok = rc == STDLIB_FAIL && e.failed_now() &&
                 hh.native() == (struct curl_slist *)0 && !hh.valid();
            sent_ok = all_sentinel(g_small, sizeof(g_small));
            snprintf(name, sizeof(name), "hdrnull_%03d", i);
            emit(base + i, "http_headers.add", name, "REFUSED", sent_ok, 1, ok);
        } else {
            if (i % 3 == 0) {
                len = 1u + (rnd_u32() % 1800u);
                memcpy(g_small + GUARD, "X-Adversarial: ", 15u);
                memset(g_small + GUARD + 15u, 'H', len);
                g_small[GUARD + 15u + len] = '\0';
            } else {
                line = lines[(i / 3) % (int)(sizeof(lines) / sizeof(lines[0]))];
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
            snprintf(name, sizeof(name), "hdrline_%03d", i);
            emit(base + i, "http_headers.add", name, "DEFINED_COPY",
                 sent_ok, 0, ok);
        }
        hh.drop();
    }
}

/* --------- F8: invalid / borrowed descriptor refusals (100) ------------ */
static void family_net_invalid(int base) {
    int i;
    for (i = 0; i < 100; i++) {
        error e = error();
        int fd, mode, expect_cat = STDLIB_CATEGORY_OK, got, ok, sent_ok;
        char name[32];
        const char *api;

        switch (i % 5) {
        case 0: fd = -1; break;
        case 1: fd = 0; break;
        case 2: fd = 1; break;
        case 3: fd = 2; break;
        default: fd = 999999; break;
        }
        mode = (i / 5) % 4;
        fill(g_buf, sizeof(g_buf));
        fill(g_src, sizeof(g_src));
        error_reset(&e);
        if (mode == 0) {
            connection cx = connection_borrow(fd);
            size_t cap = (i % 11 == 0) ? 0 : 1u + (rnd_u32() % (unsigned int)BUF_MAX);
            char *dst = (i % 13 == 0) ? (char *)0 : (g_buf + GUARD);
            /* fd 999999 is the only borrowed descriptor that passes is_valid(),
             * so only it reaches the failing read() and gets category IO. */
            expect_cat = (fd == 999999 && cap != 0 && dst != (char *)0)
                             ? STDLIB_CATEGORY_IO : STDLIB_CATEGORY_INVALID;
            got = cx.read(dst, cap, &e);
            ok = got == STDLIB_FAIL && e.failed_now() &&
                 e.category() == expect_cat;
            api = "connection.read";
            sent_ok = all_sentinel(g_buf, sizeof(g_buf));
        } else if (mode == 1) {
            connection cw = connection_borrow(fd);
            const char *src = (i % 13 == 0) ? (const char *)0 : g_src;
            size_t len = (size_t)(rnd_u32() % 128u);
            expect_cat = (fd == 999999 && src != (const char *)0)
                             ? STDLIB_CATEGORY_IO : STDLIB_CATEGORY_INVALID;
            got = cw.write(src, len, &e);
            ok = got == STDLIB_FAIL && e.failed_now() &&
                 e.category() == expect_cat;
            api = "connection.write";
            sent_ok = all_sentinel(g_buf, sizeof(g_buf));
        } else if (mode == 2) {
            listener lp = (i % 2 == 0) ? listener(-1) : listener_borrow(fd);
            got = lp.bound_port();
            ok = got == 0;
            api = "listener.bound_port";
            sent_ok = all_sentinel(g_buf, sizeof(g_buf));
        } else {
            listener la = (i % 2 == 0) ? listener(-1) : listener_borrow(fd);
            connection acc = la.accept();
            got = acc.is_valid();
            ok = got == 0 && acc.descriptor() == -1;
            api = "listener.accept";
            sent_ok = all_sentinel(g_buf, sizeof(g_buf));
        }
        snprintf(name, sizeof(name), "netbad_%03d", i);
        emit(base + i, api, name, "REFUSED", sent_ok, 1, ok);
    }
}

/* ---------------- synthetic HTTP framing classifier -------------------- */
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

/* A truncated prefix may legitimately stop at any stage of the defined parse
 * model; require the result to be one of the model's tokens. */
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

/* -------- F9 / F10: truncated + chunked HTTP over AF_UNIX (200) --------- */
static void family_http_stream(int base, const HttpCase *tbl, int ntbl,
                               const char *tag) {
    int i;
    for (i = 0; i < 100; i++) {
        const HttpCase *hc = &tbl[i % ntbl];
        size_t plen = strlen(hc->payload), k, sent_total = 0;
        size_t cap, got_n, expect_n;
        int sp[2], got, ok, sent_ok;
        connection cx;
        error e = error();
        char name[40];

        fill(g_msg, sizeof(g_msg));
        memcpy(g_msg + GUARD, hc->payload, plen + 1u);
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0) {
            snprintf(name, sizeof(name), "%s_%03d", tag, i);
            emit(base + i, "connection.read", name, "SOCKETPAIR_FAIL", 0, 0, 0);
            continue;
        }
        while (sent_total < plen) {
            ssize_t w = write(sp[1], g_msg + GUARD + sent_total,
                              plen - sent_total);
            if (w <= 0)
                break;
            sent_total += (size_t)w;
        }
        ok = sent_total == plen;

        if (i % 4 == 0 && plen > 1u)
            cap = 1u + (rnd_u32() % (unsigned int)(plen - 1u));
        else
            cap = plen + (rnd_u32() % 40u);
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
            snprintf(name, sizeof(name), "%s_%03d", tag, i);
            emit(base + i, "connection.read", name, cls, sent_ok, 0, ok);
        }
        close(sp[0]);
        close(sp[1]);
    }
}

int main(void) {
    error err = error();
    http global_http = http(&err);
    CURLU *uctrl = curl_url();
    CURLUcode rc;
    char *part = (char *)0;
    int ok = 1;

    /* controls: the offline parser and global init really work */
    if (uctrl == (CURLU *)0 || !global_http.valid())
        return 3;
    rc = curl_url_set(uctrl, CURLUPART_URL, "http://127.0.0.1:9/control", 0);
    ok = ok && rc == CURLUE_OK;
    if (ok) {
        rc = curl_url_get(uctrl, CURLUPART_HOST, &part, 0);
        ok = ok && rc == CURLUE_OK && part != (char *)0 &&
             strcmp(part, "127.0.0.1") == 0;
        if (part != (char *)0)
            curl_free(part);
    }
    curl_url_cleanup(uctrl);

    g_log = fopen(RESULTS "scenarios.log", "w");
    if (g_log == (FILE *)0) {
        fprintf(stderr, "ADV1000: cannot open %sscenarios.log\n", RESULTS);
        global_http.drop();
        return 2;
    }
    if (!ok) {
        fprintf(stderr, "ADV1000: control check failed\n");
        fclose(g_log);
        global_http.drop();
        return 4;
    }

    family_zero_cap(0);
    family_null_arg(100);
    family_capacity(200);
    family_mul_guard(300);
    family_url_malformed(400);
    family_url_overlong(500);
    family_header_line(600);
    family_net_invalid(700);
    family_http_stream(800, g_trunc,
                       (int)(sizeof(g_trunc) / sizeof(g_trunc[0])), "trunc");
    family_http_stream(900, g_chunk,
                       (int)(sizeof(g_chunk) / sizeof(g_chunk[0])), "chunk");

    if (fclose(g_log) != 0) {
        global_http.drop();
        return 5;
    }
    global_http.drop();
    printf("ADV1000 net_http %d %d %d %d\n", g_count, g_refused, g_sentinel,
           g_failures);
    printf("ADV1000_DIGEST %016llx\n", g_hash);
    return g_failures != 0 || g_count != 1000;
}
