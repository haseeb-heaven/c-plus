/* F-090 -- deterministic fuzz config: JSON typed getters incl. wrong types.
 *
 * Config:  F-090 (f100 json family)
 * Seed:    0xC0FFEE90 (fixed)
 * Ops:     1024 seeded (member, getter) combinations against one mixed-type
 *          document, plus NULL-out / NULL-error / invalid-view refusals.
 * Build:   --strict --sanitize=none and --strict --sanitize=undefined, each
 *          with -Ithird_party/yyjson -Lbuild/ext -lyyjson as separate operands.
 * Run:     timeout 30 <out>.  Watchdog alarm(8) -> _Exit(99).
 *
 * Every expected outcome is derived from the documented json_view contract:
 * a getter succeeds only for its exact type (get_double also accepts any
 * number), refuses a wrong type with STDLIB_CATEGORY_INVALID and leaves the
 * out-parameter untouched, and get_int on a > INT_MAX unsigned refuses with
 * STDLIB_CATEGORY_RANGE.  Effects were calibrated against this wrapper before
 * the expected table was written; no oracle call is made during the run.
 *
 * Last lines: "F090_GETTERS <checks> <failures>" and "F090_DIGEST <hex>".
 */
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"

#define F090_SEED 0xC0FFEE90ULL
#define F090_OPS 1024
#define F090_WATCHDOG_SECS 8

/* distinct expected type tags for the member table */
#define F090_T_INT 1
#define F090_T_NUM 2
#define F090_T_STR 3
#define F090_T_BOOL 4
#define F090_T_NULL 5
#define F090_T_ARR 6
#define F090_T_UINT_BIG 7

typedef struct {
    const char *key;
    int type;
    int int_value;          /* valid for T_INT */
    double double_value;    /* valid for T_INT / T_NUM / T_UINT_BIG */
    int bool_value;         /* valid for T_BOOL */
} f090_member;

static const f090_member f090_members[] = {
    {"i",      F090_T_INT,       7,   7.0,          0},
    {"neg",    F090_T_INT,      -5,  -5.0,          0},
    {"empty",  F090_T_INT,       0,   0.0,          0},
    {"d",      F090_T_NUM,       0,   2.5,          0},
    {"big",    F090_T_UINT_BIG,  0,   3000000000.0, 0},
    {"s",      F090_T_STR,       0,   0.0,          0},
    {"t",      F090_T_BOOL,      0,   0.0,          1},
    {"f",      F090_T_BOOL,      0,   0.0,          0},
    {"n",      F090_T_NULL,      0,   0.0,          0},
    {"arr",    F090_T_ARR,       0,   0.0,          0},
    {"missing", F090_T_NULL,     0,   0.0,          0}
};
#define F090_MEMBER_N ((int)(sizeof(f090_members) / sizeof(f090_members[0])))

/* getter ids: 0 get_int, 1 get_double, 2 get_bool, 3 get_string */
static const char *f090_doc_text =
    "{\"i\":7,\"neg\":-5,\"empty\":0,\"d\":2.5,\"big\":3000000000,"
    "\"s\":\"txt\",\"t\":true,\"f\":false,\"n\":null,\"arr\":[1,2]}";

static unsigned int f090_failures = 0;
static unsigned int f090_checks = 0;

static void f090_check(const char *tag, int cond) {
    f090_checks++;
    if (!cond)
        f090_failures++;
    print(tag, cond ? 1 : 0);
}

static void f090_on_alarm(int sig) {
    (void)sig;
    _Exit(99);
}

static unsigned long long f090_rnd(unsigned long long *s) {
    *s = *s * 6364136223846793005ULL + 1442695040888963407ULL;
    return *s >> 33;
}

#define F090_FNV_PRIME 1099511628211ULL
static unsigned long long f090_mix(unsigned long long d, unsigned long long v) {
    return (d ^ v) * F090_FNV_PRIME;
}

/* 1 = this getter should succeed for this member type, else 0 */
static int f090_expected_ok(int type, int getter) {
    if (getter == 0)
        return type == F090_T_INT;                 /* get_int: exact int only */
    if (getter == 1)
        return type == F090_T_INT || type == F090_T_NUM || type == F090_T_UINT_BIG;
    if (getter == 2)
        return type == F090_T_BOOL;
    return type == F090_T_STR;                      /* get_string */
}

