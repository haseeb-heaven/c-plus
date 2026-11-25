/* tests/redblue20/blue_format/proof.c
 *
 * BLUE-TEAM PROOF: checked format / IO safety of the C+ SafeCRT layer.
 *
 * Always include safe_format.h FIRST: its CPLUS_SAFE_OBJ_SIZE uses
 * __builtin_object_size(p, 1) (nearest enclosing subobject), which is the
 * definition that clamps a struct member to the member's own extent.  safe_io.h
 * is included afterwards and therefore does not override it.
 *
 * Link against build/safe/libcplus_safecrt.a (see run.sh).
 *
 * Contract proven (all with cplus_runtime_set_violation_trap(0) so violations
 * return -1 / EINVAL instead of _exit(70)):
 *   pn_printf   - %n and all length / positional / flag / embedded forms refused
 *   pn_scanf    - %n and positional scanf forms refused
 *   nonliteral  - non-literal formats refused by every *_s macro entry point
 *   bounded     - one-byte-too-small destination is bounded / dmax over real
 *                 capacity is refused, never overflows
 *   structmem   - struct-member destination capacity clamped to the member
 *   nullstream  - NULL stream/name refused by every checked IO entry point
 *   doubleclose - second close refused, use-after-close stream refused
 *   control     - legitimate formats still accepted (guards are not over-broad)
 *
 * Every case runs in a forked child with alarm(2); the parent reaps it and also
 * runs under `timeout 60` from run.sh.  Every case ends with a canary check.
 */
#define _POSIX_C_SOURCE 200112L
#include "safe_format.h" /* FIRST: fixes CPLUS_SAFE_OBJ_SIZE to type 1 */
#include "safe_io.h"
#include "safe_runtime.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#pragma clang diagnostic ignored "-Wformat-security"
#pragma clang diagnostic ignored "-Wformat-extra-args"
#pragma clang diagnostic ignored "-Wformat"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#pragma GCC diagnostic ignored "-Wformat-security"
#pragma GCC diagnostic ignored "-Wformat-extra-args"
#pragma GCC diagnostic ignored "-Wformat"
#endif

#define SEED     UINT64_C(0x424C554520666D74) /* "BLUE fmt" */
#define NCASES   2100u
#define MAGIC1   0x11223344
#define MAGIC2   0x55667788
#define LEAD_BYTE  0xC3u
#define TRAIL_BYTE 0x5Cu
#define GUARD_SZ   16u
#define BODY_SZ    64u

enum {
    EXIT_PASS = 0,
    EXIT_RC = 1,        /* wrong return value */
    EXIT_ERRNO = 2,     /* wrong errno */
    EXIT_CANARY = 3,    /* guard/canary bytes corrupted */
    EXIT_TARGET = 4,    /* %n target was modified */
    EXIT_BUF = 5,       /* destination content wrong */
    EXIT_SETUP = 6,     /* test setup failed */
    EXIT_QUERY = 7      /* closed-stream query wrong */
};

typedef struct {
    unsigned char lead[GUARD_SZ];
    unsigned char body[BODY_SZ];
    unsigned char trail[GUARD_SZ];
} gbuf_t;

typedef struct {
    unsigned char lead[GUARD_SZ];
    unsigned char trail[GUARD_SZ];
} canary_t;

typedef struct {
    uint32_t a, b, c, d;
} params_t;

static const char *const CAT_NAMES[] = {
    "pn_printf", "pn_scanf", "nonliteral", "bounded",
    "structmem", "nullstream", "doubleclose", "control"
};
#define NCATS ((uint32_t)(sizeof CAT_NAMES / sizeof CAT_NAMES[0]))

/* ---------- deterministic fixed-seed parameters ---------- */
static uint64_t mix64(uint64_t x) {
    x ^= x >> 33;
    x *= UINT64_C(0xff51afd7ed558ccd);
    x ^= x >> 33;
    x *= UINT64_C(0xc4ceb9fe1a85ec53);
    x ^= x >> 33;
    return x;
}

