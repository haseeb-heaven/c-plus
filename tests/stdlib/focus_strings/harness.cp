/* focus_strings: deterministic differential harness for stdlib/string.hp.
 *
 * Method (per the parent task):
 *   - fixed-seed xorshift32 PRNG (no libc rand, no time dependence)
 *   - one forked child per generated case, so a fatal runtime error or signal
 *     is isolated to that child and classified as CRASH
 *   - canary guards around stack out-buffers and around the model state
 *   - an independent reference model (plain char buffer + length) predicts
 *     size/content/invariants after every operation
 *   - child exit status encodes OK / CORRUPT / UNEXPECTED_OK /
 *     UNEXPECTED_REFUSED; parent also detects CRASH via waitpid
 *
 * Classification (child exit codes):
 *   0  OK            - actual behaviour matches the reference model
 *   1  CORRUPT       - content/size/canary/invariant mismatch
 *   2  UNEXPECTED_OK - model predicted refusal but operation succeeded
 *   3  UNEXPECTED_REFUSED - model predicted success but operation refused
 *   other/signal -> CRASH (parent side)
 *
 * Build:
 *   ./build/c+ build tests/stdlib/focus_strings/harness.cp --strict \
 *       --sanitize=none -I. -o <bin>
 * Run:
 *   timeout 60 <bin> [num_cases]      (default 1500)
 */
#define STDLIB_ALLOC_LIMIT ((size_t)4096)
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"

/* ------------------------------------------------------------------ PRNG */
static unsigned int rng_state;
static void rng_seed(unsigned int seed) {
    unsigned int x = seed;
    if (x == 0)
        x = 0x9E3779B9u;
    rng_state = x;
}
static unsigned int rnd(void) {
    unsigned int x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}
static size_t rnd_range(size_t lo, size_t hi) {
    /* hi inclusive; assume hi - lo < 2^31 and no variable division */
    size_t span = hi - lo + 1u;
    return lo + ((size_t)rnd() & (span - 1u));
}

/* --------------------------------------------------------------- canaries */
#define CANARY_BYTES 32
static unsigned char g_canary[CANARY_BYTES];
static void canary_init(void) {
    size_t i;
    for (i = 0; i < CANARY_BYTES; i++)
        g_canary[i] = (unsigned char)(0xA5u ^ (i & 0xFFu));
}
static int canary_ok(void) {
    size_t i;
    for (i = 0; i < CANARY_BYTES; i++)
        if (g_canary[i] != (unsigned char)(0xA5u ^ (i & 0xFFu)))
            return 0;
    return 1;
}

/* --------------------------------------------------------- literal pool */
static const char *const LITS[] = {
    "", "a", "ab", "abc", "hello", "0123456789",
    "The quick brown fox", "x", "ZZZZZZZZZZZZZZZZZZZZ",
    "abcdefghijklmnopqrstuvwxyz"
};
#define NLIT ((size_t)(sizeof(LITS) / sizeof(LITS[0])))

/* ------------------------------------------------------- reference model */
#define REF_CAP 4200
static char ref[REF_CAP];
static size_t ref_len;

static void ref_reset(void) {
    ref[0] = '\0';
    ref_len = 0;
}
static void ref_append(const char *s, size_t n) {
    size_t i;
    for (i = 0; i < n; i++)
        ref[ref_len + i] = s[i];
    ref_len += n;
    ref[ref_len] = '\0';
}

/* --------------------------------------------------------- child worker */
#define EXIT_OK 0
#define EXIT_CORRUPT 1
#define EXIT_UNEXPECTED_OK 2
#define EXIT_UNEXPECTED_REFUSED 3

static int g_bad;
static void bad(void) { g_bad = 1; }

static int check_actual(string *s) {
    size_t i;
    if (s->size() != ref_len) {
        bad();
        return 0;
    }
    if (s->capacity() < ref_len + 1u || s->capacity() > STDLIB_ALLOC_LIMIT) {
        bad();
        return 0;
    }
    if (ref_len == 0) {
        if (s->c_str()[0] != '\0') {
            bad();
            return 0;
        }
    } else {
        for (i = 0; i < ref_len; i++) {
            if (s->c_str()[i] != ref[i]) {
                bad();
                return 0;
            }
        }
        if (s->c_str()[ref_len] != '\0') {
            bad();
            return 0;
        }
    }
    if (!canary_ok()) {
        bad();
        return 0;
    }
    return 1;
}