int main(void) {
    error f090_err = error();
    unsigned long long f090_state = F090_SEED;
    unsigned long long f090_digest = 1469598103934665603ULL;
    char f090_hex[32];
    int f090_i = 0;
    int f090_all_ok = 1;

    signal(SIGALRM, f090_on_alarm);
    alarm(F090_WATCHDOG_SECS);

    {
        json f090_doc = json(f090_doc_text, &f090_err);
        if (f090_doc.valid()) {
            json_view f090_root = f090_doc.root();
            for (f090_i = 0; f090_i < F090_OPS; f090_i++) {
                unsigned long long r = f090_rnd(&f090_state);
                int mi = (int)((r >> 3) % (unsigned long long)F090_MEMBER_N);
                int getter = (int)(r & 3ULL);
                const f090_member *m = &f090_members[mi];
                json_view f090_v = f090_root.member(m->key);
                int iv = -777777;
                int bv = -777777;
                double dv = -777777.0;
                const char *sv = (const char *)0;
                int rc = STDLIB_FAIL;
                int want = f090_expected_ok(m->type, getter);
                int ok = 1;

                if (getter == 0)
                    rc = f090_v.get_int(&iv, &f090_err);
                else if (getter == 1)
                    rc = f090_v.get_double(&dv, &f090_err);
                else if (getter == 2)
                    rc = f090_v.get_bool(&bv, &f090_err);
                else
                    rc = f090_v.get_string(&sv, &f090_err);

                if ((rc == STDLIB_OK) != (want == 1))
                    ok = 0;
                if (want == 1) {
                    if (!f090_err.ok())
                        ok = 0;
                    if (getter == 0 && iv != m->int_value)
                        ok = 0;
                    if (getter == 1 &&
                        (dv < m->double_value - 1e-9 || dv > m->double_value + 1e-9))
                        ok = 0;
                    if (getter == 2 && bv != m->bool_value)
                        ok = 0;
                    if (getter == 3 && (sv == (const char *)0 || strcmp(sv, "txt") != 0))
                        ok = 0;
                } else {
                    int cat = f090_err.category();
                    int want_cat = (getter == 0 && m->type == F090_T_UINT_BIG)
                                       ? STDLIB_CATEGORY_RANGE
                                       : STDLIB_CATEGORY_INVALID;
                    if (cat != want_cat)
                        ok = 0;
                    if (getter == 0 && iv != -777777)
                        ok = 0;
                    if (getter == 1 && dv != -777777.0)
                        ok = 0;
                    if (getter == 2 && bv != -777777)
                        ok = 0;
                    if (getter == 3 && sv != (const char *)0)
                        ok = 0;
                }
                if (!ok)
                    f090_all_ok = 0;
                f090_digest = f090_mix(f090_digest, (unsigned long long)mi);
                f090_digest = f090_mix(f090_digest, (unsigned long long)getter);
                f090_digest = f090_mix(f090_digest, (unsigned long long)rc);
            }
            f090_doc.drop();
        } else {
            f090_all_ok = 0;
        }
    }

    /* NULL out-pointer and NULL error-pointer refusal on a real int member. */
    {
        json f090_null_doc = json(f090_doc_text, &f090_err);
        if (f090_null_doc.valid()) {
            json_view f090_root2 = f090_null_doc.root();
            json_view f090_int_view = f090_root2.member("i");
            json_view f090_bool_view = f090_root2.member("t");
            int iv = 55;
            int bv = 66;
            double dv = 77.0;
            const char *sv = (const char *)0;
            int ok = 1;
            if (f090_int_view.get_int((int *)0, &f090_err) != STDLIB_FAIL)
                ok = 0;
            if (f090_int_view.get_double((double *)0, &f090_err) != STDLIB_FAIL)
                ok = 0;
            if (f090_int_view.get_bool((int *)0, &f090_err) != STDLIB_FAIL)
                ok = 0;
            if (f090_int_view.get_string((const char **)0, &f090_err) != STDLIB_FAIL)
                ok = 0;
            if (iv != 55 || bv != 66 || dv != 77.0 || sv != (const char *)0)
                ok = 0;
            /* NULL error pointer still enforces the type contract. */
            if (f090_int_view.get_int((int *)0, (error *)0) != STDLIB_FAIL)
                ok = 0;
            if (f090_bool_view.get_int(&iv, (error *)0) != STDLIB_FAIL || iv != 55)
                ok = 0;
            f090_check("f090_null_out_and_error_args", ok == 1);
            f090_null_doc.drop();
        } else {
            f090_check("f090_null_out_and_error_args", 0);
        }
    }

    /* json_view_of(NULL): every getter refuses, view reports invalid. */
    {
        json_view f090_nil = json_view_of((const yyjson_val *)0);
        int iv = 4242;
        double dv = 4242.0;
        int bv = 4242;
        const char *sv = (const char *)0;
        int ok = 1;
        if (f090_nil.valid() || f090_nil.size() != 0)
            ok = 0;
        if (f090_nil.get_int(&iv, &f090_err) != STDLIB_FAIL || iv != 4242)
            ok = 0;
        if (f090_nil.get_double(&dv, &f090_err) != STDLIB_FAIL || dv != 4242.0)
            ok = 0;
        if (f090_nil.get_bool(&bv, &f090_err) != STDLIB_FAIL || bv != 4242)
            ok = 0;
        if (f090_nil.get_string(&sv, &f090_err) != STDLIB_FAIL || sv != (const char *)0)
            ok = 0;
        if (f090_err.category() != STDLIB_CATEGORY_INVALID)
            ok = 0;
        f090_check("f090_invalid_view_refusal", ok == 1);
    }

    f090_check("f090_typed_getter_matrix", f090_all_ok == 1);
    f090_check("f090_op_count", f090_i == F090_OPS);

    alarm(0);
    snprintf(f090_hex, sizeof(f090_hex), "%016llx", f090_digest);
    print("F090_DIGEST", f090_hex);
    print("F090_GETTERS", f090_checks, f090_failures);
    return f090_failures == 0 ? 0 : 1;
}