static void case_params(uint32_t i, params_t *p) {
    uint64_t h1 = mix64(SEED ^ ((uint64_t)i * UINT64_C(0x9E3779B97F4A7C15)));
    uint64_t h2 = mix64(h1 ^ UINT64_C(0xD1B54A32D192ED03));
    p->a = (uint32_t)h1;
    p->b = (uint32_t)(h1 >> 32);
    p->c = (uint32_t)h2;
    p->d = (uint32_t)(h2 >> 32);
}

/* ---------- canary helpers ---------- */
static void gb_init(gbuf_t *g) {
    memset(g->lead, LEAD_BYTE, GUARD_SZ);
    memset(g->trail, TRAIL_BYTE, GUARD_SZ);
    memset(g->body, 'A', BODY_SZ);
}
static int gb_ok(const gbuf_t *g) {
    uint32_t i;
    for (i = 0; i < GUARD_SZ; ++i) {
        if (g->lead[i] != LEAD_BYTE) return 0;
        if (g->trail[i] != TRAIL_BYTE) return 0;
    }
    return 1;
}
static void can_init(canary_t *c) {
    memset(c->lead, LEAD_BYTE, GUARD_SZ);
    memset(c->trail, TRAIL_BYTE, GUARD_SZ);
}
static int can_ok(const canary_t *c) {
    uint32_t i;
    for (i = 0; i < GUARD_SZ; ++i) {
        if (c->lead[i] != LEAD_BYTE) return 0;
        if (c->trail[i] != TRAIL_BYTE) return 0;
    }
    return 1;
}
/* Return 'code' if it is a failure and/or a canary broke; else 0 (PASS). */
static int finish(const gbuf_t *g, const canary_t *c, int code) {
    if (code != EXIT_PASS) return code;
    if (g && !gb_ok(g)) return EXIT_CANARY;
    if (!can_ok(c)) return EXIT_CANARY;
    return EXIT_PASS;
}

/* ---------- %n attack formats (printf family) ---------- */
static const char *const PN_PRINTF[] = {
    "%n", "%hhn", "%hn", "%ln", "%lln", "%jn", "%zn", "%tn", "%Ln", "%qn",
    "%1$n", "%2$n", "%3$n", "%1$hhn", "%2$lln", "%3$zn", "%1$ln", "%2$jn",
    "%1$hn", "%2$tn", "%1$qn", "%+n", "%-n", "%#n", "% n", "%0n", "%'n",
    "%10n", "%05n", "%.3n", "%*n", "%.*n", "%-10.4n", "%1$10n", "%1$.3n",
    "%1$*2$n", "%2$*1$n", "%+1$n", "%-1$n", "%#2$n", "%1$05n", "%1$.*n",
    "%2$.*1$n", "abc%n", "%s%n", "%%n%n", "%d %n", "x%1$ny", "%n%n",
    "pre%llnpost", "%1$n%2$n", "%20$n", "%99$n"
};
/* ---------- %n attack formats (scanf family) ---------- */
static const char *const PN_SCANF[] = {
    "%n", "%hhn", "%hn", "%ln", "%lln", "%jn", "%zn", "%tn",
    "%1$n", "%2$n", "%3$n", "%1$hhn", "%2$lln", "%3$zn", "%1$ln", "%2$jn",
    "%d%n", "%3d%n", "%*d%n", "%1$d%2$n", "%2$d%1$n", "abc%n", "%n%n",
    "%1$*2$n", "%2$*1$n", "x%1$ny", "pre%llnpost", "%1$n%1$n"
};

static void copy_fmt(const char *src, char *dst, size_t cap) {
    size_t j = 0;
    while (src[j] && j + 1 < cap) {
        dst[j] = src[j];
        ++j;
    }
    dst[j] = '\0';
}

