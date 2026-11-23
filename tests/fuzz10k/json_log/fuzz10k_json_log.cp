/* FUZZ10K / json_log -- 10,000 deterministic fuzz scenarios via forked children.
 *
 * Scope: stdlib/json.hp (json ctor, json_input_within_budget,
 * json_parse_budget, read_size/value_count, views, builder) and
 * stdlib/log.hp (levels, sinks, write/info, structured fields).
 * Compiled cplus_json_* symbols are exercised by the sibling compiled_probe.cp.
 *
 * Determinism: fixed SplitMix64 seed 0xF02A5EED1234ABCD; every id 0..9999
 * derives all parameters from (seed, id). No wall-clock, PID or address is used
 * to choose a scenario or to classify its result.
 *
 * Isolation: each scenario runs in a forked child. A per-child alarm() is the
 * hang watchdog; wait4() reports the termination signal (crash vs hang) and
 * the child's peak RSS (bounded-memory check). Four contiguous 32-byte guards
 * around the JSON and log-message work buffers detect buffer corruption.
 *
 * Output protocol: one canonical "S <id> <kind> <result> <sentinel> <rss_kb>"
 * line per scenario, then "FUZZ10K json_log <n> <crashes> <hangs>
 * <corruptions> <refusals>". A corresponding line per scenario is written
 * through a real log_sink to <log-path> (default results/scenarios.log).
 *
 * Exact build (from repo root):
 *   ./build/c+ build tests/fuzz10k/json_log/fuzz10k_json_log.cp \
 *     --strict --sanitize=none -I. -Ithird_party/yyjson -Lbuild/ext -lyyjson \
 *     -o tests/fuzz10k/json_log/results/fuzz10k_json_log
 * Run:  timeout 600 <bin> <log-path>   (log-path defaults to results/scenarios.log)
 *
 * NOTE: this is evidence, not proof. It exercises the listed contracts with
 * deterministic inputs; it cannot establish the absence of all defects.
 */
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"
#include "stdlib/log.hp"

#define FUZZ_N 10000
#define FUZZ_SEED 0xF02A5EED1234ABCDULL
#define GUARD 32
#define J_CAP ((size_t)128u * 1024u + 16u)
#define MSG_CAP 4200u
#define WATCHDOG_SEC 5
#define MEM_CAP_BYTES ((unsigned long long)256u * 1024u * 1024u)
#define MEM_RLIMIT ((rlim_t)512u * 1024u * 1024u)

/* ---- outcome tokens ---------------------------------------------------- */
#define RES_OK 0
#define RES_REFUSED 1
#define RES_DEFINED 2
#define RES_FAIL 3
#define RES_CRASH 4
#define RES_HANG 5
#define RES_CORRUPT 6
#define RES_OOM 7

static const char *res_name(int r) {
    if (r == RES_OK)
        return "ok";
    if (r == RES_REFUSED)
        return "refused";
    if (r == RES_DEFINED)
        return "defined";
    if (r == RES_CRASH)
        return "CRASH";
    if (r == RES_HANG)
        return "HANG";
    if (r == RES_CORRUPT)
        return "CORRUPT";
    if (r == RES_OOM)
        return "OOM";
    return "FAIL";
}

/* ---- scenario kinds ---------------------------------------------------- */
#define K_J_CTOR 0
#define K_J_PARSE_BUDGET 1
#define K_J_BUDGET_FN 2
#define K_J_DEEP 3
#define K_J_DEEP_BUDGET 4
#define K_J_TRUNC 5
#define K_J_LARGE 6
#define K_J_MALFORMED 7
#define K_J_RANDOM_BYTES 8
#define K_J_NULL_ARGS 9
#define K_J_VIEW 10
#define K_J_BUILDER 11
#define K_L_MSG 12
#define K_L_NULL 13
#define K_L_LEVEL 14
#define K_L_FIELDS 15
#define K_COUNT 16