static int child_case(unsigned int seed) {
    string s = string("seed");
    string other = string("other");
    unsigned int i;
    unsigned int nops;

    g_bad = 0;
    rng_seed(seed);
    canary_init();
    ref_reset();
    ref_append("seed", 4);

    if (!check_actual(&s)) {
        fprintf(stderr, "case %u: init mismatch\n", seed);
        return EXIT_CORRUPT;
    }

    nops = 4u + (rnd() & 7u); /* 4..11 ops */
    for (i = 0; i < nops; i++) {
        unsigned int op = rnd() & 15u;
        size_t want;
        size_t off;
        int rc;

        switch (op) {
        case 0: case 1: case 2: case 3: {
            const char *lit = LITS[rnd_range(0, NLIT - 1u)];
            size_t n = strlen(lit);
            ref_append(lit, n);
            rc = s.append(lit);
            if (rc != 0 || !check_actual(&s)) {
                fprintf(stderr, "case %u op %u append-lit\n", seed, i);
                return EXIT_CORRUPT;
            }
            break;
        }
        case 4: case 5: case 6: { /* append self suffix, may reallocate */
            if (ref_len == 0) {
                /* append empty terminator: must be a no-op */
                rc = s.append(s.c_str());
                if (rc != 0 || !check_actual(&s)) {
                    fprintf(stderr, "case %u op %u self-empty\n", seed, i);
                    return EXIT_CORRUPT;
                }
                break;
            }
            off = rnd_range(0, ref_len); /* 0..len (len => empty suffix) */
            {
                size_t n = ref_len - off;
                ref_append(ref + off, n);
            }
            rc = s.append(s.c_str() + off);
            if (rc != 0 || !check_actual(&s)) {
                fprintf(stderr, "case %u op %u self-suffix off=%lu\n",
                        seed, i, (unsigned long)off);
                return EXIT_CORRUPT;
            }
            break;
        }
        case 7: { /* clear then append a stale interior pointer */
            size_t old_len = ref_len;
            if (old_len == 0)
                break;
            off = rnd_range(1, old_len); /* 1..old_len */
            s.clear();
            ref_reset();
            if (!check_actual(&s)) {
                fprintf(stderr, "case %u op %u clear\n", seed, i);
                return EXIT_CORRUPT;
            }
            /* after clear, only byte 0 is zeroed; bytes [off, old_len) still
             * hold the previous contents and the NUL sits at old_len */
            {
                size_t n = old_len - off;
                ref_append(ref + off, 0); /* keep model empty until append */
                {
                    char stale[REF_CAP];
                    size_t k;
                    for (k = 0; k < old_len; k++)
                        stale[k] = ref[k];
                    stale[old_len] = '\0';
                    ref_append(stale + off, n);
                }
            }
            rc = s.append(s.c_str() + off);
            if (rc != 0 || !check_actual(&s)) {
                fprintf(stderr, "case %u op %u stale-after-clear off=%lu\n",
                        seed, i, (unsigned long)off);
                return EXIT_CORRUPT;
            }
            break;
        }
        case 8: case 9: { /* reserve: random moderate size */
            want = rnd_range(0, 300u);
            rc = s.reserve(want);
            if (rc != 0) {
                fprintf(stderr, "case %u op %u reserve %lu\n", seed, i,
                        (unsigned long)want);
                return EXIT_CORRUPT;
            }
            /* reserve never changes content or length; capacity >= want */
            if (s.capacity() < want || !check_actual(&s)) {
                fprintf(stderr, "case %u op %u reserve-cap\n", seed, i);
                return EXIT_CORRUPT;
            }
            break;
        }
        case 10: { /* reserve at/over the hard limit: must refuse cleanly */
            want = STDLIB_ALLOC_LIMIT + 1u;
            rc = s.reserve(want);
            if (rc == 0) {
                fprintf(stderr, "case %u op %u reserve-over-limit OK\n",
                        seed, i);
                return EXIT_UNEXPECTED_OK;
            }
            if (!check_actual(&s)) {
                fprintf(stderr, "case %u op %u reserve-over-limit state\n",
                        seed, i);
                return EXIT_CORRUPT;
            }
            break;
        }
        case 11: { /* clear */
            s.clear();
            ref_reset();
            if (!check_actual(&s)) {
                fprintf(stderr, "case %u op %u clear\n", seed, i);
                return EXIT_CORRUPT;
            }
            break;
        }
        case 12: { /* drop + re-init via append */
            s.drop();
            if (s.valid()) {
                fprintf(stderr, "case %u op %u drop-valid\n", seed, i);
                return EXIT_CORRUPT;
            }
            ref_reset();
            rc = s.append("R");
            if (rc != 0 || !check_actual(&s)) {
                fprintf(stderr, "case %u op %u reinit\n", seed, i);
                return EXIT_CORRUPT;
            }
            ref_append("R", 1);
            if (!check_actual(&s)) {
                fprintf(stderr, "case %u op %u reinit-content\n", seed, i);
                return EXIT_CORRUPT;
            }
            break;
        }
        case 13: { /* substr into a canary-guarded output buffer */
            unsigned char buf[CANARY_BYTES + 32 + CANARY_BYTES];
            char *out;
            size_t start = rnd_range(0, ref_len);
            size_t n = rnd_range(0, 40u);
            size_t expect_n = n;
            size_t k;
            for (k = 0; k < sizeof(buf); k++)
                buf[k] = (unsigned char)(0x3Cu ^ (k & 0xFFu));
            out = (char *)(buf + CANARY_BYTES);
            rc = s.substr(start, n, out, 32u);
            if (rc != 0) {
                fprintf(stderr, "case %u op %u substr-rc\n", seed, i);
                return EXIT_CORRUPT;
            }
            if (expect_n > ref_len - start)
                expect_n = ref_len - start;
            if (expect_n > 31u)
                expect_n = 31u;
            for (k = 0; k < expect_n; k++) {
                if (out[k] != ref[start + k]) {
                    fprintf(stderr, "case %u op %u substr-content\n", seed, i);
                    return EXIT_CORRUPT;
                }
            }
            if (out[expect_n] != '\0') {
                fprintf(stderr, "case %u op %u substr-term\n", seed, i);
                return EXIT_CORRUPT;
            }
            for (k = 0; k < CANARY_BYTES; k++) {
                if (buf[k] != (unsigned char)(0x3Cu ^ (k & 0xFFu)) ||
                    buf[CANARY_BYTES + 32u + k] !=
                        (unsigned char)(0x3Cu ^ ((CANARY_BYTES + 32u + k) & 0xFFu))) {
                    fprintf(stderr, "case %u op %u substr-canary\n", seed, i);
                    return EXIT_CORRUPT;
                }
            }
            break;
        }
        case 14: { /* copy_from another (fresh) string */
            size_t n = rnd_range(0, 24u);
            size_t k;
            char tmp[32];
            for (k = 0; k < n; k++)
                tmp[k] = (char)('A' + (k & 31u));
            tmp[n] = '\0';
            other = string(tmp);
            rc = s.copy_from(&other);
            if (rc != 0) {
                fprintf(stderr, "case %u op %u copy-rc\n", seed, i);
                return EXIT_CORRUPT;
            }
            ref_reset();
            ref_append(tmp, n);
            if (!check_actual(&s)) {
                fprintf(stderr, "case %u op %u copy-content\n", seed, i);
                return EXIT_CORRUPT;
            }
            break;
        }
        default: { /* huge append request via a synthetic long literal is not
                    * possible without allocating; instead pass NULL (refusal) */
            rc = s.append((const char *)0);
            if (rc == 0) {
                fprintf(stderr, "case %u op %u append-null OK\n", seed, i);
                return EXIT_UNEXPECTED_OK;
            }
            if (!check_actual(&s)) {
                fprintf(stderr, "case %u op %u append-null state\n", seed, i);
                return EXIT_CORRUPT;
            }
            break;
        }
        }
    }

    /* final view smoke test: reading the view must not corrupt anything */
    {
        string_view v = s.to_view();
        if (v.size() != s.size()) {
            fprintf(stderr, "case %u final view size\n", seed);
            return EXIT_CORRUPT;
        }
        if (!canary_ok()) {
            fprintf(stderr, "case %u final canary\n", seed);
            return EXIT_CORRUPT;
        }
    }
    (void)other;
    return g_bad ? EXIT_CORRUPT : EXIT_OK;
}

