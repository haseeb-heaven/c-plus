/* Deterministic, bounded token-bucket rate limiter (secure-app example).
 *
 * This program is a TEACHING example of checked / defensive C+ style.  It is
 * NOT a proof of security.  It deliberately uses no wall clock: every "time
 * tick" is supplied as a fixed integer on the command line, so the transcript
 * is fully reproducible and can be diffed by a test harness.
 *
 * Command line:
 *   ratelimit <capacity> <refill> <n> [<tick0> <cost0>] ... [<tickN-1> <costN-1>]
 *
 *   capacity  - bucket size in tokens; must be 1..RL_MAX_CAPACITY
 *   refill    - tokens added per elapsed tick; must be 1..RL_MAX_REFILL
 *   n         - number of requests; must be 0..RL_MAX_REQUESTS
 *   tick/cost - one pair per request.  ticks must be >= 0 and non-decreasing;
 *               cost must be 1..capacity (a larger request could never pass).
 *
 * The bucket starts full at tick 0.  For each request the limiter refills by
 * (tick - last_tick) * refill tokens (capped at capacity), then ALLOWs and
 * debits when the bucket has enough tokens, otherwise DENYs without debiting.
 *
 * Every rejection is a first-class, observable event:
 *   REFUSED <code> <NAME> <detail>
 * and the process exits with that same <code>.  See NOTES.md for the full
 * threat model and the exact build / run commands.
 *
 * Build (from repo root):
 *   ./build/c+ build examples/secure_apps/ratelimit/ratelimit.cp --strict --sanitize=none -I. -o /tmp/ratelimit
 */
#include "cplus/cplus.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- Hard bounds: bounded arrays, no dynamic/unbounded growth ---------- */
#define RL_MAX_CAPACITY 1000000L
#define RL_MAX_REFILL   1000000L
#define RL_MAX_REQUESTS 64

/* ---- Refusal codes (also used as the process exit status) -------------- */
#define RL_ERR_ARGS      1 /* wrong number of arguments                     */
#define RL_ERR_PARSE     2 /* a numeric argument failed checked parsing     */
#define RL_ERR_CAPACITY  3 /* capacity <= 0 or > RL_MAX_CAPACITY           */
#define RL_ERR_REFILL    4 /* refill <= 0 or > RL_MAX_REFILL               */
#define RL_ERR_COUNT     5 /* request count < 0 or > RL_MAX_REQUESTS       */
#define RL_ERR_TICK      6 /* negative or non-monotonic tick               */
#define RL_ERR_COST      7 /* cost <= 0 or > capacity                      */
#define RL_ERR_OVERFLOW  8 /* overflow in bucket arithmetic                */

/* Bounded storage: fixed-size arrays, indexed only within [0, RL_MAX_REQUESTS). */
static long g_ticks[RL_MAX_REQUESTS];
static long g_costs[RL_MAX_REQUESTS];
static long g_level[RL_MAX_REQUESTS];   /* bucket level after refill, before debit */
static long g_after[RL_MAX_REQUESTS];   /* bucket level after the decision        */
static int  g_decision[RL_MAX_REQUESTS]; /* 1 = ALLOW, 0 = DENY                    */

/* Strict mode cannot prove the extent of argv via direct subscripting, so all
 * argv reads go through one helper that uses a bounded __builtin_memcpy. */
static char *rl_arg_at(char **argv, int idx) {
    char *value;
    __builtin_memcpy(&value, argv + idx, sizeof(value));
    return value;
}

/* Checked decimal-to-long conversion: rejects empty strings, trailing junk,
 * and out-of-range values via errno (all via the standard checked stdlib). */
static int rl_parse_long(const char *text, long *out) {
    char *end = NULL;
    long value;
    if (text == NULL)
        return -1;
    errno = 0;
    value = strtol(text, &end, 10);
    if (errno == ERANGE)
        return -1;
    if (end == text)
        return -1;
    if (*end != '\0')
        return -1;
    *out = value;
    return 0;
}

static int rl_refuse(int code, const char *name, const char *detail) {
    print("REFUSED", code, name, detail);
    return code;
}

