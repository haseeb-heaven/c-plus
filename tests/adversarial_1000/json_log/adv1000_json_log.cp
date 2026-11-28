/* ADV1000 / json_log -- 1000 deterministic adversarial scenarios.
 *
 * Scope: stdlib/json.hp (json ctor, json_input_within_budget,
 * json_parse_budget, read_size/value_count, views, builder) and
 * stdlib/log.hp (levels, sinks, write/info/warn/failure).
 *
 * Determinism: every scenario id 0..999 derives all parameters from a fixed
 * SplitMix64 seed and its own id.  No wall-clock, PID or address is used in a
 * scenario result.  First 16 ids are pinned breakers/boundaries; the rest are
 * PRNG-generated but reproducible.
 *
 * Sentinel integrity: each case arms four byte guards around the JSON and log
 * message work buffers, then checks them after the API call.  A single changed
 * guard byte fails the case.
 *
 * Output: one canonical line "S <id> <api> <case> <result> <sentinel_ok>" per
 * scenario, then "ADV1000 json_log 1000 <refused> <sentinel_ok> <failures>".
 * One corresponding log line per scenario is written through a real log_sink to
 * <log-path> (default results/scenarios.log), supplied on the command line.
 *
 * Build (from repo root):
 *   ./build/c+ build tests/adversarial_1000/json_log/adv1000_json_log.cp \
 *     --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson \
 *     -o tests/adversarial_1000/json_log/results/adv1000_json_log
 * Run:  timeout 120 <bin> <log-path>
 *
 * NOTE: this is evidence, not proof.  It exercises the listed contracts with
 * deterministic inputs; it cannot establish the absence of all defects.
 */
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"
#include "stdlib/log.hp"

#define ADV_N 1000
#define ADV_SEED 0xC0FFEE123456789ULL
#define ADV_GUARD 32
#define ADV_JSON_CAP ((size_t)2 * 1024 * 1024 + 8)
#define ADV_MSG_CAP 4200

/* ---- outcome tokens ---------------------------------------------------- */
#define RES_OK 0
#define RES_REFUSED 1
#define RES_DEFINED 2
#define RES_FAIL 3

static const char *adv_res_name(int r) {
    if (r == RES_OK)
        return "ok";
    if (r == RES_REFUSED)
        return "refused";
    if (r == RES_DEFINED)
        return "defined";
    return "FAIL";
}

/* ---- scenario kinds ---------------------------------------------------- */
#define K_JSON_NULL 0
#define K_JSON_BUDGET_NULL 1
#define K_JSON_BUDGET_ZERO_LEN 2
#define K_JSON_BUDGET_SIZE_MAX 3
#define K_JSON_TRUNC 4
#define K_JSON_DEEP_DEFAULT 5
#define K_JSON_DEEP_OVERRIDE 6
#define K_JSON_LONG 7
#define K_JSON_MALFORMED 8
#define K_JSON_BUDGET_FN 9
#define K_JSON_BUDGET_NULLFN 10
#define K_JSON_READSIZE 11
#define K_JSON_VALCOUNT 12
#define K_JSON_VIEW 13
#define K_JSON_BUILDER_NULL 14
#define K_LOG_NULL_STREAM 15
#define K_LOG_OPEN_NULL 16
#define K_LOG_PARSE_NULL 17
#define K_LOG_MSG 18
#define K_LOG_API_NULLMSG 19
#define K_LOG_LEVEL_NAME 20
#define K_COUNT 21

/* ---- work buffers and guards ------------------------------------------- */
static unsigned char g_jpre[ADV_GUARD];
static unsigned char g_jbuf[ADV_JSON_CAP];
static unsigned char g_jpost[ADV_GUARD];
static unsigned char g_mpre[ADV_GUARD];
static char g_mbuf[ADV_MSG_CAP];
static unsigned char g_mpost[ADV_GUARD];
static unsigned char g_expect[ADV_GUARD];

static log_sink *g_sink;
static log_sink *g_probe;

