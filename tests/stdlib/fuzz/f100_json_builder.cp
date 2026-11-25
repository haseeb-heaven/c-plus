/* F-091 -- deterministic fuzz config: JSON builder mutations and round-trip.
 *
 * Config:  F-091 (f100 json family)
 * Seed:    0xC0FFEE91 (fixed)
 * Ops:     128 builder cycles x 8 seeded mutations = 1024 add operations,
 *          each round-tripped through write() -> parse() -> typed getters.
 * Build:   --strict --sanitize=none and --strict --sanitize=undefined, each
 *          with -Ithird_party/yyjson -Lbuild/ext -lyyjson as separate operands.
 * Run:     timeout 30 <out>.  Watchdog alarm(8) -> _Exit(99).
 *
 * Mutation types (seeded) are add_int / add_double / add_bool / add_string on
 * eight unique keys per builder, so no duplicate-key ambiguity is generated.
 * A separate section exercises documented refusals: NULL key/value, non-finite
 * double, and every mutator after drop.  Expected values are kept in a shadow
 * table and verified after the round-trip.
 *
 * Last lines: "F091_BUILDER <checks> <failures>" and "F091_DIGEST <hex>".
 */
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"

#define F091_SEED 0xC0FFEE91ULL
#define F091_CYCLES 128
#define F091_KEYS 8
#define F091_OPS (F091_CYCLES * F091_KEYS)
#define F091_WATCHDOG_SECS 8

/* yyjson's mutable add_* methods do NOT copy the key string (only
 * add_string's value is copied by the wrapper).  Keys must therefore be
 * stable, program-lifetime storage -- a reused stack buffer silently makes
 * every key alias the last written value.  This is documented as a wrapper
 * hazard in F100_THREAD_JSON_NOTES.md. */
static const char *f091_keys[F091_KEYS] = {
    "k0", "k1", "k2", "k3", "k4", "k5", "k6", "k7"
};

static unsigned int f091_failures = 0;
static unsigned int f091_checks = 0;

static void f091_check(const char *tag, int cond) {
    f091_checks++;
    if (!cond)
        f091_failures++;
    print(tag, cond ? 1 : 0);
}

static void f091_on_alarm(int sig) {
    (void)sig;
    _Exit(99);
}

static unsigned long long f091_rnd(unsigned long long *s) {
    *s = *s * 6364136223846793005ULL + 1442695040888963407ULL;
    return *s >> 33;
}

#define F091_FNV_PRIME 1099511628211ULL
static unsigned long long f091_mix(unsigned long long d, unsigned long long v) {
    return (d ^ v) * F091_FNV_PRIME;
}