/* ---------- category: %n refused (printf family) ---------- */
static int cat_pn_printf(const params_t *p, gbuf_t *g, canary_t *c) {
    char f[64];
    int t1 = MAGIC1, t2 = MAGIC2;
    int rc;
    copy_fmt(PN_PRINTF[p->a % (sizeof PN_PRINTF / sizeof PN_PRINTF[0])], f, sizeof f);

    errno = 0;
    switch (p->d % 4u) {
    case 0:
        rc = cplus_snprintf_s((char *)g->body, sizeof g->body, f,
                              &t1, &t2, (int)(p->b & 0xffu), "x", (int)7);
        break;
    case 1:
        rc = cplus_printf_s(f, &t1, &t2, (int)(p->b & 0xffu), "x", (int)7);
        break;
    case 2: {
        FILE *s = fopen("/dev/null", "w");
        if (!s) return EXIT_SETUP;
        rc = cplus_fprintf_s(s, f, &t1, &t2, (int)(p->b & 0xffu), "x", (int)7);
        fclose(s);
        break;
    }
    default:
        rc = cplus_sprintf_s((char *)g->body, sizeof g->body, f,
                             &t1, &t2, (int)(p->b & 0xffu), "x", (int)7);
        break;
    }
    if (rc != -1) return EXIT_RC;
    if (errno != EINVAL) return EXIT_ERRNO;
    if (t1 != MAGIC1 || t2 != MAGIC2) return EXIT_TARGET;
    return finish(g, c, EXIT_PASS);
}

/* ---------- category: %n refused (scanf family) ---------- */
static int cat_pn_scanf(const params_t *p, gbuf_t *g, canary_t *c) {
    char f[64];
    int t1 = MAGIC1, t2 = MAGIC2;
    int rc;
    copy_fmt(PN_SCANF[p->a % (sizeof PN_SCANF / sizeof PN_SCANF[0])], f, sizeof f);

    errno = 0;
    switch (p->d % 3u) {
    case 0:
        rc = cplus_sscanf_s("1234", f, &t1, &t2, (char *)g->body, (int)7);
        break;
    case 1: {
        FILE *s = fopen("/dev/null", "r");
        if (!s) return EXIT_SETUP;
        rc = cplus_fscanf_s(s, f, &t1, &t2, (char *)g->body, (int)7);
        fclose(s);
        break;
    }
    default:
        rc = cplus_scanf_s(f, &t1, &t2, (char *)g->body, (int)7);
        break;
    }
    if (rc != -1) return EXIT_RC;
    if (errno != EINVAL) return EXIT_ERRNO;
    if (t1 != MAGIC1 || t2 != MAGIC2) return EXIT_TARGET;
    return finish(g, c, EXIT_PASS);
}

/* ---------- category: non-literal formats refused ---------- */
static int cat_nonliteral(const params_t *p, gbuf_t *g, canary_t *c) {
    static const char *const tmpl[] = { "%s", "%d", "%u", "%x", "%s", "%d", "%s" };
    volatile unsigned sel = p->a % (unsigned)(sizeof tmpl / sizeof tmpl[0]);
    const char *src = tmpl[sel]; /* volatile index => not a compile-time constant */
    char f[16];
    int t = MAGIC1;
    int rc;
    copy_fmt(src, f, sizeof f);

    errno = 0;
    switch (p->b % 7u) {
    case 0:
        rc = snprintf_s((char *)g->body, sizeof g->body, f, "x");
        break;
    case 1:
        rc = sprintf_s((char *)g->body, sizeof g->body, f, "x");
        break;
    case 2:
        rc = printf_s(f, "x");
        break;
    case 3: {
        FILE *s = fopen("/dev/null", "w");
        if (!s) return EXIT_SETUP;
        rc = fprintf_s(s, f, "x");
        fclose(s);
        break;
    }
    case 4:
        rc = scanf_s(f, &t);
        break;
    case 5: {
        FILE *s = fopen("/dev/null", "r");
        if (!s) return EXIT_SETUP;
        rc = fscanf_s(s, f, &t);
        fclose(s);
        break;
    }
    default:
        rc = sscanf_s("1234", f, &t);
        break;
    }
    if (rc != -1) return EXIT_RC;
    if (errno != EINVAL) return EXIT_ERRNO;
    if (t != MAGIC1) return EXIT_TARGET;
    return finish(g, c, EXIT_PASS);
}