static const char *kind_name(int k) {
    static const char *names[K_COUNT] = {
        "j_ctor", "j_parse_budget", "j_budget_fn", "j_deep",
        "j_deep_budget", "j_trunc", "j_large", "j_malformed",
        "j_random_bytes", "j_null_args", "j_view", "j_builder",
        "l_msg", "l_null", "l_level", "l_fields"
    };
    if (k < 0 || k >= K_COUNT)
        return "unknown";
    return names[k];
}

/* ---- contiguous work area with canaries -------------------------------- */
struct work_area {
    unsigned char pre[GUARD];
    unsigned char jbuf[J_CAP];
    unsigned char post[GUARD];
    unsigned char mpre[GUARD];
    char mbuf[MSG_CAP];
    unsigned char mpost[GUARD];
};
static struct work_area g_work;

/* Parameters set by the parent before fork; the child inherits them. */
static int g_kind;
static unsigned long long g_r1;
static unsigned long long g_r2;
static unsigned long long g_r3;
static int g_raw;

/* PRNG state advanced only inside the isolated child. */
static unsigned long long g_rng;
static size_t g_gen_depth_max;

/* ---- low-level byte access (strict-safe) ------------------------------- */
static void jput(size_t i, char c) {
    __builtin_memcpy(g_work.jbuf + i, &c, 1);
}
static char jget(size_t i) {
    char c;
    __builtin_memcpy(&c, g_work.jbuf + i, 1);
    return c;
}
static void mput(size_t i, char c) {
    __builtin_memcpy(g_work.mbuf + i, &c, 1);
}

static void arm(void) {
    __builtin_memset(g_work.pre, 0xA5, GUARD);
    __builtin_memset(g_work.post, 0x5A, GUARD);
    __builtin_memset(g_work.mpre, 0x3C, GUARD);
    __builtin_memset(g_work.mpost, 0xC3, GUARD);
}

static int sentinel_ok(void) {
    unsigned char expect[GUARD];
    int ok = 1;
    __builtin_memset(expect, 0xA5, GUARD);
    if (__builtin_memcmp(g_work.pre, expect, GUARD) != 0)
        ok = 0;
    __builtin_memset(expect, 0x5A, GUARD);
    if (__builtin_memcmp(g_work.post, expect, GUARD) != 0)
        ok = 0;
    __builtin_memset(expect, 0x3C, GUARD);
    if (__builtin_memcmp(g_work.mpre, expect, GUARD) != 0)
        ok = 0;
    __builtin_memset(expect, 0xC3, GUARD);
    if (__builtin_memcmp(g_work.mpost, expect, GUARD) != 0)
        ok = 0;
    return ok;
}

/* ---- deterministic PRNG ------------------------------------------------ */
static unsigned long long mix64(unsigned long long z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}
static unsigned long long nextr(void) {
    g_rng += 0x9E3779B97F4A7C15ULL;
    return mix64(g_rng);
}
static size_t pick(size_t n) {
    if (n == 0)
        return 0;
    return (size_t)(nextr() % (unsigned long long)n);
}

static void params_for(int id, int *kind, unsigned long long *r1,
                       unsigned long long *r2, unsigned long long *r3,
                       int *raw) {
    unsigned long long k = (unsigned long long)id;
    *r1 = 0;
    *r2 = 0;
    *r3 = 0;
    *raw = 0;
    /* First 16 ids are pinned breakers/boundaries. r1 is a literal
     * depth/length/variant and r2 is a literal budget-table index. */
    if (id < 16) {
        *raw = 1;
        switch (id) {
        case 0:  *kind = K_J_NULL_ARGS; *r1 = 0; break;
        case 1:  *kind = K_J_PARSE_BUDGET; *r1 = 1; break;
        case 2:  *kind = K_J_PARSE_BUDGET; *r1 = 2; break;
        case 3:  *kind = K_J_PARSE_BUDGET; *r1 = 3; break;
        case 4:  *kind = K_J_TRUNC; *r1 = 16385; *r2 = 8; break;
        case 5:  *kind = K_J_DEEP; *r1 = 16384; break;
        case 6:  *kind = K_J_DEEP; *r1 = 16385; break;
        case 7:  *kind = K_J_DEEP_BUDGET; *r1 = 20000; *r2 = 8; break;
        case 8:  *kind = K_J_DEEP_BUDGET; *r1 = 20000; *r2 = 12; break;
        case 9:  *kind = K_J_LARGE; *r1 = 131072; break;
        case 10: *kind = K_L_MSG; *r1 = 0; break;
        case 11: *kind = K_L_MSG; *r1 = 4096; break;
        case 12: *kind = K_L_NULL; break;
        case 13: *kind = K_J_BUDGET_FN; *r1 = 5; break;
        case 14: *kind = K_J_RANDOM_BYTES; *r1 = 131072; break;
        case 15: *kind = K_J_VIEW; break;
        default: *kind = K_J_NULL_ARGS; break;
        }
        return;
    }
    *r1 = mix64(FUZZ_SEED + k * 0x9E3779B97F4A7C15ULL);
    *r2 = mix64(FUZZ_SEED ^ (k * 0xD1B54A32D192ED03ULL));
    *r3 = mix64(mix64(FUZZ_SEED + k * 0x2545F4914F6CDD1DULL + 0x1234567ULL) +
                k * 0xD6E8FEB86659FD93ULL);
    *kind = (int)(*r3 % (unsigned long long)K_COUNT);
}