static void adv_put(char *p, size_t i, char c) {
    __builtin_memcpy(p + i, &c, 1);
}

static char adv_get(const char *p, size_t i) {
    char c;
    __builtin_memcpy(&c, p + i, 1);
    return c;
}

static void adv_arm(void) {
    __builtin_memset(g_jpre, 0xA5, ADV_GUARD);
    __builtin_memset(g_jpost, 0x5A, ADV_GUARD);
    __builtin_memset(g_mpre, 0x3C, ADV_GUARD);
    __builtin_memset(g_mpost, 0xC3, ADV_GUARD);
    __builtin_memset(g_jbuf, 0, 64);
    __builtin_memset(g_mbuf, 0, 64);
}

static int adv_sentinel_ok(void) {
    int ok = 1;
    __builtin_memset(g_expect, 0xA5, ADV_GUARD);
    if (__builtin_memcmp(g_jpre, g_expect, ADV_GUARD) != 0)
        ok = 0;
    __builtin_memset(g_expect, 0x5A, ADV_GUARD);
    if (__builtin_memcmp(g_jpost, g_expect, ADV_GUARD) != 0)
        ok = 0;
    __builtin_memset(g_expect, 0x3C, ADV_GUARD);
    if (__builtin_memcmp(g_mpre, g_expect, ADV_GUARD) != 0)
        ok = 0;
    __builtin_memset(g_expect, 0xC3, ADV_GUARD);
    if (__builtin_memcmp(g_mpost, g_expect, ADV_GUARD) != 0)
        ok = 0;
    return ok;
}