int main(int argc, char **argv) {
    long total = 1500;
    long i;
    long ok = 0, corrupt = 0, unexpected_ok = 0, unexpected_refused = 0, crash = 0;

    if (argc > 1) {
        long v = 0;
        for (i = 0; argv[1][i] != '\0'; i++)
            v = v * 10 + (argv[1][i] - '0');
        if (v > 0)
            total = v;
    }

    for (i = 0; i < total; i++) {
        pid_t pid = fork();
        int st = 0;
        if (pid == 0) {
            int rc = child_case((unsigned int)(i * 2654435761u + 0x13579BDFu));
            _exit(rc);
        }
        if (pid < 0) {
            crash++;
            continue;
        }
        waitpid(pid, &st, 0);
        if (WIFSIGNALED(st)) {
            crash++;
        } else if (WIFEXITED(st)) {
            int rc = WEXITSTATUS(st);
            if (rc == EXIT_OK)
                ok++;
            else if (rc == EXIT_CORRUPT)
                corrupt++;
            else if (rc == EXIT_UNEXPECTED_OK)
                unexpected_ok++;
            else if (rc == EXIT_UNEXPECTED_REFUSED)
                unexpected_refused++;
            else
                crash++;
        } else {
            crash++;
        }
    }

    print("FOCUS_STRINGS", "ok", ok, "corrupt", corrupt,
          "unexpected_ok", unexpected_ok,
          "unexpected_refused", unexpected_refused,
          "crash", crash, "total", total);
    return (corrupt || unexpected_ok || unexpected_refused || crash) ? 1 : 0;
}
