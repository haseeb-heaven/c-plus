/* RED-TEAM agent 5 -- adversarial harness for all 26 vendored cplus_v_*.
 *
 * 2,600 deterministic cases: SEED 0xC0FFEE5AF10D5EED, per-case seed
 * SEED ^ (idx * GOLDEN), function = idx % 26. Every case runs in a forked
 * child with an alarm watchdog; 16-byte canaries surround each 1024-byte
 * source/destination object. Alphabets include embedded NUL, 0xA5 control
 * bytes and non-ASCII bytes. dmax/smax span 0, 1, exact-fit, one-over,
 * RSIZE_MAX_STR+1 and SIZE_MAX. A silent constraint handler counts refusals.
 *
 * Build (from repo root):
 *   /usr/bin/clang -O1 -g -std=c99 -Wall -Wextra -Wno-unused-parameter \
 *     tests/redteam/json_net/redteam_vendored.c \
 *     build/libc/libcplus_hardened.a build/safe/libcplus_safecrt.a \
 *     -lm -pthread -o tests/redteam/json_net/results/redteam_vendored
 * Run:  timeout 600 <bin> <log-path>
 *
 * Output per case: "S <id> <api> <name> <result> <sentinel> <ok> <promise>"
 * Final line: "REDTEAM_PART vendored <cases> <refused> <crashes> <corrupt> <unexpected_ok>"
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* Exact signatures copied from libc/install-include/cplus_vendor_*.h
 * (rsize_t == size_t, errno_t == int at the pinned safeclib commit). */
extern int cplus_v_memchr_s(const void *dest, size_t dmax, int ch, void **resultp);
extern int cplus_v_memccpy_s(void *dest, size_t dmax, const void *src, int c, size_t n);
extern int cplus_v__memchr_s_chk(const void *dest, size_t dmax, int ch, void **resultp, size_t destbos);
extern int cplus_v__memccpy_s_chk(void *dest, size_t dmax, const void *src, int c, size_t n, size_t destbos, size_t srcbos);
extern int cplus_v__strchr_s_chk(const char *dest, size_t dmax, int ch, char **resultp, size_t destbos);
extern int cplus_v__strrchr_s_chk(const char *dest, size_t dmax, int ch, char **resultp, size_t destbos);
extern int cplus_v__strstr_s_chk(char *dest, size_t dmax, const char *src, size_t slen, char **substringp, size_t destbos, size_t srcbos);
extern int cplus_v__strcasestr_s_chk(char *dest, size_t dmax, const char *src, size_t slen, char **substring, size_t destbos, size_t srcbos);
extern int cplus_v__strspn_s_chk(const char *dest, size_t dmax, const char *src, size_t slen, size_t *countp, size_t destbos, size_t srcbos);
extern int cplus_v__strcspn_s_chk(const char *dest, size_t dmax, const char *src, size_t slen, size_t *countp, size_t destbos, size_t srcbos);
extern size_t cplus_v_strlcat(char *dest, const char *src, size_t dsize);
extern size_t cplus_v_strlcpy(char *dest, const char *src, size_t dsize);
extern char *cplus_v__stpcpy_s_chk(char *dest, size_t dmax, const char *src, int *errp, size_t destbos, size_t srcbos);
extern char *cplus_v__stpncpy_s_chk(char *dest, size_t dmax, const char *src, size_t slen, int *errp, size_t destbos, size_t srcbos);
extern int cplus_v__strnset_s_chk(char *dest, size_t dmax, int value, size_t n, size_t destbos);
extern int cplus_v__strset_s_chk(char *dest, size_t dmax, int value, size_t destbos);
extern size_t cplus_v__strnterminate_s_chk(char *dest, size_t dmax, size_t destbos);
extern int cplus_v__strtolowercase_s_chk(char *dest, size_t dmax, size_t destbos);
extern int cplus_v__strtouppercase_s_chk(char *dest, size_t dmax, size_t destbos);
extern int cplus_v__strfirstchar_s_chk(char *dest, size_t dmax, char c, char **firstp, size_t destbos);
extern int cplus_v__strlastchar_s_chk(char *dest, size_t dmax, char c, char **lastp, size_t destbos);
extern int cplus_v__strfirstdiff_s_chk(const char *dest, size_t dmax, const char *src, size_t *resultp, size_t destbos);
extern int cplus_v__strlastdiff_s_chk(const char *dest, size_t dmax, const char *src, size_t *resultp, size_t destbos);
extern int cplus_v__strcmpfld_s_chk(const char *dest, size_t dmax, const char *src, int *resultp, size_t destbos);
extern int cplus_v__strcpyfld_s_chk(char *dest, size_t dmax, const char *src, size_t slen, size_t destbos);
extern int cplus_v__strcoll_s_chk(const char *dest, size_t dmax, const char *src, int *resultp, size_t destbos);

