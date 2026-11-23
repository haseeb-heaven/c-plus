/* F-092 -- deterministic fuzz config: repeated JSON parse/free cycles.
 *
 * Config:  F-092 (f100 json family)
 * Seed:    0xC0FFEE92 (fixed)
 * Ops:     1024 cycles; each cycle parses a generated document (and a seeded
 *          malformed variant), traverses views, serialises with write(),
 *          re-parses the serialised buffer, and frees both docs/buffers.
 * Build:   --strict --sanitize=none and --strict --sanitize=undefined, each
 *          with -Ithird_party/yyjson -Lbuild/ext -lyyjson as separate operands.
 * Run:     timeout 30 <out>.  Watchdog alarm(8) -> _Exit(99).
 *
 * This target stresses allocator churn and, critically, the borrowed-view /
 * owned-document lifetime split: every view is consumed before drop(), and
 * every write() buffer is released with json_free() exactly once.  Refusal of
 * malformed variants must be a clean error, never a crash.
 *
 * Last lines: "F092_CYCLES <checks> <failures>" and "F092_DIGEST <hex>".
 */
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"

#define F092_SEED 0xC0FFEE92ULL
#define F092_CYCLES 1024
#define F092_WATCHDOG_SECS 8

static unsigned int f092_failures = 0;
static unsigned int f092_checks = 0;

static void f092_check(const char *tag, int cond) {
    f092_checks++;
    if (!cond)
        f092_failures++;
    print(tag, cond ? 1 : 0);
}

static void f092_on_alarm(int sig) {
    (void)sig;
    _Exit(99);
}

static unsigned long long f092_rnd(unsigned long long *s) {
    *s = *s * 6364136223846793005ULL + 1442695040888963407ULL;
    return *s >> 33;
}

#define F092_FNV_PRIME 1099511628211ULL
static unsigned long long f092_mix(unsigned long long d, unsigned long long v) {
    return (d ^ v) * F092_FNV_PRIME;
}

int main(void) {
    error f092_err = error();
    unsigned long long f092_state = F092_SEED;
    unsigned long long f092_digest = 1469598103934665603ULL;
    char f092_hex[32];
    int f092_i;
    int f092_all_ok = 1;

    signal(SIGALRM, f092_on_alarm);
    alarm(F092_WATCHDOG_SECS);

    for (f092_i = 0; f092_i < F092_CYCLES; f092_i++) {
        unsigned long long r0 = f092_rnd(&f092_state);
        unsigned long long r1 = f092_rnd(&f092_state);
        unsigned long long r2 = f092_rnd(&f092_state);
        int variant = (int)(r0 % 3ULL);
        int a = (int)(r1 % 2000000000ULL) - 1000000000;
        int b = (int)(r2 % 1000ULL);
        char f092_buf[128];
        int expect_size = 0;
        int cycle_ok = 1;

        if (variant == 0) {
            snprintf(f092_buf, sizeof(f092_buf),
                     "{\"n\":%d,\"s\":\"v%d\",\"b\":true}", a, b);
            expect_size = 3;
        } else if (variant == 1) {
            snprintf(f092_buf, sizeof(f092_buf), "[%d,%d,%d]", a, b, a + b);
            expect_size = 3;
        } else {
            snprintf(f092_buf, sizeof(f092_buf), "%d", a);
            expect_size = 0;
        }

        {
            json f092_doc = json(f092_buf, &f092_err);
            if (f092_doc.valid()) {
                json_view f092_root = f092_doc.root();
                int got_value = -999999;
                if ((int)f092_root.size() != expect_size)
                    cycle_ok = 0;
                if (variant == 0) {
                    json_view f092_v = f092_root.member("n");
                    if (f092_v.get_int(&got_value, &f092_err) != STDLIB_OK ||
                        got_value != a)
                        cycle_ok = 0;
                } else if (variant == 1) {
                    json_view f092_v = f092_root.at(0);
                    if (f092_v.get_int(&got_value, &f092_err) != STDLIB_OK ||
                        got_value != a)
                        cycle_ok = 0;
                } else {
                    if (f092_root.get_int(&got_value, &f092_err) != STDLIB_OK ||
                        got_value != a)
                        cycle_ok = 0;
                }

                /* Serialise, re-parse the borrowed buffer, verify, free. */
                {
                    size_t f092_len = 0;
                    char *f092_text = f092_root.write(&f092_len);
                    if (f092_text == (char *)0 || f092_len == 0) {
                        cycle_ok = 0;
                    } else {
                        json f092_round = json(f092_text, &f092_err);
                        if (f092_round.valid()) {
                            json_view f092_round_root = f092_round.root();
                            if ((int)f092_round_root.size() != expect_size)
                                cycle_ok = 0;
                            if (variant == 0) {
                                json_view f092_rv = f092_round_root.member("n");
                                int rv = -999999;
                                if (f092_rv.get_int(&rv, &f092_err) != STDLIB_OK ||
                                    rv != a)
                                    cycle_ok = 0;
                            }
                            f092_round.drop();
                        } else {
                            cycle_ok = 0;
                        }
                        json_free(f092_text);
                    }
                }
                f092_doc.drop();
                f092_doc.drop();          /* idempotent */
            } else {
                cycle_ok = 0;
            }
        }

        /* Seeded malformed variant: clean refusal, never a crash. */
        {
            const char *bad;
            if ((r0 >> 8) % 3ULL == 0)
                bad = "{";
            else if ((r0 >> 8) % 3ULL == 1)
                bad = "[1,]";
            else
                bad = "{\"n\":}";
            {
                json f092_bad = json(bad, &f092_err);
                if (f092_bad.valid() ||
                    f092_err.category() != STDLIB_CATEGORY_INVALID)
                    cycle_ok = 0;
                f092_bad.drop();
            }
        }

        if (!cycle_ok)
            f092_all_ok = 0;
        f092_digest = f092_mix(f092_digest, (unsigned long long)variant);
        f092_digest = f092_mix(f092_digest, (unsigned long long)a);
        f092_digest = f092_mix(f092_digest, (unsigned long long)b);
    }
    alarm(0);

    f092_check("f092_parse_free_cycles", f092_all_ok == 1);
    f092_check("f092_op_count", f092_i == F092_CYCLES);
    json_free((char *)0);
    f092_check("f092_json_free_null", 1);

    snprintf(f092_hex, sizeof(f092_hex), "%016llx", f092_digest);
    print("F092_DIGEST", f092_hex);
    print("F092_CYCLES", f092_checks, f092_failures);
    return f092_failures == 0 ? 0 : 1;
}