/* ---------- category: one-byte-too-small bounded ---------- */
static int cat_bounded(const params_t *p, gbuf_t *g, canary_t *c) {
    unsigned cap = 8u + (p->b % 25u); /* 8..32, well within BODY_SZ */
    unsigned need = cap + 1u;         /* source is one byte too long for cap */
    char src[40];
    unsigned j;
    int rc;
    unsigned i;
    for (j = 0; j < need; ++j) src[j] = 'A';
    src[need] = '\0';

    errno = 0;
    if (p->a & 1u) {
        /* dmax claims one byte MORE than the real object capacity => refused */
        size_t claim = (size_t)BODY_SZ + 1u;
        if ((p->c & 1u) != 0u)
            rc = sprintf_s((char *)g->body, claim, "%s", src);
        else
            rc = snprintf_s((char *)g->body, claim, "%s", src);
        if (rc != -1) return EXIT_RC;
        if (errno != EINVAL) return EXIT_ERRNO;
        for (i = 0; i < BODY_SZ; ++i)
            if (g->body[i] != 'A') return EXIT_BUF;
        return finish(g, c, EXIT_PASS);
    }
    /* exact capacity: bounded truncation, NUL-terminated inside the object */
    rc = ((p->c & 1u) != 0u)
             ? sprintf_s((char *)g->body, (size_t)cap, "%s", src)
             : snprintf_s((char *)g->body, (size_t)cap, "%s", src);
    if (rc != (int)need) return EXIT_RC;
    if (g->body[cap - 1u] != '\0') return EXIT_BUF;
    for (i = 0; i + 1u < cap; ++i)
        if (g->body[i] != 'A') return EXIT_BUF;
    for (i = cap; i < BODY_SZ; ++i)
        if (g->body[i] != 'A') return EXIT_BUF;
    return finish(g, c, EXIT_PASS);
}

/* ---------- category: struct-member capacity clamped ---------- */
struct smem {
    unsigned char lead[GUARD_SZ];
    char a[16];
    unsigned char trail[GUARD_SZ];
};
static int cat_structmem(const params_t *p, gbuf_t *g, canary_t *c) {
    struct smem s;
    const char *src = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    int rc;
    unsigned i;
    (void)g;
    memset(s.lead, LEAD_BYTE, GUARD_SZ);
    memset(s.trail, TRAIL_BYTE, GUARD_SZ);
    memset(s.a, 0x5a, sizeof s.a);

    errno = 0;
    switch (p->a % 3u) {
    case 0: /* one byte over the member's real extent => refused */
        rc = snprintf_s(s.a, sizeof s.a + 1u, "%s", src);
        if (rc != -1) return EXIT_RC;
        if (errno != EINVAL) return EXIT_ERRNO;
        for (i = 0; i < sizeof s.a; ++i)
            if ((unsigned char)s.a[i] != 0x5a) return EXIT_BUF;
        break;
    case 1: /* exactly the member capacity => bounded truncation */
        rc = snprintf_s(s.a, sizeof s.a, "%s", src);
        if (rc < 0) return EXIT_RC;
        if (s.a[sizeof s.a - 1u] != '\0') return EXIT_BUF;
        break;
    default: /* smaller dmax => still bounded inside the member */
        rc = snprintf_s(s.a, 8u, "%s", src);
        if (rc < 0) return EXIT_RC;
        if (s.a[7] != '\0') return EXIT_BUF;
        for (i = 8u; i < sizeof s.a; ++i)
            if ((unsigned char)s.a[i] != 0x5a) return EXIT_BUF;
        break;
    }
    for (i = 0; i < GUARD_SZ; ++i) {
        if (s.lead[i] != LEAD_BYTE) return EXIT_CANARY;
        if (s.trail[i] != TRAIL_BYTE) return EXIT_CANARY;
    }
    return can_ok(c) ? EXIT_PASS : EXIT_CANARY;
}

