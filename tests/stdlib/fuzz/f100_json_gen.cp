/* F-089 -- deterministic fuzz config: JSON valid/malformed document generation.
 *
 * Config:  F-089 (f100 json family)
 * Seed:    0xC0FFEE89 (fixed)
 * Ops:     1024 parse/free cycles (512 seeded fixed templates + 512 generated
 *          valid documents), each with validity/type/size assertions.
 * Build:   c+ build --strict --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson
 *          c+ build --strict --sanitize=undefined <same json link flags>
 * Run:     timeout 30 <out>.  Watchdog alarm(8) -> _Exit(99) bounds a parser hang.
 *
 * The fixed template table was calibrated against this exact yyjson wrapper:
 * every "BAD" template is rejected with STDLIB_CATEGORY_INVALID, and every
 * "OK" template's root kind/size is pinned below.  Generated documents are
 * built from a bounded grammar with a fixed-seed LCG, so both the input and
 * the expected outcome are reproducible without calling a parser oracle.
 *
 * Last lines: "F089_GEN <checks> <failures>" and "F089_DIGEST <hex>".
 */
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"

#define F089_SEED 0xC0FFEE89ULL
#define F089_OPS 1024
#define F089_WATCHDOG_SECS 8

/* kind: 0=null/scalar-nonbool, 1=object, 2=array, 3=number, 4=string, 5=bool */
typedef struct {
    const char *text;
    int valid;
    int kind;
    int size;
} f089_template;

static const f089_template f089_templates[] = {
    {"0",                                        1, 3, 0},
    {"true",                                     1, 5, 0},
    {"false",                                    1, 5, 0},
    {"null",                                     1, 0, 0},
    {"\"hi\"",                                   1, 4, 0},
    {"[1,2,3]",                                  1, 2, 3},
    {"[]",                                       1, 2, 0},
    {"{}",                                       1, 1, 0},
    {"{\"a\":1,\"b\":[2,3],\"c\":{\"d\":4}}",     1, 1, 3},
    {"{\"s\":\"\\u00e9\\n\\t\"}",                 1, 1, 1},
    {"{\"big\":3000000000,\"neg\":-5,\"d\":2.5}", 1, 1, 3},
    {"[[1],[2,3],[]]",                           1, 2, 3},
    {"-1.25e10",                                 1, 3, 0},
    {"",                                         0, 0, 0},
    {"   \n\t ",                                 0, 0, 0},
    {"{",                                        0, 0, 0},
    {"[1,2",                                     0, 0, 0},
    {"{\"a\":1,}",                               0, 0, 0},
    {"[1,]",                                     0, 0, 0},
    {"{\"a\":\"\\q\"}",                          0, 0, 0},
    {"{'a':1}",                                  0, 0, 0},
    {"{a:1}",                                    0, 0, 0},
    {"[1 2]",                                    0, 0, 0},
    {"true false",                               0, 0, 0},
    {"{\"a\":1} trailing",                       0, 0, 0},
    {"NaN",                                      0, 0, 0},
    {"01",                                       0, 0, 0},
    {"+1",                                       0, 0, 0},
    {"{\"a\":1,,\"b\":2}",                       0, 0, 0},
    {"[[[]",                                     0, 0, 0},
    {"{\"a\":}",                                 0, 0, 0},
    {"\"unterminated",                           0, 0, 0},
    {"/*c*/ 1",                                  0, 0, 0},
    {"1e",                                       0, 0, 0},
    {"-",                                        0, 0, 0},
    {".5",                                       0, 0, 0},
    {"1.",                                       0, 0, 0},
    {"[-]",                                      0, 0, 0}
};
#define F089_TEMPLATE_N ((int)(sizeof(f089_templates) / sizeof(f089_templates[0])))

static unsigned int f089_failures = 0;
static unsigned int f089_checks = 0;

static void f089_check(const char *tag, int cond) {
    f089_checks++;
    if (!cond)
        f089_failures++;
    print(tag, cond ? 1 : 0);
}

static void f089_on_alarm(int sig) {
    (void)sig;
    _Exit(99);
}

static unsigned long long f089_rnd(unsigned long long *s) {
    *s = *s * 6364136223846793005ULL + 1442695040888963407ULL;
    return *s >> 33;
}

#define F089_FNV_PRIME 1099511628211ULL
static unsigned long long f089_mix(unsigned long long d, unsigned long long v) {
    return (d ^ v) * F089_FNV_PRIME;
}