/* ---- deterministic parameters ----------------------------------------- */
static unsigned long long adv_mix(unsigned long long z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static void adv_params(int id, unsigned long long *r1, unsigned long long *r2,
                       unsigned long long *r3) {
    unsigned long long k = (unsigned long long)id;
    *r1 = adv_mix(ADV_SEED + k * 0x9E3779B97F4A7C15ULL);
    *r2 = adv_mix(ADV_SEED ^ (k * 0xD1B54A32D192ED03ULL));
    /* Extra id-dependent mix removes a parameter-tuple collision between two
     * otherwise identical budget scenarios in the first draft of this suite. */
    *r3 = adv_mix(adv_mix(ADV_SEED + k * 0x2545F4914F6CDD1DULL + 0x1234567ULL) +
                  k * 0xD6E8FEB86659FD93ULL);
}

static const size_t ADV_LEN_TBL[] = {
    0, 1, 2, 3, 4, 8, 16, 64, 255, 256, 1024, 4096, 16384,
    65536, 262144, 1048576, 2097152
};
#define ADV_LEN_TBL_N 17

static size_t adv_len_from(unsigned long long r) {
    if (((r >> 11) & 3ULL) == 0ULL)
        return (size_t)(r % 2097153ULL);
    return ADV_LEN_TBL[r % ADV_LEN_TBL_N];
}

static const size_t ADV_DEPTH_TBL[] = {
    0, 1, 2, 3, 8, 16, 64, 256, 1024, 4096, 16384, 16385, 20000
};
#define ADV_DEPTH_TBL_N 13

static size_t adv_depth_from(unsigned long long r) {
    if (((r >> 13) & 3ULL) == 0ULL)
        return (size_t)(r % 20001ULL);
    return ADV_DEPTH_TBL[r % ADV_DEPTH_TBL_N];
}

/* Budget table: index 0 is the header default (16384); others are overrides. */
static const size_t ADV_BUDGET_TBL[] = {
    (size_t)16384, (size_t)0, (size_t)1, (size_t)2, (size_t)8, (size_t)16,
    (size_t)64, (size_t)1024, (size_t)16384, (size_t)16385, (size_t)20000,
    (size_t)25000, SIZE_MAX
};
#define ADV_BUDGET_TBL_N 13

static size_t adv_budget_from(unsigned long long r) {
    return ADV_BUDGET_TBL[r % ADV_BUDGET_TBL_N];
}

/* ---- document builders ------------------------------------------------- */
/* Balanced nested arrays: d '[' + '0' + d ']' -> length 2d+1. */
static size_t adv_build_deep(size_t depth) {
    size_t pos = 0;
    size_t i;
    for (i = 0; i < depth; i++)
        adv_put((char *)g_jbuf, pos++, '[');
    adv_put((char *)g_jbuf, pos++, '0');
    for (i = 0; i < depth; i++)
        adv_put((char *)g_jbuf, pos++, ']');
    adv_put((char *)g_jbuf, pos, '\0');
    return pos;
}

/* Valid JSON string of exactly `len` bytes (len >= 2), NUL at index len. */
static size_t adv_build_long_string(size_t len) {
    size_t i;
    if (len < 2) {
        adv_put((char *)g_jbuf, 0, '[');
        adv_put((char *)g_jbuf, 1, '\0');
        return len;
    }
    adv_put((char *)g_jbuf, 0, '"');
    for (i = 1; i + 1 < len; i++)
        adv_put((char *)g_jbuf, i, 'a');
    adv_put((char *)g_jbuf, len - 1, '"');
    adv_put((char *)g_jbuf, len, '\0');
    return len;
}

/* Valid flat integer array of about `target` bytes. */
static size_t adv_build_array(size_t target) {
    size_t len = target;
    size_t i;
    if (len < 3)
        len = 3;
    if (((len - 2) & 1u) == 0u)
        len--;
    adv_put((char *)g_jbuf, 0, '[');
    for (i = 1; i + 1 < len; i++)
        adv_put((char *)g_jbuf, i, (i & 1u) ? '1' : ',');
    adv_put((char *)g_jbuf, len - 1, ']');
    adv_put((char *)g_jbuf, len, '\0');
    return len;
}

static void adv_fill_msg(size_t len) {
    size_t i;
    for (i = 0; i < len; i++)
        adv_put(g_mbuf, i, (char)('A' + (int)(i % 26)));
    adv_put(g_mbuf, len, '\0');
}

/* ---- case executor ----------------------------------------------------- */
struct adv_outcome {
    int observed;
    int sentinel;
};

static struct adv_outcome adv_exec(int kind, unsigned long long r1,
                                   unsigned long long r2,
                                   unsigned long long r3, int raw,
                                   const char **api, const char **case_name) {
    struct adv_outcome out;
    error e = error();
    /* raw = 1 for the pinned breakers: r1 is the literal depth/length and r2
     * is a literal budget-table index.  Generated cases use PRNG mappings. */
    size_t depth = raw ? (size_t)r1 : adv_depth_from(r1);
    size_t len = raw ? (size_t)r1 : adv_len_from(r1);
    size_t budget = raw ? ADV_BUDGET_TBL[r2 % ADV_BUDGET_TBL_N]
                        : adv_budget_from(r2);
    int variant = (int)(r3 & 3ULL);
    int expect_ok;
    int checks;

    *api = "json";
    *case_name = "case";
    out.sentinel = 0;
    out.observed = RES_FAIL;
    adv_arm();

    switch (kind) {
    case K_JSON_NULL: {
        json d = json(NULL, &e);
        *case_name = "ctor_null";
        /* Documented: NULL source is a silent no-op (invalid doc, no error). */
        if (!d.valid() && e.ok())
            out.observed = RES_REFUSED;
        d.drop();
        break;
    }
    case K_JSON_BUDGET_NULL: {
        json d = json_parse_budget((const char *)0, 16, budget, &e);
        *case_name = "budget_null_text";
        if (!d.valid() && e.failed_now())
            out.observed = RES_REFUSED;
        d.drop();
        break;
    }
    case K_JSON_BUDGET_ZERO_LEN: {
        size_t used = adv_build_array(16);
        json d = json_parse_budget((const char *)g_jbuf, 0, budget, &e);
        *case_name = "budget_zero_len";
        (void)used;
        if (!d.valid() && e.failed_now())
            out.observed = RES_REFUSED;
        d.drop();
        break;
    }
    case K_JSON_BUDGET_SIZE_MAX: {
        size_t used = adv_build_array(16);
        json d = json_parse_budget((const char *)g_jbuf, SIZE_MAX, budget, &e);
        *case_name = "budget_size_max_len";
        (void)used;
        if (!d.valid() && e.failed_now())
            out.observed = RES_REFUSED;
        d.drop();
        break;
    }
    case K_JSON_TRUNC: {
        size_t doc_len;
        size_t cut;
        char saved;
        json d;
        depth = adv_depth_from(r1);
        if (depth < 1)
            depth = 1;
        doc_len = adv_build_deep(depth);
        /* Strictly less than doc_len, otherwise the "truncated" input would
         * in fact be the complete document and parse successfully. */
        if (doc_len < 2)
            doc_len = 2;
        cut = 1 + (size_t)(r2 % (unsigned long long)(doc_len - 1));
        *case_name = "truncated";
        if (variant == 0) {
            /* Explicit-length parse of a truncated prefix. */
            d = json_parse_budget((const char *)g_jbuf, cut, budget, &e);
            if (!d.valid() && e.failed_now())
                out.observed = RES_REFUSED;
        } else {
            /* NUL-terminated truncation through the json() constructor. */
            saved = adv_get((const char *)g_jbuf, cut);
            adv_put((char *)g_jbuf, cut, '\0');
            d = json((const char *)g_jbuf, &e);
            adv_put((char *)g_jbuf, cut, saved);
            if (!d.valid() && e.failed_now())
                out.observed = RES_REFUSED;
        }
        d.drop();
        break;
    }
    case K_JSON_DEEP_DEFAULT: {
        size_t used = adv_build_deep(depth);
        json d = json((const char *)g_jbuf, &e);
        (void)used;
        *case_name = "deep_default_budget";
        expect_ok = (depth <= (size_t)16384);
        if (expect_ok) {
            size_t rs = d.read_size();
            size_t vc = d.value_count();
            if (d.valid() && e.ok() && rs > 0 && vc > 0)
                out.observed = RES_OK;
        } else {
            if (!d.valid() && e.failed_now() && d.read_size() == 0 &&
                d.value_count() == 0)
                out.observed = RES_REFUSED;
        }
        d.drop();
        break;
    }
    case K_JSON_DEEP_OVERRIDE: {
        size_t used = adv_build_deep(depth);
        json d = json_parse_budget((const char *)g_jbuf, used, budget, &e);
        (void)used;
        *case_name = "deep_budget_override";
        expect_ok = (depth <= budget);
        if (expect_ok) {
            if (d.valid() && e.ok() && d.read_size() > 0 &&
                d.value_count() > 0)
                out.observed = RES_OK;
        } else {
            if (!d.valid() && e.failed_now() && d.read_size() == 0 &&
                d.value_count() == 0)
                out.observed = RES_REFUSED;
        }
        d.drop();
        break;
    }
    case K_JSON_LONG: {
        size_t used = adv_build_long_string(len);
        json d = json_parse_budget((const char *)g_jbuf, used, budget, &e);
        *case_name = "length_0_to_2MiB";
        if (used < 2) {
            if (!d.valid() && e.failed_now())
                out.observed = RES_REFUSED;
        } else {
            if (d.valid() && e.ok() && d.read_size() == used &&
                d.value_count() >= 1)
                out.observed = RES_OK;
        }
        d.drop();
        break;
    }
    case K_JSON_MALFORMED: {
        size_t used = adv_build_deep(depth);
        char target;
        if (used == 0)
            break;
        target = adv_get((const char *)g_jbuf, 0);
        if (target == '[')
            adv_put((char *)g_jbuf, 0, 'x');
        else
            adv_put((char *)g_jbuf, 0, 'x');
        {
            json d = json_parse_budget((const char *)g_jbuf, used, budget, &e);
            *case_name = "malformed_token";
            if (!d.valid() && e.failed_now())
                out.observed = RES_REFUSED;
            d.drop();
        }
        break;
    }
    case K_JSON_BUDGET_FN: {
        size_t used = adv_build_deep(depth);
        *case_name = "input_within_budget";
        checks = json_input_within_budget((const char *)g_jbuf, used, budget, &e);
        expect_ok = (depth <= budget && used > 0 &&
                     used < (size_t)(SIZE_MAX - 64));
        if (expect_ok) {
            if (checks == 1 && e.ok())
                out.observed = RES_OK;
        } else {
            if (checks == 0 && e.failed_now())
                out.observed = RES_REFUSED;
        }
        break;
    }
    case K_JSON_BUDGET_NULLFN: {
        *case_name = "input_within_budget_null";
        checks = json_input_within_budget((const char *)0, 8, budget, &e);
        if (checks == 0 && e.failed_now())
            out.observed = RES_REFUSED;
        break;
    }
    case K_JSON_READSIZE: {
        size_t used = adv_build_long_string(len);
        json d = json_parse_budget((const char *)g_jbuf, used, budget, &e);
        *case_name = "read_size";
        if (used < 2) {
            if (!d.valid() && d.read_size() == 0)
                out.observed = RES_REFUSED;
        } else {
            if (d.valid() && d.read_size() == used)
                out.observed = RES_OK;
        }
        d.drop();
        break;
    }
    case K_JSON_VALCOUNT: {
        size_t used = adv_build_array(len);
        json d = json_parse_budget((const char *)g_jbuf, used, budget, &e);
        *case_name = "value_count";
        if (d.valid()) {
            if (d.value_count() >= 1 && d.read_size() > 0)
                out.observed = RES_OK;
        } else {
            if (d.value_count() == 0 && d.read_size() == 0)
                out.observed = RES_REFUSED;
        }
        d.drop();
        break;
    }
    case K_JSON_VIEW: {
        static const char *FIX = "{\"a\":7,\"b\":\"str\",\"c\":[1,2]}";
        json d = json(FIX, &e);
        *case_name = "view_accessors";
        if (d.valid()) {
            json_view root = d.root();
            json_view a = root.member("a");
            json_view none = root.member("nope");
            json_view nullkey = root.member((const char *)0);
            json_view arr = root.member("c");
            json_view far = arr.at(99);
            int got = -1;
            int rc = a.get_int(&got, &e);
            if (root.is_object() && a.valid() && !none.valid() &&
                !nullkey.valid() && arr.is_array() && arr.size() == 2 &&
                rc == STDLIB_OK && got == 7 && far.valid() == 0)
                out.observed = RES_DEFINED;
        } else {
            json_view invalid_root = d.root();
            if (invalid_root.valid() == 0)
                out.observed = RES_DEFINED;
        }
        d.drop();
        break;
    }
    case K_JSON_BUILDER_NULL: {
        json_builder b = json_builder();
        *case_name = "builder_null_args";
        if (b.valid()) {
            int r1v = b.add_string("k", (const char *)0, &e);
            int r2v = b.add_int((const char *)0, 1, &e);
            char *dump;
            size_t dl = 0;
            if (r1v == STDLIB_FAIL && r2v == STDLIB_FAIL && e.failed_now()) {
                dump = b.write(&dl);
                json_free(dump);
                out.observed = RES_REFUSED;
            }
        }
        b.drop();
        break;
    }
    case K_LOG_NULL_STREAM: {
        log_sink dead = log_sink((FILE *)0, 0, LOG_INFO);
        int r1v = dead.info("probe", &e);
        int r2v = dead.write(LOG_INFO, "probe", (const char *const *)0,
                             (const long long *)0, 0, (const char *)0, 0, &e);
        *api = "log";
        *case_name = "null_stream";
        if (!dead.valid() && r1v == STDLIB_FAIL && r2v == STDLIB_FAIL &&
            e.failed_now() && e.category() == STDLIB_CATEGORY_INVALID)
            out.observed = RES_REFUSED;
        dead.drop();
        break;
    }
    case K_LOG_OPEN_NULL: {
        log_sink dead = log_open_file((const char *)0, LOG_INFO, &e);
        *api = "log";
        *case_name = "open_null_path";
        if (!dead.valid() && e.failed_now() &&
            e.category() == STDLIB_CATEGORY_INVALID)
            out.observed = RES_REFUSED;
        dead.drop();
        break;
    }
    case K_LOG_PARSE_NULL: {
        int lv = log_level_parse((const char *)0, &e);
        *api = "log";
        *case_name = "level_parse_null";
        if (lv == -1 && e.failed_now() &&
            e.category() == STDLIB_CATEGORY_INVALID)
            out.observed = RES_REFUSED;
        break;
    }
    case K_LOG_MSG: {
        size_t mlen = (size_t)(r1 % 4097ULL);
        int rc;
        adv_fill_msg(mlen);
        *api = "log";
        *case_name = "message_0_to_4096";
        if (g_probe != (log_sink *)0) {
            rc = g_probe->write(LOG_INFO, g_mbuf, (const char *const *)0,
                                (const long long *)0, 0, (const char *)0, 0, &e);
            if (mlen <= 400) {
                if (rc == STDLIB_OK && e.ok())
                    out.observed = RES_OK;
            } else if (mlen >= 600) {
                if (rc == STDLIB_FAIL && e.failed_now() &&
                    e.category() == STDLIB_CATEGORY_BOUNDS)
                    out.observed = RES_REFUSED;
            } else {
                if (rc == STDLIB_OK ||
                    (rc == STDLIB_FAIL && e.failed_now()))
                    out.observed = RES_DEFINED;
            }
        }
        break;
    }
    case K_LOG_API_NULLMSG: {
        int rc;
        *api = "log";
        *case_name = "null_message_defined";
        if (g_probe != (log_sink *)0) {
            rc = g_probe->info((const char *)0, &e);
            if (rc == STDLIB_OK && e.ok())
                out.observed = RES_DEFINED;
        }
        break;
    }
    case K_LOG_LEVEL_NAME: {
        int ok = 1;
        if (__builtin_strcmp(log_level_name(LOG_TRACE), "TRACE") != 0)
            ok = 0;
        if (__builtin_strcmp(log_level_name(LOG_DEBUG), "DEBUG") != 0)
            ok = 0;
        if (__builtin_strcmp(log_level_name(LOG_INFO), "INFO") != 0)
            ok = 0;
        if (__builtin_strcmp(log_level_name(LOG_WARN), "WARN") != 0)
            ok = 0;
        if (__builtin_strcmp(log_level_name(LOG_ERROR), "ERROR") != 0)
            ok = 0;
        if (__builtin_strcmp(log_level_name(LOG_FATAL), "FATAL") != 0)
            ok = 0;
        if (__builtin_strcmp(log_level_name(-7), "UNKNOWN") != 0)
            ok = 0;
        if (__builtin_strcmp(log_level_name(999), "UNKNOWN") != 0)
            ok = 0;
        *api = "log";
        *case_name = "level_names";
        if (ok)
            out.observed = RES_DEFINED;
        break;
    }
    default:
        break;
    }

    out.sentinel = adv_sentinel_ok();
    return out;
}

/* Pinned first 16 scenarios: breakers and exact boundaries. */
static void adv_fixed_case(int id, int *kind, unsigned long long *r1,
                           unsigned long long *r2, unsigned long long *r3) {
    *r1 = 0;
    *r2 = 0;
    *r3 = 0;
    switch (id) {
    case 0:  *kind = K_JSON_NULL; break;
    case 1:  *kind = K_JSON_BUDGET_NULL; break;
    case 2:  *kind = K_JSON_BUDGET_ZERO_LEN; break;
    case 3:  *kind = K_JSON_BUDGET_SIZE_MAX; break;
    case 4:  *kind = K_JSON_TRUNC; *r1 = 16385; *r2 = 8200; break;
    case 5:  *kind = K_JSON_DEEP_DEFAULT; *r1 = 16385; break;
    case 6:  *kind = K_JSON_DEEP_DEFAULT; *r1 = 16384; break;
    case 7:  *kind = K_JSON_DEEP_OVERRIDE; *r1 = 20000; *r2 = 8; break;
    case 8:  *kind = K_JSON_DEEP_OVERRIDE; *r1 = 20000; *r2 = 12; break;
    case 9:  *kind = K_JSON_LONG; *r1 = 2097152; *r2 = 8; break;
    case 10: *kind = K_LOG_NULL_STREAM; break;
    case 11: *kind = K_LOG_OPEN_NULL; break;
    case 12: *kind = K_LOG_PARSE_NULL; break;
    case 13: *kind = K_LOG_MSG; *r1 = 0; break;
    case 14: *kind = K_LOG_MSG; *r1 = 4096; break;
    case 15: *kind = K_JSON_BUDGET_NULLFN; break;
    default: *kind = K_JSON_NULL; break;
    }
}

int main(int argc, char **argv) {
    const char *log_path = "results/scenarios.log";
    const char *probe_path = "results/probe.log";
    error log_err = error();
    log_sink sink;
    log_sink probe;
    char line[192];
    int id;
    int failures = 0;
    int refused = 0;
    int sentinel_ok = 0;
    int emitted = 0;
    int log_write_failures = 0;
    int snprintf_rc;
    int raw;

    if (argc > 1) {
        const char *arg;
        __builtin_memcpy(&arg, argv + 1, sizeof(arg));
        if (arg != (const char *)0)
            log_path = arg;
    }
    if (argc > 2) {
        const char *arg;
        __builtin_memcpy(&arg, argv + 2, sizeof(arg));
        if (arg != (const char *)0)
            probe_path = arg;
    }

    sink = log_open_file(log_path, LOG_TRACE, &log_err);
    if (!sink.valid()) {
        fprintf(stderr, "ADV1000: cannot open log path %s\n", log_path);
        return 2;
    }
    /* Probe sink is deliberately separate: message-length stress writes must
     * not inflate the 1000-line canonical scenarios.log. */
    probe = log_open_file(probe_path, LOG_TRACE, &log_err);
    if (!probe.valid()) {
        sink.drop();
        fprintf(stderr, "ADV1000: cannot open probe path %s\n", probe_path);
        return 2;
    }
    g_sink = &sink;
    g_probe = &probe;

    for (id = 0; id < ADV_N; id++) {
        unsigned long long r1, r2, r3;
        int kind;
        const char *api = "json";
        const char *case_name = "case";
        struct adv_outcome o;

        if (id < 16) {
            adv_fixed_case(id, &kind, &r1, &r2, &r3);
            raw = 1;
        } else {
            adv_params(id, &r1, &r2, &r3);
            kind = (int)(r3 % (unsigned long long)K_COUNT);
            raw = 0;
        }
        o = adv_exec(kind, r1, r2, r3, raw, &api, &case_name);

        fprintf(stdout, "S %d %s %s %s %d\n", id, api, case_name,
                adv_res_name(o.observed), o.sentinel);
        emitted++;

        if (o.observed == RES_FAIL || o.sentinel != 1)
            failures++;
        if (o.observed == RES_REFUSED)
            refused++;
        if (o.sentinel == 1)
            sentinel_ok++;

        snprintf_rc = snprintf(line, sizeof(line), "S %d %s %s %s %d", id,
                               api, case_name, adv_res_name(o.observed),
                               o.sentinel);
        if (snprintf_rc <= 0 || (size_t)snprintf_rc >= sizeof(line)) {
            log_write_failures++;
        } else if (sink.info(line, &log_err) != STDLIB_OK) {
            log_write_failures++;
        }
    }

    g_sink = (log_sink *)0;
    g_probe = (log_sink *)0;
    sink.drop();
    probe.drop();

    if (emitted != ADV_N)
        failures++;
    if (log_write_failures != 0)
        failures++;

    fprintf(stdout, "ADV1000 json_log 1000 %d %d %d\n", refused, sentinel_ok,
            failures);
    return failures == 0 ? 0 : 1;
}