/* ---------- category: NULL streams / args refused ---------- */
static int cat_nullstream(const params_t *p, gbuf_t *g, canary_t *c) {
    int t = MAGIC1;
    int rc;
    char *cp;
    size_t n;
    FILE *f;
    unsigned which = p->a % 12u;

    errno = 0;
    switch (which) {
    case 0:  rc = cplus_fprintf_s(NULL, "%s", "x");
             if (rc != -1 || errno != EINVAL) return (rc != -1) ? EXIT_RC : EXIT_ERRNO;
             break;
    case 1:  rc = cplus_fprintf_s_checked(1, NULL, "%s", "x");
             if (rc != -1 || errno != EINVAL) return (rc != -1) ? EXIT_RC : EXIT_ERRNO;
             break;
    case 2:  rc = cplus_fscanf_s(NULL, "%d", &t);
             if (rc != -1 || errno != EINVAL) return (rc != -1) ? EXIT_RC : EXIT_ERRNO;
             break;
    case 3:  rc = cplus_fscanf_s_checked(1, NULL, "%d", &t);
             if (rc != -1 || errno != EINVAL) return (rc != -1) ? EXIT_RC : EXIT_ERRNO;
             break;
    case 4:  cp = cplus_fgets_s((char *)g->body, BODY_SZ, (int)BODY_SZ, NULL);
             if (cp != NULL || errno != EINVAL) return cp ? EXIT_RC : EXIT_ERRNO;
             break;
    case 5:  cp = cplus_fgets_s_checked((char *)g->body, BODY_SZ, (int)BODY_SZ, NULL);
             if (cp != NULL || errno != EINVAL) return cp ? EXIT_RC : EXIT_ERRNO;
             break;
    case 6:  n = cplus_fread_s(g->body, BODY_SZ, 1, 1, NULL);
             if (n != 0) return EXIT_RC;
             break;
    case 7:  n = cplus_fread_s_checked(g->body, BODY_SZ, BODY_SZ, 1, 1, NULL);
             if (n != 0) return EXIT_RC;
             break;
    case 8:  n = cplus_fwrite_s(g->body, BODY_SZ, 1, 1, NULL);
             if (n != 0) return EXIT_RC;
             break;
    case 9:  n = cplus_fwrite_s_checked(g->body, BODY_SZ, BODY_SZ, 1, 1, NULL);
             if (n != 0) return EXIT_RC;
             break;
    case 10: f = cplus_fopen_s(NULL, "r");
             if (f != NULL || errno != EINVAL) return f ? EXIT_RC : EXIT_ERRNO;
             break;
    default: rc = cplus_fclose_s(NULL);
             if (rc != EOF || errno != EINVAL) return (rc != EOF) ? EXIT_RC : EXIT_ERRNO;
             break;
    }
    if (t != MAGIC1) return EXIT_TARGET;
    return finish(g, c, EXIT_PASS);
}