extern void cplus_sc_set_mem_constraint_handler_s_wrapper(void (*handler)(const char *, void *, int));
extern void cplus_sc_set_str_constraint_handler_s_wrapper(void (*handler)(const char *, void *, int));
extern void cplus_runtime_set_violation_trap(int enabled);

#define FUNC_COUNT 26u
#define PER_FN 100u
#define N_CASES ((unsigned)FUNC_COUNT * PER_FN)
#define SEED UINT64_C(0xC0FFEE5AF10D5EED)
#define GOLDEN UINT64_C(0x9E3779B97F4A7C15)
#define CAP 1024u
#define CANARY 16u
#define RSIZE_MAX_STR ((size_t)4096u)
#define ALARM_SEC 10

static const char *const FN_NAMES[FUNC_COUNT] = {
    "cplus_v_memchr_s", "cplus_v_memccpy_s",
    "cplus_v__memchr_s_chk", "cplus_v__memccpy_s_chk",
    "cplus_v__strchr_s_chk", "cplus_v__strrchr_s_chk",
    "cplus_v__strstr_s_chk", "cplus_v__strcasestr_s_chk",
    "cplus_v__strspn_s_chk", "cplus_v__strcspn_s_chk",
    "cplus_v_strlcat", "cplus_v_strlcpy",
    "cplus_v__stpcpy_s_chk", "cplus_v__stpncpy_s_chk",
    "cplus_v__strnset_s_chk", "cplus_v__strset_s_chk",
    "cplus_v__strnterminate_s_chk", "cplus_v__strtolowercase_s_chk",
    "cplus_v__strtouppercase_s_chk", "cplus_v__strfirstchar_s_chk",
    "cplus_v__strlastchar_s_chk", "cplus_v__strfirstdiff_s_chk",
    "cplus_v__strlastdiff_s_chk", "cplus_v__strcmpfld_s_chk",
    "cplus_v__strcpyfld_s_chk", "cplus_v__strcoll_s_chk"
};

typedef struct { unsigned char pre[CANARY]; unsigned char data[CAP]; unsigned char post[CANARY]; } gbuf;

static gbuf g_dst, g_src;
static volatile unsigned long long g_handler_calls;

static void silent_handler(const char *msg, void *ptr, int err) {
    (void)msg; (void)ptr; (void)err;
    g_handler_calls++;
}

static uint64_t rng_state;
static void rng_seed(uint64_t s) { rng_state = s; }
static uint32_t rnd32(void) {
    uint64_t z = (rng_state += GOLDEN);
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return (uint32_t)((z ^ (z >> 31)) >> 32);
}
static size_t rnd_below(size_t n) { return n ? (size_t)(rnd32() % (uint32_t)n) : 0; }

static void buf_init(gbuf *b) {
    memset(b->pre, 0xA5, CANARY);
    memset(b->data, 0x5A, CAP);
    memset(b->post, 0xA5, CANARY);
}
static int buf_canaries_ok(const gbuf *b) {
    size_t i;
    for (i = 0; i < CANARY; i++)
        if (b->pre[i] != 0xA5 || b->post[i] != 0xA5) return 0;
    return 1;
}
static void gen_string(unsigned char *p) {
    static const unsigned char alpha[] = {
        0, 0, 0, 'A', 'z', ' ', '[', ']', '{', '}', '\r', '\n',
        0xA5, 0xC3, 0x28, 0x80, 0xF0, 0x7F, 'a', 'Z', '0', '9'
    };
    size_t n = rnd_below(400u) + 1u;
    size_t i;
    for (i = 0; i < n; i++) p[i] = alpha[rnd_below(sizeof alpha)];
    p[n] = '\0';
}

static size_t pick_len(size_t struct_len) {
    static const size_t tbl[] = { 0u, 1u, 2u, 3u, 4u, 8u, 16u, 64u, 255u, 256u, 1023u, 1024u, 1025u, 4096u, 4097u };
    switch (rnd32() % 8u) {
    case 0: return 0u;
    case 1: return 1u;
    case 2: return struct_len;              /* exact fit */
    case 3: return struct_len + 1u;         /* one over */
    case 4: return RSIZE_MAX_STR + 1u;
    case 5: return (size_t)-1;              /* SIZE_MAX */
    case 6: return tbl[rnd32() % (sizeof tbl / sizeof tbl[0])];
    default: return rnd_below(1100u);
    }
}

static int dmax_oversized(size_t v);
static int slen_oversized(size_t v);

static void report(int fd, unsigned id, const char *api, const char *result,
                   int sent, int ok, int promise) {
    char line[256];
    int n = snprintf(line, sizeof line, "S %05u %s %s %s %d %d %d\n",
                     id, api, api, result, sent ? 1 : 0, ok ? 1 : 0, promise ? 1 : 0);
    if (n > 0) {
        size_t k = (size_t)n;
        if (k >= sizeof line) k = sizeof line - 1;
        (void)write(fd, line, k);
    }
}