static const size_t LEN_TBL[] = {
    0, 1, 2, 3, 4, 8, 16, 64, 255, 256, 1024, 4096, 16384, 65536, 131072
};
#define LEN_TBL_N 15
static size_t len_from(unsigned long long r) {
    if (((r >> 11) & 3ULL) == 0ULL)
        return (size_t)(r % 131073ULL);
    return LEN_TBL[r % LEN_TBL_N];
}
static size_t depth_from(unsigned long long r) {
    static const size_t T[] = {
        0, 1, 2, 3, 8, 16, 64, 256, 1024, 4096, 16384, 16385, 20000
    };
    if (((r >> 13) & 3ULL) == 0ULL)
        return (size_t)(r % 20001ULL);
    return T[r % 13];
}
static const size_t BUDGET_TBL[] = {
    (size_t)16384, (size_t)0, (size_t)1, (size_t)2, (size_t)8, (size_t)16,
    (size_t)64, (size_t)1024, (size_t)16384, (size_t)16385, (size_t)20000,
    (size_t)25000, SIZE_MAX
};
#define BUDGET_TBL_N 13
static size_t budget_from(unsigned long long r) {
    return BUDGET_TBL[r % BUDGET_TBL_N];
}

/* ---- bounded JSON document builders ------------------------------------ */
static void append_bytes(size_t *pos, const char *s, size_t n, size_t cap) {
    size_t i;
    for (i = 0; i < n && *pos < cap; i++)
        jput((*pos)++, s[i]);
}
static void append_lit(size_t *pos, const char *s, size_t cap) {
    append_bytes(pos, s, __builtin_strlen(s), cap);
}

static void gen_value(size_t *pos, int depth_left, int cur_depth, size_t cap);

static void gen_array(size_t *pos, int depth_left, int cur_depth, size_t cap) {
    size_t cnt = pick(4);
    size_t i;
    if (cur_depth + 1 > (int)g_gen_depth_max)
        g_gen_depth_max = (size_t)(cur_depth + 1);
    jput((*pos)++, '[');
    for (i = 0; i < cnt && *pos + 8 < cap; i++) {
        if (i)
            jput((*pos)++, ',');
        gen_value(pos, depth_left - 1, cur_depth + 1, cap);
    }
    jput((*pos)++, ']');
}

static void gen_object(size_t *pos, int depth_left, int cur_depth, size_t cap) {
    size_t cnt = pick(4);
    size_t i;
    if (cur_depth + 1 > (int)g_gen_depth_max)
        g_gen_depth_max = (size_t)(cur_depth + 1);
    jput((*pos)++, '{');
    for (i = 0; i < cnt && *pos + 12 < cap; i++) {
        char key[8];
        __builtin_memset(key, 0, sizeof(key));
        key[0] = 'k';
        key[1] = (char)('0' + (int)(i & 7u));
        if (i)
            jput((*pos)++, ',');
        jput((*pos)++, '"');
        append_bytes(pos, key, 2, cap);
        jput((*pos)++, '"');
        jput((*pos)++, ':');
        gen_value(pos, depth_left - 1, cur_depth + 1, cap);
    }
    jput((*pos)++, '}');
}