/* ---------- category: double close / use-after-close refused ---------- */
static int cat_doubleclose(const params_t *p, gbuf_t *g, canary_t *c) {
    char path[160];
    FILE *f, *g2;
    int c1, c2, isc, wasc, r1, r2;
    char *r3;
    size_t r4, r5;
    int t = MAGIC1;

    {
        const char *tmp = getenv("TMPDIR");
        if (!tmp || !*tmp) tmp = "/tmp";
        snprintf(path, sizeof path, "%s/bf_blue_%ld.tmp", tmp, (long)getpid());
    }
    (void)p;
    remove(path);
    f = cplus_fopen_s(path, "w+");
    if (!f) return EXIT_SETUP;

    c1 = cplus_fclose_s(f);               /* first close: ok */
    isc = cplus_io_stream_is_closed(f);   /* marker recorded */
    errno = 0;
    c2 = cplus_fclose_s(f);               /* second close: refused */
    wasc = cplus_safe_stream_was_closed(f);

    errno = 0; r1 = cplus_fprintf_s(f, "%s", "x");          /* use-after-close */
    errno = 0; r2 = cplus_fscanf_s(f, "%d", &t);            /* use-after-close */
    errno = 0; r3 = cplus_fgets_s((char *)g->body, 8, 8, f);   /* use-after-close */
    errno = 0; r4 = cplus_fread_s(g->body, BODY_SZ, 1, 1, f);   /* use-after-close */
    errno = 0; r5 = cplus_fwrite_s(g->body, BODY_SZ, 1, 1, f);  /* use-after-close */

    if (c1 != 0) return EXIT_RC;
    if (isc != 1 || wasc != 1) return EXIT_QUERY;
    if (c2 != EOF || errno != EINVAL) return (c2 != EOF) ? EXIT_RC : EXIT_ERRNO;
    (void)errno;
    if (r1 != -1 || r2 != -1 || r3 != NULL || r4 != 0 || r5 != 0) return EXIT_RC;
    if (t != MAGIC1) return EXIT_TARGET;
    remove(path);

    /* a fresh stream is not reported closed after registration */
    g2 = cplus_fopen_s(path, "w+");
    if (!g2) return EXIT_SETUP;
    if (cplus_io_stream_is_closed(g2) != 0) return EXIT_QUERY;
    if (cplus_fclose_s(g2) != 0) return EXIT_RC;
    remove(path);
    return finish(g, c, EXIT_PASS);
}

/* ---------- category: legitimate formats still accepted ---------- */
static int cat_control(const params_t *p, gbuf_t *g, canary_t *c) {
    int t = 0;
    int rc;
    unsigned i;
    switch (p->a % 5u) {
    case 0:
        rc = snprintf_s((char *)g->body, sizeof g->body, "%s", "hello");
        if (rc != 5 || strcmp((char *)g->body, "hello") != 0) return EXIT_RC;
        break;
    case 1: /* escaped %%n must be accepted and yield the literal "%n" */
        rc = snprintf_s((char *)g->body, sizeof g->body, "%%n");
        if (rc != 2 || strcmp((char *)g->body, "%n") != 0) return EXIT_BUF;
        break;
    case 2:
        rc = sscanf_s("123", "%d", &t);
        if (rc != 1 || t != 123) return EXIT_RC;
        break;
    case 3: {
        FILE *s = fopen("/dev/null", "w");
        if (!s) return EXIT_SETUP;
        rc = cplus_fprintf_s(s, "%s", "x");
        fclose(s);
        if (rc != 1) return EXIT_RC;
        break;
    }
    default: /* bounded truncation on a legitimately too-small buffer */
        rc = snprintf_s((char *)g->body, 8u, "%s", "ABCDEFGHIJ");
        if (rc != 10 || memcmp(g->body, "ABCDEFG\0", 8) != 0) return EXIT_BUF;
        break;
    }
    for (i = BODY_SZ - GUARD_SZ; i < BODY_SZ; ++i)
        if (g->body[i] != 'A') return EXIT_BUF;
    return finish(g, c, EXIT_PASS);
}

/* ---------- one case body (runs in child) ---------- */
static int run_case(uint32_t cat, const params_t *p) {
    gbuf_t g;
    canary_t c;
    int code;
    gb_init(&g);
    can_init(&c);
    switch (cat) {
    case 0:  code = cat_pn_printf(p, &g, &c); break;
    case 1:  code = cat_pn_scanf(p, &g, &c); break;
    case 2:  code = cat_nonliteral(p, &g, &c); break;
    case 3:  code = cat_bounded(p, &g, &c); break;
    case 4:  code = cat_structmem(p, &g, &c); break;
    case 5:  code = cat_nullstream(p, &g, &c); break;
    case 6:  code = cat_doubleclose(p, &g, &c); break;
    default: code = cat_control(p, &g, &c); break;
    }
    return finish(&g, &c, code);
}

