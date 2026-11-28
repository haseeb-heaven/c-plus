/* FUZZ10K / io -- 12,000 deterministic, forked fuzz scenarios for the checked
 * C+ stream-I/O APIs: cplus_fopen_s / cplus_fclose_s / cplus_fread_s /
 * cplus_fwrite_s / cplus_fprintf_s / cplus_fscanf_s.
 *
 * DETERMINISM
 *   One documented fixed seed (FUZZ_SEED) plus the monotonic scenario id drive
 *   a splitmix64 stream.  No clock, pid, getenv, rand(), or temp-path text ever
 *   enters a decision or the scenario log.  The api is selected round-robin
 *   (id % 6) so every API receives exactly 2,000 scenarios; every other
 *   parameter is PRNG-derived.  Re-running with the same seed and scenario
 *   count reproduces an identical scenarios.log byte-for-byte.
 *
 * ISOLATION
 *   Each scenario runs in its own fork()ed child.  The child arms alarm(5)
 *   and _Exit(99) on SIGALRM (hang), _Exit(1) on a contract/canary failure,
 *   and _Exit(0) when every assertion holds.  The parent classifies normal
 *   exit 0 as pass, exit 1 as corruption, exit 99 as hang, and any signal as
 *   a crash.  The run.sh wrapper applies a second, process-wide `timeout 600`.
 *
 * SAFETY MODEL
 *   All user buffers are heap allocations with 32-byte guard bands before and
 *   after the declared bmax region.  File fixtures live only in a bounded
 *   mktemp-created directory passed on argv; no user/home path is referenced.
 *   Read-back uses a cap of MAX_BUF+16 and fails if even one extra byte exists,
 *   so an unbounded write cannot go unnoticed.  The library's violation trap
 *   is disabled in the child so documented refusals are observable as defined
 *   return values (-1 / EOF / 0) instead of exiting 70 by design.
 *
 * OUTPUT
 *   scenarios.log (path on argv[1]) : one `S <id> <api> <case> <D|R> <ok>`
 *                                     line per scenario, then two trailer
 *                                     lines (DISTINCT, DIGEST placeholder).
 *   stdout : FUZZ10K io <scenarios> <crashes> <hangs> <corruptions> <refusals>
 *   Exit status is non-zero iff scenarios != N_SCENARIOS or any
 *   crash/hang/corruption/duplicate-tuple occurred.
 *
 * This is evidence for the tested inputs, NOT a proof of safety.
 */
#define _POSIX_C_SOURCE 200112L

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "safe_io.h"
#include "safe_format.h"
#include "safe_log.h"
#include "safe_runtime.h"

#define FUZZ_SEED 0x46555A5A3130304BULL /* ASCII "FUZZ100K" */
#define N_SCENARIOS 12000
#define MAX_BUF 4096u
#define GUARD_BYTES 32u
#define CANARY 0x5Au
#define BODY_INIT 0xA5u
#define EXIT_CHECKFAIL 1
#define EXIT_HANG 99

enum {
    API_FOPEN = 0,
    API_FCLOSE = 1,
    API_FREAD = 2,
    API_FWRITE = 3,
    API_FPRINTF = 4,
    API_FSCANF = 5,
    API_COUNT = 6
};

typedef struct {
    uint64_t id;
    unsigned api;
    unsigned ccase;
    size_t bmax;
    size_t esize;
    unsigned cmode;     /* 0 normal, 1 zero, 2 one-over, 3 huge */
    size_t payload_len;
    size_t count_val;
    uint64_t val0;
    uint64_t val1;
    uint64_t val2;
    char path[256];
    char missing[264];
} Scenario;

/* Per-child counters; the child reports refusals to the parent over a pipe. */
typedef struct {
    int refusals;
    int checks;
} Shared;

static int g_report_fd = -1;

static const char *api_name(unsigned api) {
    static const char *const names[API_COUNT] = {
        "fopen_s", "fclose_s", "fread_s", "fwrite_s", "fprintf_s", "fscanf_s"
    };
    return (api < API_COUNT) ? names[api] : "unknown";
}