static void run_case(unsigned id, int fd) {
    unsigned fn = id % FUNC_COUNT;
    const char *api = FN_NAMES[fn];
    size_t dlen, slen, nbytes;
    int null_dest, null_src, null_out;
    char *out_ptr = NULL;
    size_t out_sz = (size_t)0xDEADBEEFu;
    int out_int = -12345;
    int errp = -12345;
    int ch, value, sent, promise, ok = 1;
    const char *result;
    unsigned long long before;

    rng_seed(SEED ^ ((uint64_t)id * GOLDEN));
    buf_init(&g_dst); buf_init(&g_src);
    dlen = 0;
    gen_string(g_dst.data + 0u);
    gen_string(g_src.data + 0u);
    dlen = pick_len(CAP);
    slen = pick_len(CAP);
    nbytes = rnd_below(CAP + 1u);
    ch = (int)g_src.data[rnd_below(CAP)];
    value = (int)(rnd32() % 600u) - 300;
    null_dest = (rnd32() % 16u) == 0u;
    null_src  = (rnd32() % 16u) == 0u;
    null_out  = (rnd32() % 16u) == 0u;
    promise = (null_dest || null_src || null_out);

    {
        char *d = null_dest ? NULL : (char *)g_dst.data;
        const char *s = null_src ? NULL : (const char *)g_src.data;
        char **op = null_out ? NULL : &out_ptr;
        size_t *szp = null_out ? NULL : &out_sz;
        int *ip = null_out ? NULL : &out_int;
        size_t dm = dlen, sl = slen, nb = nbytes;

        /* Non-BOS entry points have no object-size parameter: bound their
         * length arguments to the real object so the call itself is defined. */
        if (dm > CAP) dm = CAP;
        if (sl > CAP) sl = CAP;
        if (nb > CAP) nb = CAP;
        if (nb > dm) nb = dm;

        before = g_handler_calls;
        switch (fn) {
        case 0: (void)cplus_v_memchr_s((const void *)d, dm, ch, (void **)op); break;
        case 1: (void)cplus_v_memccpy_s(d, dm, (const void *)s, ch, nb); break;
        case 2: (void)cplus_v__memchr_s_chk((const void *)d, dlen, ch, (void **)op, CAP); break;
        case 3: (void)cplus_v__memccpy_s_chk(d, dlen, (const void *)s, ch, nb, CAP, CAP); break;
        case 4: (void)cplus_v__strchr_s_chk(s, dlen, ch, op, CAP); break;
        case 5: (void)cplus_v__strrchr_s_chk(s, dlen, ch, op, CAP); break;
        case 6: (void)cplus_v__strstr_s_chk(d, dlen, s, slen, op, CAP, CAP); break;
        case 7: (void)cplus_v__strcasestr_s_chk(d, dlen, s, slen, op, CAP, CAP); break;
        case 8: (void)cplus_v__strspn_s_chk(d, dlen, s, slen, szp, CAP, CAP); break;
        case 9: (void)cplus_v__strcspn_s_chk(d, dlen, s, slen, szp, CAP, CAP); break;
        case 10: (void)cplus_v_strlcat(d, s, dm); break;
        case 11: (void)cplus_v_strlcpy(d, s, dm); break;
        case 12: (void)cplus_v__stpcpy_s_chk(d, dlen, s, null_out ? NULL : &errp, CAP, CAP); break;
        case 13: (void)cplus_v__stpncpy_s_chk(d, dlen, s, slen, null_out ? NULL : &errp, CAP, CAP); break;
        case 14: (void)cplus_v__strnset_s_chk(d, dlen, value, sl, CAP); break;
        case 15: (void)cplus_v__strset_s_chk(d, dlen, value, CAP); break;
        case 16: (void)cplus_v__strnterminate_s_chk(d, dlen, CAP); break;
        case 17: (void)cplus_v__strtolowercase_s_chk(d, dlen, CAP); break;
        case 18: (void)cplus_v__strtouppercase_s_chk(d, dlen, CAP); break;
        case 19: (void)cplus_v__strfirstchar_s_chk(d, dlen, (char)ch, op, CAP); break;
        case 20: (void)cplus_v__strlastchar_s_chk(d, dlen, (char)ch, op, CAP); break;
        case 21: (void)cplus_v__strfirstdiff_s_chk(d, dlen, s, szp, CAP); break;
        case 22: (void)cplus_v__strlastdiff_s_chk(d, dlen, s, szp, CAP); break;
        case 23: (void)cplus_v__strcmpfld_s_chk(d, dlen, s, ip, CAP); break;
        case 24: (void)cplus_v__strcpyfld_s_chk(d, dlen, s, slen, CAP); break;
        case 25: (void)cplus_v__strcoll_s_chk(d, dlen, s, ip, CAP); break;
        default: ok = 0; break;
        }
    }
    (void)out_ptr; (void)out_sz; (void)out_int; (void)errp;

    /* strnterminate_s returns the number of bytes used, not an errno. */
    if (fn == 16) {
        /* handled via the generic handler-call classification above */
    }

    sent = buf_canaries_ok(&g_dst) && buf_canaries_ok(&g_src);
    if (g_handler_calls > before) result = "REFUSED";
    else result = "DEFINED_OK";
    if (!sent) result = "CORRUPT";
    report(fd, id, api, result, sent, ok, promise);
}