static int f089_kind(json_view *v) {
    if (v->is_object())
        return 1;
    if (v->is_array())
        return 2;
    if (v->is_number())
        return 3;
    if (v->is_string())
        return 4;
    if (v->is_bool())
        return 5;
    return 0;
}

int main(void) {
    error f089_err = error();
    unsigned long long f089_state = F089_SEED;
    unsigned long long f089_digest = 1469598103934665603ULL;
    char f089_hex[32];
    int f089_i;
    int f089_fixed_ok = 1;
    int f089_generated_ok = 1;

    signal(SIGALRM, f089_on_alarm);
    alarm(F089_WATCHDOG_SECS);

    for (f089_i = 0; f089_i < F089_OPS; f089_i++) {
        char f089_buf[160];
        const char *text;
        int expected_valid, expected_kind, expected_size;
        json f089_doc;
        int valid;

        if (f089_i < F089_OPS / 2) {
            unsigned long long r = f089_rnd(&f089_state);
            int idx = (int)(r % (unsigned long long)F089_TEMPLATE_N);
            text = f089_templates[idx].text;
            expected_valid = f089_templates[idx].valid;
            expected_kind = f089_templates[idx].kind;
            expected_size = f089_templates[idx].size;
            f089_digest = f089_mix(f089_digest, (unsigned long long)idx);
        } else {
            unsigned long long ra = f089_rnd(&f089_state);
            unsigned long long rb = f089_rnd(&f089_state);
            unsigned long long rc = f089_rnd(&f089_state);
            int a = (int)(ra % 1000ULL);
            int b = (int)(rb % 1000ULL);
            int flag = (int)(rc & 1ULL);
            const char *word = (rc & 2ULL) ? "alpha" : "beta";
            snprintf(f089_buf, sizeof(f089_buf),
                     "{\"a\":%d,\"b\":%d,\"c\":\"%s\",\"d\":%s}",
                     a, b, word, flag ? "true" : "false");
            text = f089_buf;
            expected_valid = 1;
            expected_kind = 1;
            expected_size = 4;
            f089_digest = f089_mix(f089_digest, (unsigned long long)a);
            f089_digest = f089_mix(f089_digest, (unsigned long long)b);
            f089_digest = f089_mix(f089_digest, (unsigned long long)flag);
        }

        f089_doc = json(text, &f089_err);
        valid = f089_doc.valid();
        if (valid != expected_valid) {
            if (f089_i < F089_OPS / 2)
                f089_fixed_ok = 0;
            else
                f089_generated_ok = 0;
        }
        f089_digest = f089_mix(f089_digest, (unsigned long long)valid);
        if (valid) {
            json_view f089_root = f089_doc.root();
            int kind = f089_kind(&f089_root);
            size_t sz = f089_root.size();
            if (kind != expected_kind || (int)sz != expected_size) {
                if (f089_i < F089_OPS / 2)
                    f089_fixed_ok = 0;
                else
                    f089_generated_ok = 0;
            }
            f089_digest = f089_mix(f089_digest, (unsigned long long)kind);
            f089_digest = f089_mix(f089_digest, (unsigned long long)sz);
            /* Bounded traversal of array elements: at(i) must be a valid view
             * for every in-range index and invalid past the end. */
            if (f089_root.is_array()) {
                size_t j;
                for (j = 0; j < sz && j < 8; j++) {
                    json_view f089_item = f089_root.at(j);
                    if (!f089_item.valid())
                        f089_generated_ok = 0;
                }
                {
                    json_view f089_past = f089_root.at(sz);
                    if (f089_past.valid())
                        f089_generated_ok = 0;
                }
            }
            f089_doc.drop();
        } else if (f089_err.category() != STDLIB_CATEGORY_INVALID) {
            if (f089_i < F089_OPS / 2)
                f089_fixed_ok = 0;
            else
                f089_generated_ok = 0;
        }
        f089_doc.drop();             /* idempotent on already-dropped docs */
    }
    alarm(0);

    f089_check("f089_fixed_templates", f089_fixed_ok == 1);
    f089_check("f089_generated_documents", f089_generated_ok == 1);
    f089_check("f089_op_count", f089_i == F089_OPS);

    /* json_free(NULL) is a documented no-op. */
    json_free((char *)0);
    f089_check("f089_json_free_null", 1);

    snprintf(f089_hex, sizeof(f089_hex), "%016llx", f089_digest);
    print("F089_DIGEST", f089_hex);
    print("F089_GEN", f089_checks, f089_failures);
    return f089_failures == 0 ? 0 : 1;
}