static uint64_t splitmix64(uint64_t *x) {
    uint64_t z = (*x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static void gen_scenario(uint64_t id, const char *tmpdir, Scenario *s) {
    uint64_t r = FUZZ_SEED + id * 0x9E3779B97F4A7C15ULL;
    uint64_t w0 = splitmix64(&r);
    uint64_t w1 = splitmix64(&r);
    uint64_t w2 = splitmix64(&r);
    uint64_t w3 = splitmix64(&r);
    uint64_t w4 = splitmix64(&r);
    uint64_t w5 = splitmix64(&r);
    uint64_t w6 = splitmix64(&r);
    int n;

    memset(s, 0, sizeof *s);
    s->id = id;
    s->api = (unsigned)(id % API_COUNT); /* round-robin: 2000 each */
    s->ccase = (unsigned)(w0 % 16u);
    s->bmax = (size_t)(w1 % ((uint64_t)MAX_BUF + 1u));
    s->esize = (size_t)(w2 % 9u);        /* 0..8; 0 forces a refusal */
    s->cmode = (unsigned)((w2 >> 32) % 4u);
    s->payload_len = (size_t)(w3 % ((uint64_t)MAX_BUF + 1u));
    s->count_val = (size_t)(w4 % ((uint64_t)MAX_BUF + 3u));
    s->val0 = w5;
    s->val1 = w6;
    s->val2 = splitmix64(&r);

    n = snprintf(s->path, sizeof s->path, "%s/fx_%llu.bin",
                 tmpdir, (unsigned long long)id);
    if (n < 0 || (size_t)n >= sizeof s->path) s->path[0] = '\0';
    n = snprintf(s->missing, sizeof s->missing, "%s/no_such_fx_%llu.bin",
                 tmpdir, (unsigned long long)id);
    if (n < 0 || (size_t)n >= sizeof s->missing) s->missing[0] = '\0';
}

/* Fingerprint of the full decision tuple (id included) for duplicate check. */
static uint64_t scenario_fp(const Scenario *s) {
    uint64_t h = s->id ^ FUZZ_SEED;
    h ^= (uint64_t)s->api * 0x9E3779B97F4A7C15ULL;
    h ^= (uint64_t)s->ccase * 0xBF58476D1CE4E5B9ULL;
    h ^= (uint64_t)s->bmax * 0x94D049BB133111EBULL;
    h ^= (uint64_t)s->esize * 0x517CC1B727220A95ULL;
    h ^= (uint64_t)s->cmode * 0xD1B54A32D192ED03ULL;
    h ^= (uint64_t)s->payload_len * 0xA0761D6478BD642FULL;
    h ^= (uint64_t)s->count_val * 0xE7037ED1A0B428DBULL;
    h ^= s->val0 ^ s->val1 ^ s->val2;
    return splitmix64(&h);
}

/* --------------------------- guarded buffers --------------------------- */

static unsigned char *gbuf_alloc(size_t body, unsigned char **body_out) {
    unsigned char *p = (unsigned char *)malloc(body + 2u * GUARD_BYTES);
    if (!p) return NULL;
    memset(p, CANARY, GUARD_BYTES);
    memset(p + GUARD_BYTES, BODY_INIT, body);
    memset(p + GUARD_BYTES + body, CANARY, GUARD_BYTES);
    *body_out = p + GUARD_BYTES;
    return p;
}

static int gbuf_check(const unsigned char *p, size_t body) {
    size_t i;
    if (!p) return -1;
    for (i = 0; i < GUARD_BYTES; ++i) {
        if (p[i] != CANARY) return -1;
        if (p[GUARD_BYTES + body + i] != CANARY) return -1;
    }
    return 0;
}

static void fill_payload(unsigned char *p, size_t n, uint64_t seed) {
    size_t i;
    for (i = 0; i < n; ++i) p[i] = (unsigned char)(splitmix64(&seed) & 0xFFu);
}

/* Raw whole-file read with a hard cap; returns -2 if the file is larger. */
static int read_file_raw(const char *path, unsigned char *out, size_t cap,
                         size_t *out_len) {
    FILE *f;
    size_t n;
    unsigned char extra;
    f = fopen(path, "rb");
    if (!f) return -1;
    n = fread(out, 1, cap, f);
    if (n == cap && fread(&extra, 1, 1, f) == 1) {
        fclose(f);
        return -2; /* file exceeds cap: unbounded write */
    }
    *out_len = n;
    fclose(f);
    return 0;
}

static int write_file_raw(const char *path, const unsigned char *data, size_t n) {
    FILE *f = fopen(path, "wb");
    size_t w;
    if (!f) return -1;
    w = (n == 0) ? 0 : fwrite(data, 1, n, f);
    if (w != n) { fclose(f); return -1; }
    if (fclose(f) != 0) return -1;
    return 0;
}

static void on_alarm(int sig) {
    (void)sig;
    _Exit(EXIT_HANG);
}

/* ------------------------------- children ------------------------------ */

static int child_fopen(const Scenario *s, Shared *sh) {
    FILE *f = NULL;
    unsigned char expected[MAX_BUF];
    unsigned char actual[MAX_BUF + 16];
    size_t got = 0;
    int rc, pass = 0;

    switch (s->ccase % 7u) {
    case 0: /* create + double close of the real stale handle */
        f = cplus_fopen_s(s->path, "wb");
        if (!f) goto done;
        if (cplus_fclose_s(f) != 0) goto done;
        if (cplus_fclose_s(f) != EOF) goto done; /* double close refused */
        sh->refusals++;
        f = NULL;
        if (access(s->path, F_OK) != 0) goto done;
        pass = 1;
        break;
    case 1: /* read pre-existing fixture */
        fill_payload(expected, s->payload_len, s->val0);
        if (write_file_raw(s->path, expected, s->payload_len) != 0) goto done;
        f = cplus_fopen_s(s->path, "rb");
        if (!f) goto done;
        if (cplus_fclose_s(f) != 0) goto done;
        f = NULL;
        if (read_file_raw(s->path, actual, sizeof actual, &got) != 0) goto done;
        if (got != s->payload_len) goto done;
        if (memcmp(actual, expected, got) != 0) goto done;
        pass = 1;
        break;
    case 2: /* null path */
        if (cplus_fopen_s(NULL, "wb") != NULL) goto done;
        sh->refusals++;
        pass = 1;
        break;
    case 3: /* null mode */
        if (cplus_fopen_s(s->path, NULL) != NULL) goto done;
        sh->refusals++;
        pass = 1;
        break;
    case 4: /* invalid mode */
        if (cplus_fopen_s(s->path, "zz") != NULL) goto done;
        sh->refusals++;
        pass = 1;
        break;
    case 5: /* nonexistent file */
        if (cplus_fopen_s(s->missing, "rb") != NULL) goto done;
        sh->refusals++;
        pass = 1;
        break;
    case 6: /* append mode on a fresh fixture */
        f = cplus_fopen_s(s->path, "ab");
        if (!f) goto done;
        if (cplus_fclose_s(f) != 0) goto done;
        f = NULL;
        pass = 1;
        break;
    }
done:
    if (f) { (void)cplus_fclose_s(f); }
    rc = pass ? 0 : 1;
    return rc;
}

static int child_fclose(const Scenario *s, Shared *sh) {
    FILE *a = NULL, *b = NULL, *c = NULL, *d = NULL;
    FILE *raw = NULL;
    int i, pass = 0;

    switch (s->ccase % 5u) {
    case 0: /* close, then double-close the same real (stale) handle */
        a = cplus_fopen_s(s->path, "wb");
        if (!a) goto done;
        if (cplus_fclose_s(a) != 0) goto done;
        if (cplus_fclose_s(a) != EOF) goto done; /* must be refused, not UAF */
        sh->refusals++;
        a = NULL; /* closed: never touch it again */
        pass = 1;
        break;
    case 1: /* NULL stream */
        if (cplus_fclose_s(NULL) != EOF) goto done;
        sh->refusals++;
        pass = 1;
        break;
    case 2: /* stream not registered via cplus_fopen_s: close is still safe */
        raw = fopen(s->path, "wb");
        if (!raw) goto done;
        if (cplus_fclose_s(raw) != 0) goto done; /* never dereferenced after */
        raw = NULL;
        pass = 1;
        break;
    case 3: /* four live streams, then refuse four double closes */
        a = cplus_fopen_s(s->path, "wb");
        b = cplus_fopen_s(s->missing, "wb");
        c = cplus_fopen_s(s->path, "ab");
        d = cplus_fopen_s(s->missing, "ab");
        if (!a || !b || !c || !d) goto done;
        if (cplus_fclose_s(a) != 0 || cplus_fclose_s(b) != 0 ||
            cplus_fclose_s(c) != 0 || cplus_fclose_s(d) != 0) goto done;
        if (cplus_fclose_s(a) != EOF || cplus_fclose_s(b) != EOF ||
            cplus_fclose_s(c) != EOF || cplus_fclose_s(d) != EOF) goto done;
        sh->refusals += 4;
        a = b = c = d = NULL;
        pass = 1;
        break;
    case 4: /* repeated open/close cycles with distinct FILE* values */
        for (i = 0; i < 6; ++i) {
            FILE *t = cplus_fopen_s(s->path, "wb");
            if (!t) goto done;
            if (cplus_fclose_s(t) != 0) goto done;
            if (cplus_fclose_s(t) != EOF) goto done; /* double close */
            sh->refusals++;
        }
        pass = 1;
        break;
    }
done:
    /* Only still-open slots are cleaned up here; a second close of an already
     * closed handle would be a harness bug, not an API test. */
    if (a) (void)cplus_fclose_s(a);
    if (b) (void)cplus_fclose_s(b);
    if (c) (void)cplus_fclose_s(c);
    if (d) (void)cplus_fclose_s(d);
    if (raw) (void)fclose(raw);
    return pass ? 0 : 1;
}

static int child_fread(const Scenario *s, Shared *sh) {
    unsigned char payload[MAX_BUF];
    unsigned char actual[MAX_BUF + 16];
    unsigned char *base = NULL, *body = NULL;
    FILE *f = NULL;
    size_t esize = s->esize;
    size_t bmax = s->bmax;
    size_t maxelems, count, r, i, got = 0, touched;
    size_t plen = s->payload_len;
    unsigned ccase = s->ccase % 9u;
    int pass = 0, nullbuf = 0, nullstream = 0;

    if (ccase == 8) plen = 0; /* EOF case */
    fill_payload(payload, plen, s->val1);
    if (write_file_raw(s->path, payload, plen) != 0) goto done;
    if (ccase == 4) esize = 0;
    if (ccase == 7) bmax = 0;
    if (ccase == 5) nullbuf = 1;
    if (ccase == 6) nullstream = 1;

    if (!nullstream) {
        f = cplus_fopen_s(s->path, "rb");
        if (!f) goto done;
    }
    base = gbuf_alloc(bmax, &body);
    if (!base) goto done;

    maxelems = (esize > 0) ? (bmax / esize) : 0;
    switch (s->cmode) {
    case 1:  count = 0; break;
    case 2:  count = maxelems + 1u; break;
    case 3:  count = (size_t)-1; break;
    default: count = s->count_val % (maxelems + 2u); break;
    }
    if (ccase == 2) count = maxelems + 1u;
    if (ccase == 3) count = (size_t)-1;
    if (ccase == 8) count = maxelems + 1u; /* bigger than an empty file */

    r = cplus_fread_s(nullbuf ? NULL : body, bmax, esize, count,
                      nullstream ? NULL : f);
    sh->checks++;

    if (esize == 0 || nullbuf || nullstream) {
        if (r != 0) goto done;
        sh->refusals++;
    } else {
        if (r > maxelems) goto done;
        if (r * esize > bmax) goto done;
        if (r * esize > plen) goto done;
        /* stdio may store a partial final item when EOF splits an element;
         * that item is not counted in r but its bytes are still written. */
        touched = (count > (size_t)-1 / esize) ? bmax : count * esize;
        if (touched > bmax) touched = bmax;
        {
            size_t eof_touch = ((plen + esize - 1u) / esize) * esize;
            if (eof_touch < touched) touched = eof_touch;
        }
        for (i = touched; i < bmax; ++i) {
            if (body[i] != BODY_INIT) goto done; /* write past the readable region */
        }
    }
    if (gbuf_check(base, bmax) != 0) goto done;

    if (f) {
        if (cplus_fclose_s(f) != 0) goto done;
        f = NULL;
    }
    if (read_file_raw(s->path, actual, sizeof actual, &got) != 0) goto done;
    if (got != plen) goto done;
    if (memcmp(actual, payload, got) != 0) goto done; /* reads must not alter file */
    pass = 1;
done:
    if (f) (void)cplus_fclose_s(f);
    free(base);
    return pass ? 0 : 1;
}

static int child_fwrite(const Scenario *s, Shared *sh) {
    unsigned char *base = NULL, *body = NULL;
    unsigned char actual[MAX_BUF + 16];
    FILE *f = NULL;
    size_t esize = s->esize;
    size_t bmax = s->bmax;
    size_t maxelems, count, w, i, got = 0;
    unsigned ccase = s->ccase % 8u;
    int pass = 0, nullbuf = 0, nullstream = 0;

    if (ccase == 4) esize = 0;
    if (ccase == 7) bmax = 0;
    if (ccase == 5) nullbuf = 1;
    if (ccase == 6) nullstream = 1;

    if (!nullstream) {
        f = cplus_fopen_s(s->path, "wb");
        if (!f) goto done;
    }
    base = gbuf_alloc(bmax, &body);
    if (!base) goto done;
    fill_payload(body, bmax, s->val2);

    maxelems = (esize > 0) ? (bmax / esize) : 0;
    switch (s->cmode) {
    case 1:  count = 0; break;
    case 2:  count = maxelems + 1u; break;
    case 3:  count = (size_t)-1; break;
    default: count = s->count_val % (maxelems + 2u); break;
    }

    w = cplus_fwrite_s(nullbuf ? NULL : body, bmax, esize, count,
                       nullstream ? NULL : f);
    sh->checks++;

    if (esize == 0 || nullbuf || nullstream) {
        if (w != 0) goto done;
        sh->refusals++;
    } else {
        if (w > maxelems) goto done;
        if (w * esize > bmax) goto done;
    }
    if (gbuf_check(base, bmax) != 0) goto done;

    if (f) {
        if (cplus_fclose_s(f) != 0) goto done;
        f = NULL;
    }
    if (nullstream) {
        /* No stream was opened, so no fixture exists to verify. */
    } else {
        if (read_file_raw(s->path, actual, sizeof actual, &got) != 0) goto done;
        if (got != ((esize == 0 || nullbuf) ? 0u : w * esize)) goto done;
        for (i = 0; i < got; ++i) {
            if (actual[i] != body[i]) goto done; /* file integrity / exact prefix */
        }
    }
    pass = 1;
done:
    if (f) (void)cplus_fclose_s(f);
    free(base);
    return pass ? 0 : 1;
}

static int child_fprintf(const Scenario *s, Shared *sh) {
    unsigned char *gbase = NULL, *gbody = NULL;
    unsigned char actual[MAX_BUF + 16];
    FILE *f = NULL;
    int value = (int)(s->val0 & 0x7FFFFFFFu);
    unsigned uvalue = (unsigned)(s->val1 & 0xFFFFFFFFu);
    long long llvalue = (long long)(s->val2 >> 1);
    int dummy = 0;
    int n = -1, expect = -1;
    size_t got = 0;
    unsigned ccase = s->ccase % 9u;
    int pass = 0;

    gbase = gbuf_alloc(128, &gbody);
    if (!gbase) goto done;
    f = cplus_fopen_s(s->path, "wb");
    if (!f) goto done;

    switch (ccase) {
    case 0:
        expect = snprintf((char *)gbody, 128, "%d", value);
        n = cplus_fprintf_s(f, "%d", value);
        break;
    case 1:
        expect = snprintf((char *)gbody, 128, "%u", uvalue);
        n = cplus_fprintf_s(f, "%u", uvalue);
        break;
    case 2:
        expect = snprintf((char *)gbody, 128, "%08x", uvalue);
        n = cplus_fprintf_s(f, "%08x", uvalue);
        break;
    case 3:
        expect = snprintf((char *)gbody, 128, "%lld", llvalue);
        n = cplus_fprintf_s(f, "%lld", llvalue);
        break;
    case 4:
        expect = snprintf((char *)gbody, 128, "%s", "abcdef");
        n = cplus_fprintf_s(f, "%s", "abcdef");
        break;
    case 5:
        expect = snprintf((char *)gbody, 128, "%%");
        n = cplus_fprintf_s(f, "%%");
        break;
    case 6: /* forbidden %n */
        n = cplus_fprintf_s(f, "%n", &dummy);
        expect = -1;
        break;
    case 7: /* null format */
        n = cplus_fprintf_s(f, NULL);
        expect = -1;
        break;
    case 8: /* null stream */
        n = cplus_fprintf_s(NULL, "%d", value);
        expect = -1;
        break;
    }
    sh->checks++;

    /* Flush/close before reading the fixture back; otherwise buffered output
     * would still be in the FILE* and the read-back would be a harness bug. */
    if (cplus_fclose_s(f) != 0) goto done;
    f = NULL;

    if (n != expect) goto done;
    if (expect >= 0) {
        size_t want = (size_t)expect;
        if (want >= 128) goto done;
        if (read_file_raw(s->path, actual, sizeof actual, &got) != 0) goto done;
        if (got != want) goto done;
        if (memcmp(actual, gbody, want) != 0) goto done;
    } else {
        sh->refusals++;
        if (read_file_raw(s->path, actual, sizeof actual, &got) != 0) goto done;
        if (got != 0) goto done; /* refused formatting wrote nothing */
    }
    if (gbuf_check(gbase, 128) != 0) goto done;
    pass = 1;
done:
    if (f) (void)cplus_fclose_s(f);
    free(gbase);
    (void)dummy;
    return pass ? 0 : 1;
}

typedef struct {
    unsigned char pre[32];
    union {
        int i;
        unsigned u;
        long long ll;
        char s[64];
    } v;
    unsigned char post[32];
} ScanDst;

static int scan_canary_ok(const ScanDst *d) {
    size_t i;
    for (i = 0; i < sizeof d->pre; ++i) {
        if (d->pre[i] != CANARY || d->post[i] != CANARY) return 0;
    }
    return 1;
}

static int child_fscanf(const Scenario *s, Shared *sh) {
    ScanDst d;
    unsigned char text[96];
    unsigned char actual[MAX_BUF + 16];
    FILE *f = NULL;
    int value = (int)(s->val0 & 0x7FFFFFFFu);
    unsigned uvalue = (unsigned)(s->val1 & 0xFFFFFFFFu);
    long long llvalue = (long long)(s->val2 >> 1);
    int n = -1, expect = -1;
    size_t textlen = 0, got = 0;
    unsigned ccase = s->ccase % 9u;
    int pass = 0;

    memset(&d, 0, sizeof d);
    memset(d.pre, CANARY, sizeof d.pre);
    memset(d.post, CANARY, sizeof d.post);
    memset(&d.v, BODY_INIT, sizeof d.v);

    switch (ccase) {
    case 0: textlen = (size_t)snprintf((char *)text, sizeof text, "%d", value); break;
    case 1: textlen = (size_t)snprintf((char *)text, sizeof text, "%u", uvalue); break;
    case 2: textlen = (size_t)snprintf((char *)text, sizeof text, "abcdef"); break;
    case 3: textlen = (size_t)snprintf((char *)text, sizeof text, "%lld", llvalue); break;
    case 4: textlen = (size_t)snprintf((char *)text, sizeof text, "abc"); break;
    case 5: textlen = (size_t)snprintf((char *)text, sizeof text, "123"); break;
    case 6: textlen = (size_t)snprintf((char *)text, sizeof text, "%d", value); break;
    case 7: textlen = (size_t)snprintf((char *)text, sizeof text, "%d", value); break;
    case 8: textlen = 0; break; /* empty file -> EOF */
    }
    if (write_file_raw(s->path, text, textlen) != 0) goto done;

    if (ccase != 7) {
        f = cplus_fopen_s(s->path, "rb");
        if (!f) goto done;
    }

    switch (ccase) {
    case 0:
        n = cplus_fscanf_s(f, "%d", &d.v.i);
        expect = (n == 1 && d.v.i == value) ? 1 : -2;
        break;
    case 1:
        n = cplus_fscanf_s(f, "%u", &d.v.u);
        expect = (n == 1 && d.v.u == uvalue) ? 1 : -2;
        break;
    case 2:
        n = cplus_fscanf_s(f, "%3s", d.v.s);
        expect = (n == 1 && memcmp(d.v.s, "abc", 3) == 0 && d.v.s[3] == '\0'
                  && d.v.s[4] == (char)BODY_INIT) ? 1 : -2;
        break;
    case 3:
        n = cplus_fscanf_s(f, "%lld", &d.v.ll);
        expect = (n == 1 && d.v.ll == llvalue) ? 1 : -2;
        break;
    case 4: /* unbounded %s must be refused */
        n = cplus_fscanf_s(f, "%s", d.v.s);
        expect = -1;
        break;
    case 5: /* %n must be refused */
        n = cplus_fscanf_s(f, "%n", &d.v.i);
        expect = -1;
        break;
    case 6: /* null format */
        n = cplus_fscanf_s(f, NULL);
        expect = -1;
        break;
    case 7: /* null stream */
        n = cplus_fscanf_s(NULL, "%d", &d.v.i);
        expect = -1;
        break;
    case 8: /* EOF on empty file is a defined result */
        n = cplus_fscanf_s(f, "%d", &d.v.i);
        expect = -1;
        break;
    }
    sh->checks++;

    if (n != expect) goto done;
    if (expect < 0) {
        if (ccase != 8) sh->refusals++;
        if (ccase >= 4 && ccase <= 7) {
            /* Refused formats/streams must never touch the destination. */
            if (d.v.i != (int)0xA5A5A5A5u) goto done;
            if (d.v.u != 0xA5A5A5A5u) goto done;
            if (d.v.ll != (long long)0xA5A5A5A5A5A5A5A5ULL) goto done;
            if (d.v.s[0] != (char)BODY_INIT || d.v.s[63] != (char)BODY_INIT) goto done;
        }
    }
    if (!scan_canary_ok(&d)) goto done;
    if (f) {
        if (cplus_fclose_s(f) != 0) goto done;
        f = NULL;
    }
    if (read_file_raw(s->path, actual, sizeof actual, &got) != 0) goto done;
    if (got != textlen) goto done;
    if (memcmp(actual, text, got) != 0) goto done; /* scans must not alter file */
    pass = 1;
done:
    if (f) (void)cplus_fclose_s(f);
    return pass ? 0 : 1;
}

static int child_run(const Scenario *s, Shared *sh) {
    signal(SIGALRM, on_alarm);
    alarm(5);
    (void)cplus_runtime_set_violation_trap(0);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    sh->refusals = 0;
    sh->checks = 0;

    switch (s->api) {
    case API_FOPEN:   return child_fopen(s, sh);
    case API_FCLOSE:  return child_fclose(s, sh);
    case API_FREAD:   return child_fread(s, sh);
    case API_FWRITE:  return child_fwrite(s, sh);
    case API_FPRINTF: return child_fprintf(s, sh);
    case API_FSCANF:  return child_fscanf(s, sh);
    default:          return 1;
    }
}

/* --------------------------------- parent ------------------------------ */

static int fingerprint_seen(uint64_t *set, size_t cap, uint64_t fp) {
    size_t i = (size_t)(fp & (uint64_t)(cap - 1u));
    size_t n;
    for (n = 0; n < cap; ++n) {
        if (set[i] == 0) { set[i] = fp; return 0; }
        if (set[i] == fp) return 1;
        i = (i + 1u) & (cap - 1u);
    }
    return 1;
}

int main(int argc, char **argv) {
    const char *logpath;
    const char *tmpdir;
    FILE *log;
    Shared sh;
    int pipefd[2];
    uint64_t *fps;
    const size_t fp_cap = 32768u; /* power of two > 12000 */
    uint64_t id;
    unsigned long crashes = 0, hangs = 0, corruptions = 0, refusals = 0;
    unsigned long duplicates = 0, nrun = 0;
    int rc = 0;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <scenarios.log> <bounded-temp-dir>\n", argv[0]);
        return 2;
    }
    logpath = argv[1];
    tmpdir = argv[2];
    if (tmpdir[0] != '/') {
        fprintf(stderr, "temp dir must be an absolute path\n");
        return 2;
    }

    log = fopen(logpath, "wb");
    if (!log) { perror("scenarios.log"); return 2; }
    fps = (uint64_t *)calloc(fp_cap, sizeof *fps);
    if (!fps) {
        fprintf(stderr, "allocation failed\n");
        return 2;
    }

    for (id = 1; id <= N_SCENARIOS; ++id) {
        Scenario s;
        uint64_t fp;
        pid_t pid;
        int status = 0, dup, outcome, ok, wait_rc;
        int reported = 0;
        unsigned long child_refusals;

        gen_scenario(id, tmpdir, &s);
        if (s.path[0] == '\0' || s.missing[0] == '\0') {
            corruptions++;
            fprintf(log, "S %llu pathgen 0 X 0\n", (unsigned long long)id);
            continue;
        }
        fp = scenario_fp(&s);
        dup = fingerprint_seen(fps, fp_cap, fp);
        if (dup) { duplicates++; corruptions++; }

        sh.refusals = 0;
        sh.checks = 0;
        if (pipe(pipefd) != 0) {
            corruptions++;
            outcome = 'X';
            ok = 0;
            child_refusals = 0;
            goto logged;
        }
        pid = fork();
        if (pid < 0) {
            close(pipefd[0]);
            close(pipefd[1]);
            corruptions++;
            outcome = 'X';
            ok = 0;
            child_refusals = 0;
        } else if (pid == 0) {
            int cr, v;
            const unsigned char *p;
            size_t left;
            close(pipefd[0]);
            g_report_fd = pipefd[1];
            cr = child_run(&s, &sh);
            v = sh.refusals;
            p = (const unsigned char *)&v;
            left = sizeof v;
            while (left > 0) {
                ssize_t k = write(g_report_fd, p, left);
                if (k <= 0) break;
                p += k;
                left -= (size_t)k;
            }
            _Exit(cr == 0 ? 0 : EXIT_CHECKFAIL);
        } else {
            ssize_t rd;
            close(pipefd[1]);
            g_report_fd = -1;
            do { wait_rc = waitpid(pid, &status, 0); } while (wait_rc < 0 && errno == EINTR);
            rd = read(pipefd[0], &reported, sizeof reported);
            close(pipefd[0]);
            if (wait_rc != pid) {
                corruptions++;
                outcome = 'X';
                ok = 0;
                child_refusals = 0;
            } else if (WIFSIGNALED(status)) {
                crashes++;
                outcome = 'C';
                ok = 0;
                child_refusals = 0;
            } else if (WIFEXITED(status) && WEXITSTATUS(status) == EXIT_HANG) {
                hangs++;
                outcome = 'H';
                ok = 0;
                child_refusals = 0;
            } else if (WIFEXITED(status) && WEXITSTATUS(status) == EXIT_CHECKFAIL) {
                corruptions++;
                outcome = 'X';
                ok = 0;
                child_refusals = 0;
            } else if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                if (rd != (ssize_t)sizeof reported || reported < 0) {
                    corruptions++;
                    outcome = 'X';
                    ok = 0;
                    child_refusals = 0;
                } else {
                    child_refusals = (unsigned long)reported;
                    refusals += child_refusals;
                    outcome = child_refusals ? 'R' : 'D';
                    ok = dup ? 0 : 1;
                }
            } else {
                corruptions++;
                outcome = 'X';
                ok = 0;
                child_refusals = 0;
            }
        }
logged:
        nrun++;
        if (fprintf(log, "S %llu %s %u %c %d %lu\n",
                    (unsigned long long)id, api_name(s.api), s.ccase,
                    outcome, ok, child_refusals) < 0) {
            corruptions++;
        }
        if (fflush(log) != 0) corruptions++;
        if (unlink(s.path) != 0 && errno != ENOENT) { /* keep temp dir bounded */ }
    }

    fprintf(log, "FUZZ10K_DISTINCT %lu\n",
            (unsigned long)(nrun - duplicates));
    fprintf(log, "FUZZ10K_DIGEST computed-by-run.sh\n");
    if (fclose(log) != 0) corruptions++;
    free(fps);

    if (nrun != N_SCENARIOS || crashes || hangs || corruptions) rc = 1;
    printf("FUZZ10K io %lu %lu %lu %lu %lu\n",
           nrun, crashes, hangs, corruptions, refusals);
    return rc;
}