static void gen_value(size_t *pos, int depth_left, int cur_depth, size_t cap) {
    size_t choice;
    if (*pos >= cap)
        return;
    choice = depth_left > 0 ? pick(8) : pick(5);
    switch (choice) {
    case 0:
        append_lit(pos, "null", cap);
        break;
    case 1:
        append_lit(pos, "true", cap);
        break;
    case 2:
        append_lit(pos, "false", cap);
        break;
    case 3: {
        char num[32];
        int n = __builtin_snprintf(num, sizeof(num), "%llu",
                                   nextr() % 100000000ULL);
        if (n > 0)
            append_bytes(pos, num, (size_t)n, cap);
        break;
    }
    case 4: {
        size_t slen = 1 + pick(8);
        size_t i;
        jput((*pos)++, '"');
        for (i = 0; i < slen && *pos < cap; i++)
            jput((*pos)++, (char)('a' + (int)(nextr() % 26ULL)));
        jput((*pos)++, '"');
        break;
    }
    case 5:
    case 7:
        gen_array(pos, depth_left, cur_depth, cap);
        break;
    default:
        gen_object(pos, depth_left, cur_depth, cap);
        break;
    }
}

/* Returns the used length (NUL written at that index). */
static size_t gen_document(size_t cap) {
    size_t pos = 0;
    int depth = 1 + (int)pick(10);
    g_gen_depth_max = 0;
    gen_value(&pos, depth, 0, cap);
    if (pos >= cap)
        pos = cap - 1;
    jput(pos, '\0');
    return pos;
}

static size_t build_deep(size_t depth) {
    size_t pos = 0;
    size_t i;
    if (depth > J_CAP / 2)
        depth = J_CAP / 2 - 1;
    for (i = 0; i < depth; i++)
        jput(pos++, '[');
    jput(pos++, '0');
    for (i = 0; i < depth; i++)
        jput(pos++, ']');
    jput(pos, '\0');
    return pos;
}

static size_t build_long_string(size_t len) {
    size_t i;
    if (len >= J_CAP)
        len = J_CAP - 1;
    if (len < 2) {
        jput(0, '[');
        jput(1, '\0');
        return len;
    }
    jput(0, '"');
    for (i = 1; i + 1 < len; i++)
        jput(i, 'a');
    jput(len - 1, '"');
    jput(len, '\0');
    return len;
}

static size_t build_flat_array(size_t target) {
    size_t len = target;
    size_t i;
    if (len < 3)
        len = 3;
    if (len >= J_CAP)
        len = J_CAP - 1;
    if (((len - 2) & 1u) == 0u)
        len--;
    jput(0, '[');
    for (i = 1; i + 1 < len; i++)
        jput(i, (i & 1u) ? '1' : ',');
    jput(len - 1, ']');
    jput(len, '\0');
    return len;
}

static size_t build_random_bytes(size_t n) {
    static const char alphabet[] =
        "{}[]\":,0123456789truefalsnl \t\\/+-.eE";
    size_t i;
    if (n >= J_CAP)
        n = J_CAP - 1;
    for (i = 0; i < n; i++)
        jput(i, alphabet[nextr() % (__builtin_strlen(alphabet) - 1u)]);
    jput(n, '\0');
    return n;
}

static void fill_msg(size_t len) {
    size_t i;
    if (len >= MSG_CAP)
        len = MSG_CAP - 1;
    for (i = 0; i < len; i++)
        mput(i, (char)('A' + (int)(i % 26)));
    mput(len, '\0');
}

