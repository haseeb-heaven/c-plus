/* FUZZ10K / json_log -- compiled cplus_json_* symbol coverage.
 *
 * Companion to fuzz10k_json_log.cp. Exercises the real compiled artifacts in
 * build/safe/libcplus_safecrt.a (verified with nm by verify_nm.sh):
 *   cplus_json_parse, cplus_json_get_int, cplus_json_get_str, cplus_json_free,
 *   cplus_safe_log, cplus_safe_log_set_level, cplus_safe_log_get_level.
 *
 * Determinism: fixed SplitMix64 seed 0xC0FFEE0DF02A5EED, 2048 scenarios,
 * forked children, per-child alarm watchdog, wait4 peak-RSS bound, contiguous
 * canaries around the work buffer. Same result protocol as the main harness.
 *
 * Exact build (from repo root):
 *   ./build/c+ build tests/fuzz10k/json_log/compiled_probe.cp \
 *     --strict --sanitize=none -I. -Ithird_party/yyjson -Lbuild/ext -lyyjson \
 *     -o tests/fuzz10k/json_log/results/compiled_probe \
 *     -- build/safe/libcplus_safecrt.a
 * Run:  timeout 600 <bin>
 *
 * NOTE: this is evidence, not proof. It exercises the listed contracts with
 * deterministic inputs; it cannot establish the absence of all defects.
 */
#include <limits.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/safe/safe_log.h"

#if defined(__cplusplus)
extern "C" {
#endif
extern void *cplus_json_parse(const char *text, int *status);
extern int cplus_json_get_int(void *handle, const char *key, int64_t *out);
extern const char *cplus_json_get_str(void *handle, const char *key);
extern void cplus_json_free(void *handle);
/* cplus_safe_log{,_set_level,_get_level} are declared in stdlib/safe/safe_log.h. */
#if defined(__cplusplus)
}
#endif

#define CP_N 2048
#define CP_SEED 0xC0FFEE0DF02A5EEDULL
#define GUARD 32
#define J_CAP ((size_t)128u * 1024u + 16u)
#define WATCHDOG_SEC 5
#define MEM_CAP_BYTES ((unsigned long long)256u * 1024u * 1024u)
#define MEM_RLIMIT ((rlim_t)512u * 1024u * 1024u)

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

#define C_VALID 0
#define C_MALFORMED 1
#define C_NULL 2
#define C_DEEP 3
#define C_LARGE 4
#define C_GET_NULL 5
#define C_FREE 6
#define C_LOG 7
#define C_COUNT 8

static const char *kind_name(int k) {
    static const char *names[C_COUNT] = {
        "parse_valid", "parse_malformed", "parse_null", "parse_deep",
        "parse_large", "get_null", "free", "log"
    };
    if (k < 0 || k >= C_COUNT)
        return "unknown";
    return names[k];
}

struct work_area {
    unsigned char pre[GUARD];
    unsigned char jbuf[J_CAP];
    unsigned char post[GUARD];
};
static struct work_area g_work;

static int g_kind;
static unsigned long long g_r1;
static unsigned long long g_r2;
static unsigned long long g_r3;

static void jput(size_t i, char c) {
    __builtin_memcpy(g_work.jbuf + i, &c, 1);
}
static char jget(size_t i) {
    char c;
    __builtin_memcpy(&c, g_work.jbuf + i, 1);
    return c;
}
static void arm(void) {
    __builtin_memset(g_work.pre, 0xA5, GUARD);
    __builtin_memset(g_work.post, 0x5A, GUARD);
}
static int sentinel_ok(void) {
    unsigned char expect[GUARD];
    if (__builtin_memset(expect, 0xA5, GUARD) == 0)
        return 0;
    if (__builtin_memcmp(g_work.pre, expect, GUARD) != 0)
        return 0;
    __builtin_memset(expect, 0x5A, GUARD);
    if (__builtin_memcmp(g_work.post, expect, GUARD) != 0)
        return 0;
    return 1;
}

static unsigned long long mix64(unsigned long long z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}
static void params_for(int id, unsigned long long *r1, unsigned long long *r2,
                       unsigned long long *r3, int *kind) {
    unsigned long long k = (unsigned long long)id;
    *r1 = mix64(CP_SEED + k * 0x9E3779B97F4A7C15ULL);
    *r2 = mix64(CP_SEED ^ (k * 0xD1B54A32D192ED03ULL));
    *r3 = mix64(mix64(CP_SEED + k * 0x2545F4914F6CDD1DULL + 0x1234567ULL) +
                k * 0xD6E8FEB86659FD93ULL);
    *kind = (int)(*r3 % (unsigned long long)C_COUNT);
}