int main(void) {
    error f091_err = error();
    unsigned long long f091_state = F091_SEED;
    unsigned long long f091_digest = 1469598103934665603ULL;
    char f091_hex[32];
    int f091_cycle;
    int f091_all_ok = 1;

    signal(SIGALRM, f091_on_alarm);
    alarm(F091_WATCHDOG_SECS);

    for (f091_cycle = 0; f091_cycle < F091_CYCLES; f091_cycle++) {
        json_builder f091_bld = json_builder();
        int f091_types[F091_KEYS];
        int f091_ivals[F091_KEYS];
        double f091_dvals[F091_KEYS];
        int f091_bvals[F091_KEYS];
        char f091_strvals[F091_KEYS][16];
        int f091_k;
        int f091_cycle_ok = 1;

        if (!f091_bld.valid()) {
            f091_all_ok = 0;
            f091_bld.drop();
            continue;
        }
        for (f091_k = 0; f091_k < F091_KEYS; f091_k++) {
            const char *f091_key = f091_keys[f091_k];
            unsigned long long r = f091_rnd(&f091_state);
            int kind = (int)(r & 3ULL);
            int rc = STDLIB_FAIL;
            f091_types[f091_k] = kind;
            if (kind == 0) {
                f091_ivals[f091_k] = (int)(r % 100000ULL) - 50000;
                rc = f091_bld.add_int(f091_key, f091_ivals[f091_k], &f091_err);
            } else if (kind == 1) {
                f091_dvals[f091_k] = (double)(int)((r >> 8) % 10000ULL) / 8.0;
                rc = f091_bld.add_double(f091_key, f091_dvals[f091_k], &f091_err);
            } else if (kind == 2) {
                f091_bvals[f091_k] = (int)((r >> 12) & 1ULL);
                rc = f091_bld.add_bool(f091_key, f091_bvals[f091_k], &f091_err);
            } else {
                snprintf(f091_strvals[f091_k], sizeof(f091_strvals[f091_k]),
                         "v%u", (unsigned)(r % 1000ULL));
                rc = f091_bld.add_string(f091_key, f091_strvals[f091_k], &f091_err);
            }
            if (rc != STDLIB_OK || !f091_err.ok())
                f091_cycle_ok = 0;
            f091_digest = f091_mix(f091_digest, (unsigned long long)f091_cycle);
            f091_digest = f091_mix(f091_digest, (unsigned long long)f091_k);
            f091_digest = f091_mix(f091_digest, (unsigned long long)kind);
            f091_digest = f091_mix(f091_digest, (unsigned long long)rc);
        }

        /* Round-trip: write -> parse -> type-specific getter verification. */
        {
            size_t f091_len = 0;
            char *f091_text = f091_bld.write(&f091_len);
            if (f091_text == (char *)0 || f091_len == 0) {
                f091_cycle_ok = 0;
            } else {
                json f091_doc = json(f091_text, &f091_err);
                if (f091_doc.valid()) {
                    json_view f091_root = f091_doc.root();
                    if (f091_root.size() != F091_KEYS)
                        f091_cycle_ok = 0;
                    for (f091_k = 0; f091_k < F091_KEYS; f091_k++) {
                        const char *f091_key = f091_keys[f091_k];
                        json_view f091_v;
                        int rc = STDLIB_FAIL;
                        f091_v = f091_root.member(f091_key);
                        if (f091_types[f091_k] == 0) {
                            int out = -999999;
                            rc = f091_v.get_int(&out, &f091_err);
                            if (out != f091_ivals[f091_k])
                                f091_cycle_ok = 0;
                        } else if (f091_types[f091_k] == 1) {
                            double out = -999999.0;
                            rc = f091_v.get_double(&out, &f091_err);
                            if (out < f091_dvals[f091_k] - 1e-9 ||
                                out > f091_dvals[f091_k] + 1e-9)
                                f091_cycle_ok = 0;
                        } else if (f091_types[f091_k] == 2) {
                            int out = -999999;
                            rc = f091_v.get_bool(&out, &f091_err);
                            if (out != f091_bvals[f091_k])
                                f091_cycle_ok = 0;
                        } else {
                            const char *out = (const char *)0;
                            rc = f091_v.get_string(&out, &f091_err);
                            if (out == (const char *)0 ||
                                strcmp(out, f091_strvals[f091_k]) != 0)
                                f091_cycle_ok = 0;
                        }
                        if (rc != STDLIB_OK)
                            f091_cycle_ok = 0;
                    }
                    f091_doc.drop();
                } else {
                    f091_cycle_ok = 0;
                }
                json_free(f091_text);
            }
        }
        f091_bld.drop();
        if (!f091_cycle_ok)
            f091_all_ok = 0;
    }
    alarm(0);
    f091_check("f091_builder_roundtrip_cycles", f091_all_ok == 1);

    /* Documented refusals for invalid builder arguments. */
    {
        json_builder f091_reject = json_builder();
        int ok = 1;
        if (f091_reject.add_int((const char *)0, 1, &f091_err) != STDLIB_FAIL ||
            f091_err.category() != STDLIB_CATEGORY_INVALID)
            ok = 0;
        if (f091_reject.add_string("k", (const char *)0, &f091_err) != STDLIB_FAIL ||
            f091_err.category() != STDLIB_CATEGORY_INVALID)
            ok = 0;
        if (f091_reject.add_double("nan", NAN, &f091_err) != STDLIB_FAIL ||
            f091_err.category() != STDLIB_CATEGORY_RANGE)
            ok = 0;
        if (f091_reject.add_double("inf", INFINITY, &f091_err) != STDLIB_FAIL ||
            f091_err.category() != STDLIB_CATEGORY_RANGE)
            ok = 0;
        f091_check("f091_invalid_args_refused", ok == 1);
        f091_reject.drop();
    }

    /* Post-drop refusal; triple drop is idempotent (no UAF). */
    {
        json_builder f091_dropped = json_builder();
        int ok = 1;
        if (!f091_dropped.valid())
            ok = 0;
        if (f091_dropped.add_int("k", 1, &f091_err) != STDLIB_OK)
            ok = 0;
        f091_dropped.drop();
        f091_dropped.drop();
        f091_dropped.drop();
        if (f091_dropped.valid())
            ok = 0;
        if (f091_dropped.add_int("k", 1, &f091_err) != STDLIB_FAIL)
            ok = 0;
        if (f091_dropped.add_double("k", 1.0, &f091_err) != STDLIB_FAIL)
            ok = 0;
        if (f091_dropped.add_bool("k", 1, &f091_err) != STDLIB_FAIL)
            ok = 0;
        if (f091_dropped.add_string("k", "v", &f091_err) != STDLIB_FAIL)
            ok = 0;
        if (f091_dropped.write((size_t *)0) != (char *)0)
            ok = 0;
        f091_check("f091_post_drop_refusal", ok == 1);
    }

    f091_check("f091_op_count", f091_cycle == F091_CYCLES);
    snprintf(f091_hex, sizeof(f091_hex), "%016llx", f091_digest);
    print("F091_DIGEST", f091_hex);
    print("F091_BUILDER", f091_checks, f091_failures);
    return f091_failures == 0 ? 0 : 1;
}