/* ---- scenario executor (runs in the child) ----------------------------- */
static int run_scenario(void) {
    error e = error();
    int observed = RES_FAIL;
    size_t depth;
    size_t len;
    size_t used;
    size_t budget;
    size_t variant;
    int rc;

    if (g_raw) {
        depth = (size_t)g_r1;
        len = (size_t)g_r1;
        budget = g_r2 < BUDGET_TBL_N ? BUDGET_TBL[g_r2] : SIZE_MAX;
    } else {
        depth = depth_from(g_r1);
        len = len_from(g_r1);
        budget = budget_from(g_r2);
    }
    variant = (size_t)(g_r3 % 5ULL);

    switch (g_kind) {
    case K_J_CTOR: {
        used = gen_document(4096);
        {
            json d = json((const char *)g_work.jbuf, &e);
            if (d.valid()) {
                if (e.ok() && d.read_size() > 0 && d.value_count() > 0)
                    observed = RES_OK;
            } else if (e.failed_now()) {
                observed = RES_REFUSED;
            }
            d.drop();
        }
        break;
    }
    case K_J_PARSE_BUDGET: {
        if (g_raw && (g_r1 == 1 || g_r1 == 2 || g_r1 == 3)) {
            /* pinned null / zero-length / SIZE_MAX paths */
            const char *text = (g_r1 == 1) ? (const char *)0
                                           : (const char *)g_work.jbuf;
            size_t length = (g_r1 == 1) ? 16u : ((g_r1 == 2) ? 0u : SIZE_MAX);
            json d = json_parse_budget(text, length, budget, &e);
            if (!d.valid() && e.failed_now())
                observed = RES_REFUSED;
            d.drop();
        } else {
            used = gen_document(4096);
            json d = json_parse_budget((const char *)g_work.jbuf, used, budget, &e);
            if (d.valid()) {
                if (e.ok() && d.read_size() > 0 && d.value_count() > 0)
                    observed = RES_OK;
            } else if (e.failed_now()) {
                observed = RES_REFUSED;
            }
            d.drop();
        }
        break;
    }
    case K_J_BUDGET_FN: {
        if (g_raw && g_r1 == 5) {
            int c = json_input_within_budget((const char *)0, 8, budget, &e);
            if (c == 0 && e.failed_now())
                observed = RES_REFUSED;
            break;
        }
        used = gen_document(4096);
        {
            int c = json_input_within_budget((const char *)g_work.jbuf, used,
                                             budget, &e);
            if (used > 0 && g_gen_depth_max <= budget) {
                if (c == 1 && e.ok())
                    observed = RES_OK;
            } else if (c == 0 && e.failed_now()) {
                observed = RES_REFUSED;
            }
        }
        break;
    }
    case K_J_DEEP: {
        used = build_deep(depth);
        {
            json d = json((const char *)g_work.jbuf, &e);
            if (depth <= (size_t)16384) {
                if (d.valid() && e.ok() && d.read_size() > 0 &&
                    d.value_count() > 0)
                    observed = RES_OK;
            } else if (!d.valid() && e.failed_now() && d.read_size() == 0 &&
                       d.value_count() == 0) {
                observed = RES_REFUSED;
            }
            d.drop();
        }
        break;
    }
    case K_J_DEEP_BUDGET: {
        used = build_deep(depth);
        {
            json d = json_parse_budget((const char *)g_work.jbuf, used, budget, &e);
            if (depth <= budget && used > 0) {
                if (d.valid() && e.ok() && d.read_size() > 0 &&
                    d.value_count() > 0)
                    observed = RES_OK;
            } else if (!d.valid() && e.failed_now() && d.read_size() == 0) {
                observed = RES_REFUSED;
            }
            d.drop();
        }
        break;
    }
    case K_J_TRUNC: {
        size_t cut;
        if (depth < 1)
            depth = 1;
        used = build_deep(depth);
        if (used < 2)
            used = 2;
        cut = 1 + (size_t)(nextr() % (unsigned long long)(used - 1));
        if ((g_r3 & 1ULL) == 0ULL) {
            json d = json_parse_budget((const char *)g_work.jbuf, cut, budget, &e);
            if (!d.valid() && e.failed_now() && d.read_size() == 0)
                observed = RES_REFUSED;
            d.drop();
        } else {
            char saved = jget(cut);
            json d;
            jput(cut, '\0');
            d = json((const char *)g_work.jbuf, &e);
            jput(cut, saved);
            if (!d.valid() && e.failed_now() && d.read_size() == 0)
                observed = RES_REFUSED;
            d.drop();
        }
        break;
    }
    case K_J_LARGE: {
        used = build_long_string(len);
        {
            json d = json_parse_budget((const char *)g_work.jbuf, used, budget, &e);
            if (d.valid()) {
                if (e.ok() && d.read_size() == used && d.value_count() >= 1)
                    observed = RES_OK;
            } else if (e.failed_now()) {
                observed = RES_REFUSED;
            }
            d.drop();
        }
        break;
    }
    case K_J_MALFORMED: {
        used = gen_document(4096);
        if (used > 0)
            jput(0, 'x');
        {
            json d = json_parse_budget((const char *)g_work.jbuf, used, budget, &e);
            if (!d.valid() && e.failed_now())
                observed = RES_REFUSED;
            d.drop();
        }
        break;
    }
    case K_J_RANDOM_BYTES: {
        used = build_random_bytes(len);
        {
            json d = json_parse_budget((const char *)g_work.jbuf, used, budget, &e);
            if (d.valid()) {
                if (d.read_size() > 0)
                    observed = RES_OK;
            } else if (e.failed_now()) {
                observed = RES_REFUSED;
            }
            d.drop();
        }
        break;
    }
    case K_J_NULL_ARGS: {
        used = build_flat_array(16);
        (void)used;
        switch (variant) {
        case 0: {
            json d = json((const char *)0, &e);
            if (!d.valid())
                observed = RES_REFUSED;
            d.drop();
            break;
        }
        case 1: {
            json d = json_parse_budget((const char *)0, 16, budget, &e);
            if (!d.valid() && e.failed_now())
                observed = RES_REFUSED;
            d.drop();
            break;
        }
        case 2: {
            json d = json_parse_budget((const char *)g_work.jbuf, 0, budget, &e);
            if (!d.valid() && e.failed_now())
                observed = RES_REFUSED;
            d.drop();
            break;
        }
        case 3: {
            json d = json_parse_budget((const char *)g_work.jbuf, SIZE_MAX, budget, &e);
            if (!d.valid() && e.failed_now())
                observed = RES_REFUSED;
            d.drop();
            break;
        }
        default: {
            int c = json_input_within_budget((const char *)0, 8, budget, &e);
            if (c == 0 && e.failed_now())
                observed = RES_REFUSED;
            break;
        }
        }
        break;
    }
    case K_J_VIEW: {
        static const char FIX[] = "{\"a\":7,\"b\":\"str\",\"c\":[1,2]}";
        json d = json(FIX, &e);
        if (d.valid()) {
            json_view root = d.root();
            json_view a = root.member("a");
            json_view none = root.member("nope");
            json_view nullkey = root.member((const char *)0);
            json_view arr = root.member("c");
            json_view far = arr.at(99);
            int got = -1;
            int r = a.get_int(&got, &e);
            double dv = 0.0;
            int rd = arr.get_double(&dv, &e);
            if (root.is_object() && a.valid() && !none.valid() &&
                !nullkey.valid() && arr.is_array() && arr.size() == 2 &&
                r == STDLIB_OK && got == 7 && !far.valid() &&
                (rd == STDLIB_FAIL || rd == STDLIB_OK))
                observed = RES_DEFINED;
        } else {
            json_view badview = d.root();
            if (!badview.valid())
                observed = RES_DEFINED;
        }
        d.drop();
        break;
    }
    case K_J_BUILDER: {
        json_builder b = json_builder();
        if (b.valid()) {
            char *dump;
            size_t dl = 0;
            int a = b.add_int("i", (int)(nextr() % 100000ULL), &e);
            int dbl = b.add_double("d", (double)(int)(nextr() % 1000ULL) / 8.0, &e);
            int bo = b.add_bool("b", (int)(nextr() & 1ULL), &e);
            int st = b.add_string("s", "text", &e);
            int bn = b.add_string("z", (const char *)0, &e);
            if (a == STDLIB_OK && dbl == STDLIB_OK && bo == STDLIB_OK &&
                st == STDLIB_OK && bn == STDLIB_FAIL && e.failed_now()) {
                dump = b.write(&dl);
                if (dump != (char *)0 && dl > 0)
                    observed = RES_DEFINED;
                json_free(dump);
            }
        }
        b.drop();
        break;
    }
    case K_L_MSG: {
        size_t mlen = (size_t)(g_r1 % 4097ULL);
        log_sink sink = log_open_file("/dev/null", LOG_TRACE, &e);
        if (sink.valid()) {
            fill_msg(mlen);
            rc = sink.write(LOG_INFO, g_work.mbuf, (const char *const *)0,
                            (const long long *)0, 0, (const char *)0, 0, &e);
            if (mlen <= 400) {
                if (rc == STDLIB_OK && e.ok())
                    observed = RES_OK;
            } else if (mlen >= 600) {
                if (rc == STDLIB_FAIL && e.failed_now() &&
                    e.category() == STDLIB_CATEGORY_BOUNDS)
                    observed = RES_REFUSED;
            } else if (rc == STDLIB_OK ||
                       (rc == STDLIB_FAIL && e.failed_now())) {
                observed = RES_DEFINED;
            }
            sink.drop();
        }
        break;
    }
    case K_L_NULL: {
        log_sink dead = log_sink((FILE *)0, 0, LOG_INFO);
        log_sink nullpath = log_open_file((const char *)0, LOG_INFO, &e);
        int lv = log_level_parse((const char *)0, &e);
        int r1v = dead.info("probe", &e);
        int r2v = dead.write(LOG_INFO, "probe", (const char *const *)0,
                             (const long long *)0, 0, (const char *)0, 0, &e);
        int r3v = dead.info((const char *)0, &e);
        if (!dead.valid() && !nullpath.valid() && lv == -1 &&
            r1v == STDLIB_FAIL && r2v == STDLIB_FAIL &&
            r3v == STDLIB_FAIL && e.failed_now() &&
            e.category() == STDLIB_CATEGORY_INVALID)
            observed = RES_REFUSED;
        dead.drop();
        nullpath.drop();
        break;
    }
    case K_L_LEVEL: {
        int ok = 1;
        int level = (int)(g_r1 % (unsigned long long)LOG_LEVELS);
        const char *name = log_level_name(level);
        error pe = error();
        int parsed = log_level_parse(name, &pe);
        if (__builtin_strcmp(name, log_level_name(level)) != 0)
            ok = 0;
        if (__builtin_strcmp(log_level_name(-7), "UNKNOWN") != 0)
            ok = 0;
        if (__builtin_strcmp(log_level_name(999), "UNKNOWN") != 0)
            ok = 0;
        if (parsed != level || !pe.ok())
            ok = 0;
        if (ok)
            observed = RES_DEFINED;
        break;
    }
    case K_L_FIELDS: {
        const char *keys[8];
        long long values[8];
        size_t cnt = pick(9);
        size_t i;
        log_sink sink = log_open_file("/dev/null", LOG_TRACE, &e);
        for (i = 0; i < cnt; i++) {
            keys[i] = "k";
            values[i] = (long long)(nextr() % 1000000ULL);
        }
        if (sink.valid()) {
            fill_msg(32);
            rc = sink.write(LOG_INFO, g_work.mbuf, keys, values, cnt,
                            "fuzz10k.cp", 42, &e);
            if (rc == STDLIB_OK && e.ok())
                observed = RES_OK;
            else if (rc == STDLIB_FAIL && e.failed_now())
                observed = RES_REFUSED;
            sink.drop();
        }
        break;
    }
    default:
        break;
    }
    return observed;
}