int main(int argc, char **argv) {
    long capacity = 0;
    long refill = 0;
    long n_long = 0;
    int n = 0;
    int i = 0;
    long tokens = 0;
    long last_tick = 0;
    long allowed = 0;
    long denied = 0;
    long long elapsed = 0;
    long long added = 0;
    long long level = 0;

    /* ---- 1. Argument count -------------------------------------------- */
    if (argc < 4)
        return rl_refuse(RL_ERR_ARGS, "ARGS", "need capacity refill n [tick cost]...");

    /* ---- 2. Checked parsing of the three control integers ------------- */
    if (rl_parse_long(rl_arg_at(argv, 1), &capacity) != 0)
        return rl_refuse(RL_ERR_PARSE, "PARSE", "capacity");
    if (rl_parse_long(rl_arg_at(argv, 2), &refill) != 0)
        return rl_refuse(RL_ERR_PARSE, "PARSE", "refill");
    if (rl_parse_long(rl_arg_at(argv, 3), &n_long) != 0)
        return rl_refuse(RL_ERR_PARSE, "PARSE", "n");

    /* ---- 3. Config validation ----------------------------------------- */
    if (capacity <= 0 || capacity > RL_MAX_CAPACITY)
        return rl_refuse(RL_ERR_CAPACITY, "CAPACITY", "out of range");
    if (refill <= 0 || refill > RL_MAX_REFILL)
        return rl_refuse(RL_ERR_REFILL, "REFILL", "out of range");
    if (n_long < 0 || n_long > RL_MAX_REQUESTS)
        return rl_refuse(RL_ERR_COUNT, "COUNT", "out of range");
    n = (int)n_long;

    /* Exactly n (tick, cost) pairs must follow the three control integers. */
    if (argc != 4 + 2 * n)
        return rl_refuse(RL_ERR_ARGS, "ARGS", "tick/cost pair count mismatch");

    /* ---- 4. Parse and validate the whole bounded request sequence ------ */
    for (i = 0; i < n; ++i) {
        long tick = 0;
        long cost = 0;
        if (rl_parse_long(rl_arg_at(argv, 4 + 2 * i), &tick) != 0)
            return rl_refuse(RL_ERR_PARSE, "PARSE", "tick");
        if (rl_parse_long(rl_arg_at(argv, 5 + 2 * i), &cost) != 0)
            return rl_refuse(RL_ERR_PARSE, "PARSE", "cost");
        if (tick < 0)
            return rl_refuse(RL_ERR_TICK, "TICK", "negative tick");
        if (i > 0 && tick < g_ticks[i - 1])
            return rl_refuse(RL_ERR_TICK, "TICK", "non-monotonic tick");
        if (cost <= 0 || cost > capacity)
            return rl_refuse(RL_ERR_COST, "COST", "out of range");
        g_ticks[i] = tick;
        g_costs[i] = cost;
    }

    /* ---- 5. Deterministic simulation (pre-pass) -----------------------
     * Compute the entire bounded transcript first.  This keeps the refusal
     * contract crisp: every input is either a complete transcript or a single
     * REFUSED line, never a partial transcript followed by REFUSED. */
    tokens = capacity; /* bucket starts full at tick 0 */
    last_tick = 0;

    for (i = 0; i < n; ++i) {
        long tick = g_ticks[i];
        long cost = g_costs[i];

        /* elapsed * refill is the only multiplication in the model. */
        elapsed = (long long)tick - (long long)last_tick; /* >= 0, monotonic */
        if (__builtin_mul_overflow(elapsed, (long long)refill, &added))
            return rl_refuse(RL_ERR_OVERFLOW, "OVERFLOW", "refill multiply");
        if (__builtin_add_overflow((long long)tokens, added, &level))
            return rl_refuse(RL_ERR_OVERFLOW, "OVERFLOW", "bucket add");

        if (level > (long long)capacity)
            level = (long long)capacity; /* cap at bucket capacity */

        if (level >= (long long)cost) {
            tokens = (long)(level - (long long)cost);
            g_decision[i] = 1;
        } else {
            tokens = (long)level;
            g_decision[i] = 0;
        }
        g_level[i] = (long)level;
        g_after[i] = tokens;
        last_tick = tick;
    }

    /* ---- 6. Emit the deterministic transcript -------------------------- */
    print("SECURE_RL_CONFIG", capacity, refill, n);
    for (i = 0; i < n; ++i) {
        if (g_decision[i]) {
            print("SECURE_RL_REQ", i, g_ticks[i], g_costs[i], g_level[i],
                  "ALLOW", g_after[i]);
            allowed++;
        } else {
            print("SECURE_RL_REQ", i, g_ticks[i], g_costs[i], g_level[i],
                  "DENY", g_after[i]);
            denied++;
        }
    }
    print("SECURE_RL_DONE", allowed, denied, tokens);
    return 0;
}