static const char *exit_reason(int code) {
    switch (code) {
    case EXIT_PASS:   return "PASS";
    case EXIT_RC:     return "FAIL:return";
    case EXIT_ERRNO:  return "FAIL:errno";
    case EXIT_CANARY: return "FAIL:canary";
    case EXIT_TARGET: return "FAIL:%n-target";
    case EXIT_BUF:    return "FAIL:buffer";
    case EXIT_SETUP:  return "FAIL:setup";
    case EXIT_QUERY:  return "FAIL:stream-query";
    default:          return "FAIL:other";
    }
}

/* ---------- FNV-1a digest over fixed-seed case parameters and results ---------- */
static uint64_t fnv_bytes(uint64_t h, const void *data, size_t n) {
    const unsigned char *b = (const unsigned char *)data;
    size_t i;
    for (i = 0; i < n; ++i) {
        h ^= b[i];
        h *= UINT64_C(0x100000001b3);
    }
    return h;
}

int main(void) {
    uint32_t i;
    uint32_t passed = 0, failed = 0;
    uint32_t cat_pass[NCATS], cat_fail[NCATS];
    uint64_t digest = UINT64_C(0xcbf29ce484222325);
    uint64_t seed = SEED;
    uint32_t ncase = NCASES;

    memset(cat_pass, 0, sizeof cat_pass);
    memset(cat_fail, 0, sizeof cat_fail);

    /* Violations return -1/EINVAL so post-conditions are observable. */
    cplus_runtime_set_violation_trap(0);

    digest = fnv_bytes(digest, &seed, sizeof seed);
    digest = fnv_bytes(digest, &ncase, sizeof ncase);

    for (i = 0; i < NCASES; ++i) {
        uint32_t cat = i % NCATS;
        params_t p;
        pid_t pid;
        int status = 0, code = -1, sig = 0;

        case_params(i, &p);
        digest = fnv_bytes(digest, &i, sizeof i);
        digest = fnv_bytes(digest, &cat, sizeof cat);
        digest = fnv_bytes(digest, &p, sizeof p);

        fflush(stdout);
        pid = fork();
        if (pid < 0) {
            code = EXIT_SETUP;
        } else if (pid == 0) {
            /* child: silence stdio, bound runtime, run exactly one case */
            freopen("/dev/null", "w", stdout);
            freopen("/dev/null", "w", stderr);
            freopen("/dev/null", "r", stdin);
            /* Prime the re-bound stderr stream: its first stdio write runs an
             * isatty() probe that would otherwise clobber the EINVAL set by
             * the safety violation (errno=ENOTTY) before our assertion. */
            fputc('\n', stderr);
            fflush(stderr);
            alarm(2);
            _exit(run_case(cat, &p));
        } else {
            if (waitpid(pid, &status, 0) < 0) {
                code = EXIT_SETUP;
            } else if (WIFSIGNALED(status)) {
                sig = WTERMSIG(status);
                code = -100 - sig;
            } else if (WIFEXITED(status)) {
                code = WEXITSTATUS(status);
            }
        }

        {
            int ok = (code == EXIT_PASS);
            if (ok) { ++passed; ++cat_pass[cat]; }
            else    { ++failed; ++cat_fail[cat]; }
            digest = fnv_bytes(digest, &code, sizeof code);
            printf("case=%04u cat=%-11s code=%4d sig=%2d result=%s%s\n",
                   i, CAT_NAMES[cat], code, sig, exit_reason(code), ok ? "" : "");
        }
    }

    for (i = 0; i < NCATS; ++i)
        printf("CAT %-11s %u %u %u\n", CAT_NAMES[i],
               cat_pass[i] + cat_fail[i], cat_pass[i], cat_fail[i]);
    printf("SEED 0x%016llx CASES %u DIGEST 0x%016llx\n",
           (unsigned long long)seed, NCASES, (unsigned long long)digest);
    printf("BLUE20 format %u %u %u\n", NCASES, passed, failed);
    return failed == 0 ? 0 : 1;
}