/* ---- child entry ------------------------------------------------------- */
static void child_run(void) {
    int observed;
    int sentinel;
    struct rlimit rl;
    alarm(WATCHDOG_SEC);
    rl.rlim_cur = MEM_RLIMIT;
    rl.rlim_max = MEM_RLIMIT;
    (void)setrlimit(RLIMIT_AS, &rl);
    arm();
    observed = run_scenario();
    sentinel = sentinel_ok();
    _exit((sentinel ? 1 : 0) | ((observed & 7) << 1));
}

/* ---- main: fork one child per scenario --------------------------------- */
int main(int argc, char **argv) {
    const char *log_path = "results/scenarios.log";
    error log_err = error();
    log_sink sink;
    char line[192];
    int id;
    int crashes = 0;
    int hangs = 0;
    int corruptions = 0;
    int refusals = 0;
    int log_write_failures = 0;
    int emitted = 0;
    int snprintf_rc;

    if (argc > 1) {
        const char *arg;
        __builtin_memcpy(&arg, argv + 1, sizeof(arg));
        if (arg != (const char *)0)
            log_path = arg;
    }

    sink = log_open_file(log_path, LOG_TRACE, &log_err);
    if (!sink.valid()) {
        fprintf(stderr, "FUZZ10K: cannot open log path %s\n", log_path);
        return 2;
    }

    for (id = 0; id < FUZZ_N; id++) {
        pid_t pid;
        int status = 0;
        struct rusage ru;
        int res = RES_FAIL;
        int sent = 0;
        int kind;
        unsigned long long r1, r2, r3;
        int raw;
        unsigned long long rss = 0;

        params_for(id, &kind, &r1, &r2, &r3, &raw);
        g_kind = kind;
        g_r1 = r1;
        g_r2 = r2;
        g_r3 = r3;
        g_raw = raw;
        g_rng = mix64(FUZZ_SEED ^ ((unsigned long long)id + 1ULL) *
                                   0x9E3779B97F4A7C15ULL);
        g_gen_depth_max = 0;

        __builtin_memset(&ru, 0, sizeof(ru));
        fflush(NULL);
        pid = fork();
        if (pid == 0) {
            child_run();
            _exit(127);
        }
        if (pid < 0) {
            crashes++;
            res = RES_CRASH;
        } else {
            wait4(pid, &status, 0, &ru);
            rss = (unsigned long long)ru.ru_maxrss;
            if (WIFSIGNALED(status)) {
                if (WTERMSIG(status) == SIGALRM) {
                    hangs++;
                    res = RES_HANG;
                } else {
                    crashes++;
                    res = RES_CRASH;
                }
                sent = 0;
            } else if (WIFEXITED(status)) {
                int code = WEXITSTATUS(status);
                int observed;
                sent = code & 1;
                observed = (code >> 1) & 7;
                if (rss > MEM_CAP_BYTES) {
                    corruptions++;
                    res = RES_OOM;
                } else if (!sent) {
                    corruptions++;
                    res = RES_CORRUPT;
                } else if (observed == RES_FAIL || observed > RES_DEFINED) {
                    corruptions++;
                    res = RES_FAIL;
                } else {
                    res = observed;
                    if (observed == RES_REFUSED)
                        refusals++;
                }
            } else {
                crashes++;
                res = RES_CRASH;
            }
        }

        /* Raw RSS varies across runs (page-fault timing), so the canonical
         * stream records only the bounded-memory verdict, not the byte count.
         * The exact peak RSS is still checked against MEM_CAP_BYTES above. */
        snprintf_rc = snprintf(line, sizeof(line),
                               "S %d %s %s %d %s", id, kind_name(kind),
                               res_name(res), sent,
                               rss <= MEM_CAP_BYTES ? "memok" : "memOOM");
        if (snprintf_rc > 0 && (size_t)snprintf_rc < sizeof(line)) {
            fprintf(stdout, "%s\n", line);
            if (sink.info(line, &log_err) != STDLIB_OK)
                log_write_failures++;
        } else {
            log_write_failures++;
        }
        emitted++;
    }

    sink.drop();

    if (emitted != FUZZ_N)
        corruptions++;
    if (log_write_failures != 0)
        corruptions++;

    fprintf(stdout, "FUZZ10K json_log %d %d %d %d %d\n", emitted, crashes,
            hangs, corruptions, refusals);
    return (crashes + hangs + corruptions) == 0 ? 0 : 1;
}