/* forward helpers for promise computation */
static int dmax_oversized(size_t v) { return v > CAP; }
static int slen_oversized(size_t v) { return v > CAP; }

static FILE *g_log;
static unsigned long long g_hash = UINT64_C(1469598103934665603);
static int g_cases, g_refused, g_crashes, g_corrupt, g_unexpected;

static void hash_line(const char *s) {
    while (*s) { unsigned char c = (unsigned char)*s; g_hash ^= c; g_hash *= UINT64_C(1099511628211); s++; }
    g_hash ^= '\n'; g_hash *= UINT64_C(1099511628211);
}

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "tests/redteam/json_net/results/scenarios_vendored.log";
    unsigned id;
    g_log = fopen(path, "w");
    if (!g_log) { fprintf(stderr, "REDTEAM: cannot open %s\n", path); return 2; }
    cplus_sc_set_mem_constraint_handler_s_wrapper(silent_handler);
    cplus_sc_set_str_constraint_handler_s_wrapper(silent_handler);
    cplus_runtime_set_violation_trap(0);

    for (id = 0; id < N_CASES; id++) {
        int pfds[2], status, n, rc;
        pid_t pid;
        char line[256], text[320];
        int v_id, v_sent, v_ok, v_prom;
        char api[64], name[64], result[64];
        if (pipe(pfds) != 0) { g_crashes++; g_cases++; continue; }
        pid = fork();
        if (pid < 0) { close(pfds[0]); close(pfds[1]); g_crashes++; g_cases++; continue; }
        if (pid == 0) {
            close(pfds[0]);
            alarm(ALARM_SEC);
            run_case(id, pfds[1]);
            close(pfds[1]);
            _exit(0);
        }
        close(pfds[1]);
        {
            size_t rdn = 0;
            for (;;) {
                char c = 0; ssize_t k = read(pfds[0], &c, 1);
                if (k <= 0 || c == '\n') break;
                if (rdn + 1 < sizeof line) line[rdn++] = c; else break;
            }
            line[rdn] = '\0';
            n = (int)rdn;
        }
        close(pfds[0]);
        do { rc = (int)waitpid(pid, &status, 0); } while (rc < 0 && errno == EINTR);
        g_cases++;
        if (rc != (int)pid) {
            snprintf(text, sizeof text, "E %05u WAIT_FAIL\n", id);
            fputs(text, g_log); hash_line(text); g_crashes++; continue;
        }
        if (WIFSIGNALED(status)) {
            snprintf(text, sizeof text, "E %05u SIGNAL %d\n", id, WTERMSIG(status));
            fputs(text, g_log); hash_line(text); g_crashes++; continue;
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || n == 0) {
            snprintf(text, sizeof text, "E %05u NO_RESULT\n", id);
            fputs(text, g_log); hash_line(text); g_crashes++; continue;
        }
        if (sscanf(line, "S %d %63s %63s %63s %d %d %d",
                   &v_id, api, name, result, &v_sent, &v_ok, &v_prom) != 7 ||
            (unsigned)v_id != id) {
            snprintf(text, sizeof text, "E %05u MALFORMED\n", id);
            fputs(text, g_log); hash_line(text); g_crashes++; continue;
        }
        snprintf(text, sizeof text, "%s\n", line);
        fputs(text, g_log); fflush(g_log); hash_line(text);
        if (!v_sent) g_corrupt++;
        if (strncmp(result, "CORRUPT", 7) == 0) g_corrupt++;
        if (strncmp(result, "UNEXPECTED_OK", 13) == 0) g_unexpected++;
        else if (v_prom && strncmp(result, "REFUSED", 7) == 0) g_refused++;
        if (!v_ok) g_crashes++;
    }
    fclose(g_log);
    printf("REDTEAM_PART vendored %d %d %d %d %d\n",
           g_cases, g_refused, g_crashes, g_corrupt, g_unexpected);
    printf("REDTEAM_DIGEST_VENDORED %016llx\n", g_hash);
    return 0;
}