static size_t build_object(int64_t number) {
    char tmp[128];
    size_t pos = 0;
    size_t i;
    size_t n;
    int len = __builtin_snprintf(tmp, sizeof(tmp),
                                 "{\"n\":%lld,\"name\":\"value\"}",
                                 (long long)number);
    if (len <= 0)
        return 0;
    n = (size_t)len;
    for (i = 0; i < n; i++)
        jput(pos++, tmp[i]);
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

static size_t build_large(size_t n) {
    size_t i;
    if (n >= J_CAP)
        n = J_CAP - 1;
    if (n < 2) {
        jput(0, '[');
        jput(1, '\0');
        return n;
    }
    jput(0, '"');
    for (i = 1; i + 1 < n; i++)
        jput(i, 'a');
    jput(n - 1, '"');
    jput(n, '\0');
    return n;
}

static int run_scenario(void) {
    int observed = RES_FAIL;
    int status = -1;
    void *handle = NULL;
    int64_t number = 0;
    const char *text;
    size_t used;
    int64_t expected;

    switch (g_kind) {
    case C_VALID: {
        expected = (int64_t)(g_r1 % 1000000ULL);
        used = build_object(expected);
        (void)used;
        handle = cplus_json_parse((const char *)g_work.jbuf, &status);
        if (handle != (void *)0 && status == 0) {
            int rc = cplus_json_get_int(handle, "n", &number);
            text = cplus_json_get_str(handle, "name");
            if (rc == 1 && number == expected && text != (const char *)0 &&
                __builtin_strcmp(text, "value") == 0)
                observed = RES_OK;
        }
        cplus_json_free(handle);
        break;
    }
    case C_MALFORMED: {
        expected = (int64_t)(g_r1 % 1000000ULL);
        used = build_object(expected);
        if (used > 0)
            jput(0, 'x');
        handle = cplus_json_parse((const char *)g_work.jbuf, &status);
        if (handle == (void *)0 && status == 2)
            observed = RES_REFUSED;
        cplus_json_free(handle);
        break;
    }
    case C_NULL: {
        status = -1;
        handle = cplus_json_parse((const char *)0, &status);
        if (handle == (void *)0 && status == 1)
            observed = RES_REFUSED;
        cplus_json_free(handle);
        cplus_json_free((void *)0);
        break;
    }
    case C_DEEP: {
        size_t depth = (size_t)(g_r1 % 20001ULL);
        if ((g_r2 & 7ULL) == 0ULL)
            depth = 20000;
        used = build_deep(depth);
        (void)used;
        status = -1;
        handle = cplus_json_parse((const char *)g_work.jbuf, &status);
        if (handle != (void *)0 && status == 0)
            observed = RES_OK;
        cplus_json_free(handle);
        break;
    }
    case C_LARGE: {
        size_t n = (size_t)(g_r1 % 131073ULL);
        if ((g_r2 & 7ULL) == 0ULL)
            n = 131072;
        used = build_large(n);
        status = -1;
        handle = cplus_json_parse((const char *)g_work.jbuf, &status);
        if (used < 2) {
            if (handle == (void *)0 && (status == 2 || status == 1))
                observed = RES_REFUSED;
        } else if (handle != (void *)0 && status == 0) {
            observed = RES_OK;
        }
        cplus_json_free(handle);
        break;
    }
    case C_GET_NULL: {
        int rc = cplus_json_get_int((void *)0, "n", &number);
        text = cplus_json_get_str((void *)0, "name");
        if (rc == 0 && text == (const char *)0)
            observed = RES_REFUSED;
        break;
    }
    case C_FREE: {
        expected = (int64_t)(g_r1 % 1000000ULL);
        used = build_object(expected);
        (void)used;
        status = -1;
        handle = cplus_json_parse((const char *)g_work.jbuf, &status);
        if (handle != (void *)0 && status == 0)
            observed = RES_DEFINED;
        cplus_json_free(handle);
        cplus_json_free((void *)0);
        break;
    }
    case C_LOG: {
        int level = (int)(g_r1 % 6ULL);
        int got;
        cplus_safe_log_set_level((cplus_safe_log_level)level);
        got = (int)cplus_safe_log_get_level();
        cplus_safe_log((cplus_safe_log_level)level, "compiled_probe", "message");
        cplus_safe_log((cplus_safe_log_level)level, (const char *)0,
                       (const char *)0);
        cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
        if (got == level && cplus_safe_log_get_level() == CPLUS_SAFE_LOG_OFF)
            observed = RES_OK;
        break;
    }
    default:
        break;
    }
    return observed;
}

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

int main(void) {
    int id;
    int crashes = 0;
    int hangs = 0;
    int corruptions = 0;
    int refusals = 0;
    int emitted = 0;

    for (id = 0; id < CP_N; id++) {
        pid_t pid;
        int status = 0;
        struct rusage ru;
        int res = RES_FAIL;
        int sent = 0;
        int kind;
        unsigned long long r1, r2, r3;
        unsigned long long rss = 0;

        params_for(id, &r1, &r2, &r3, &kind);
        g_kind = kind;
        g_r1 = r1;
        g_r2 = r2;
        g_r3 = r3;

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

        fprintf(stdout, "C %d %s %s %d %s\n", id, kind_name(kind),
                res_name(res), sent,
                rss <= MEM_CAP_BYTES ? "memok" : "memOOM");
        emitted++;
    }

    fprintf(stdout, "COMPILED json_log %d %d %d %d %d\n", emitted, crashes,
            hangs, corruptions, refusals);
    return (crashes + hangs + corruptions) == 0 ? 0 : 1;
}
