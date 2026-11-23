/* Native C+ replacement for tests/stdlib/run_adversarial.py.
 *
 * Behaviour contract (mirrors the Python runner exactly):
 *   - discovers deep/agent2/regressions/fuzz C+ harnesses and C fuzz harnesses;
 *   - supports --suite, --mode, --only and --fast with the same validation;
 *   - builds each source independently (c+ for .cp, clang for C fuzz), applies
 *     90 s build / 30 s run timeouts, feeds <name>.stdin fixtures, checks
 *     <name>.exit / <name>.expected / <name>.stderr.expected byte-for-byte;
 *   - preserves per-mode build/stdout/stderr logs under tests/stdlib/reports;
 *   - writes adversarial.json + adversarial.txt and exits non-zero on any
 *     classification other than PASS or SKIPPED (BLOCKED is a failure).
 *
 * No failure-suppressing shell no-op is used anywhere in this runner.
 * Command execution goes through the hardened libc cplus_system() wrapper and
 * file I/O goes through stdlib/file.hp, so the runner is a single .cp program
 * compiled by build/c+ and linked against libcplus_hardened.a.
 *
 * Build (from the repository root):
 *   ./build/c+ build tests/stdlib/run_adversarial.cp --strict --sanitize=none \
 *       -Ithird_party/yyjson -Lbuild/ext -lyyjson \
 *       -o /tmp/native_adversarial -- build/libc/libcplus_hardened.a
 * Run:
 *   /tmp/native_adversarial <ROOT> [--suite ...] [--mode ...] \
 *       [--only ...] [--fast]
 */
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

#include "cplus/cplus.h"
#include "stdlib/file.hp"

extern int cplus_system(const char *cmd);

#define BUILD_TIMEOUT 90
#define RUN_TIMEOUT 30
#define GIT_TIMEOUT 10

#define MAX_PATH 1024
#define MAX_CMD 8192
#define MAX_NAME 256
#define MAX_ITEMS 2048
#define MAX_MODES 16
#define CAP_OUT (1 << 20)
#define CAP_ERR (1 << 16)
#define CAP_LINE (1 << 16)

static const char *VALID_SUITES[] = {"all", "deep", "agent2", "regressions", "fuzz"};
static const char *VALID_MODES[] = {"strict-none", "default", "ubsan", "asan", "debug", "release"};
static const char *CLASS_NAMES[] = {"PASS", "FAIL", "BUILD_FAILURE", "TIMEOUT", "SKIPPED", "BLOCKED"};
#define NCLASS 6

struct Item {
    char group[32];
    char stem[MAX_NAME];
    char src_rel[MAX_PATH];
    char src_abs[MAX_PATH];
    char dir_abs[MAX_PATH];
};

struct Record {
    char test[MAX_NAME];
    char group[32];
    char source[MAX_PATH];
    char mode[24];
    int has_build_command;
    char build_command[MAX_CMD];
    int has_build_exit;
    int build_exit;
    char build_stderr[CAP_ERR];
    char classification[24];
    int has_runtime_command;
    char runtime_command[MAX_CMD];
    int has_runtime_exit;
    int runtime_exit;
    int expected_exit;
    int exp_count;
    char expectation_files[2][MAX_PATH];
    char reason[512];
    int timed_out;
    int has_stdin_fixture;
    char stdin_fixture[MAX_PATH];
};

static struct Item g_items[MAX_ITEMS];
static int g_item_count = 0;

static char g_root[MAX_PATH];
static char g_suite[32] = "all";
static char g_modes[MAX_MODES][24];
static int g_mode_count = 0;
static char g_only[MAX_NAME];
static int g_has_only = 0;
static int g_fast = 0;
static char g_cplus[MAX_PATH];

static char g_bout[CAP_OUT];
static char g_berr[CAP_ERR];
static char g_out[CAP_OUT];
static char g_err[CAP_OUT];
static char g_exp[CAP_OUT];
static char g_stdin[CAP_OUT];
static char g_src[CAP_OUT];

static char g_line[CAP_LINE];
static size_t g_line_len;
static char g_esc[CAP_LINE];

static int g_results_written;
static int g_counts[NCLASS];
static int g_class_seen[NCLASS];
static int g_class_order[NCLASS];
static int g_class_order_len;
static int g_total;
static char g_head[64] = "unknown";
static char g_generated[64];

#define LBFMT(...)                                                            \
    do {                                                                      \
        int _n = snprintf(g_line + g_line_len, CAP_LINE - g_line_len,          \
                          __VA_ARGS__);                                       \
        if (_n > 0) {                                                         \
            g_line_len += (size_t)_n;                                         \
            if (g_line_len >= CAP_LINE)                                       \
                g_line_len = CAP_LINE - 1;                                    \
        }                                                                     \
    } while (0)

#define LBRESET()                                                             \
    do {                                                                      \
        g_line_len = 0;                                                       \
        g_line[0] = '\0';                                                     \
    } while (0)

/* ---------------------------------------------------------------- helpers */

static int path_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static int is_regular(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0)
        return 0;
    return S_ISREG(st.st_mode);
}

/* Read a whole file into buf using stdlib/file.hp (binary safe: ftell gives
 * the exact byte count of each fread). Returns byte count, or -1 on failure. */
static long read_all(const char *path, char *buf, size_t cap) {
    error e = error();
    file f = file(path, "rb");
    size_t total = 0;
    if (!f.is_open())
        return -1;
    for (;;) {
        long before, after;
        int st;
        if (total >= cap)
            break;
        before = ftell(f.native());
        if (before < 0)
            return -1;
        st = f.read(buf + total, cap - total, &e);
        after = ftell(f.native());
        if (after < before)
            return -1;
        total += (size_t)(after - before);
        if (st == STDLIB_EOF || after == before)
            break;
        if (st != STDLIB_OK)
            return -1;
    }
    buf[total] = '\0';
    return (long)total;
}

/* Write raw bytes to a file using stdlib/file.hp. Returns 0 or -1. */
static int write_all(const char *path, const char *data, size_t size) {
    error e = error();
    file f = file(path, "wb");
    if (!f.is_open())
        return -1;
    if (size > 0 && f.write_bytes(data, size, &e) != STDLIB_OK)
        return -1;
    return 0;
}

static void truncate_file(const char *path) {
    error e = error();
    file f = file(path, "wb");
    (void)e;
    f.drop();
}

static int is_safe_shell_char(unsigned char c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9'))
        return 1;
    if (c == '_' || c == '@' || c == '%' || c == '+' || c == '=' ||
        c == ':' || c == ',' || c == '.' || c == '/' || c == '-')
        return 1;
    return 0;
}

/* Children must not inherit the report streams: tests such as deep_file_lifecycle
 * report their own fd numbers, and Python's subprocess uses close_fds=True. */
static void set_cloexec(FILE *f) {
    int fd, flags, r;
    if (f == NULL)
        return;
    fd = fileno(f);
    flags = fcntl(fd, F_GETFD);
    if (flags >= 0) {
        r = fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
        (void)r;
    }
}

/* Append one argument to a shell command the way shlex.join() would. */
static void sh_append(char *dst, size_t cap, const char *arg) {
    size_t len = strlen(dst);
    size_t i;
    int safe = arg[0] != '\0';
    for (i = 0; arg[i] != '\0'; i++) {
        if (!is_safe_shell_char((unsigned char)arg[i])) {
            safe = 0;
            break;
        }
    }
    if (len > 0) {
        if (len + 1 >= cap)
            return;
        dst[len++] = ' ';
        dst[len] = '\0';
    }
    if (safe) {
        if (len + strlen(arg) + 1 > cap)
            return;
        memcpy(dst + len, arg, strlen(arg) + 1);
        return;
    }
    if (len + 1 >= cap)
        return;
    dst[len++] = '\'';
    for (i = 0; arg[i] != '\0' && len + 5 < cap; i++) {
        if (arg[i] == '\'') {
            memcpy(dst + len, "'\\''", 4);
            len += 4;
        } else {
            dst[len++] = arg[i];
        }
    }
    dst[len++] = '\'';
    dst[len] = '\0';
}

/* fwrite() result must be consumed to satisfy C+ FFI safety checks. */
static void cwrite(FILE *f, const char *s, size_t n) {
    size_t written = fwrite(s, 1, n, f);
    (void)written;
}

/* JSON string (with surrounding quotes), mirroring json.dumps escaping. */
static const char *jesc(const char *s) {
    size_t o = 0;
    size_t i;
    g_esc[o++] = '"';
    for (i = 0; s[i] != '\0' && o + 12 < CAP_LINE; i++) {
        unsigned char c = (unsigned char)s[i];
        unsigned int cp = c;
        if (c >= 0x80) {
            if ((c & 0xE0) == 0xC0 && (s[i + 1] & 0xC0) == 0x80) {
                cp = ((unsigned int)(c & 0x1F) << 6) |
                     (unsigned int)(s[i + 1] & 0x3F);
                i += 1;
            } else if ((c & 0xF0) == 0xE0 && (s[i + 1] & 0xC0) == 0x80 &&
                       (s[i + 2] & 0xC0) == 0x80) {
                cp = ((unsigned int)(c & 0x0F) << 12) |
                     ((unsigned int)(s[i + 1] & 0x3F) << 6) |
                     (unsigned int)(s[i + 2] & 0x3F);
                i += 2;
            } else if ((c & 0xF8) == 0xF0 && (s[i + 1] & 0xC0) == 0x80 &&
                       (s[i + 2] & 0xC0) == 0x80 && (s[i + 3] & 0xC0) == 0x80) {
                cp = ((unsigned int)(c & 0x07) << 18) |
                     ((unsigned int)(s[i + 1] & 0x3F) << 12) |
                     ((unsigned int)(s[i + 2] & 0x3F) << 6) |
                     (unsigned int)(s[i + 3] & 0x3F);
                i += 3;
            } else {
                cp = 0xFFFD;
            }
        }
        if (c == '"') {
            g_esc[o++] = '\\';
            g_esc[o++] = '"';
        } else if (c == '\\') {
            g_esc[o++] = '\\';
            g_esc[o++] = '\\';
        } else if (c == '\n') {
            g_esc[o++] = '\\';
            g_esc[o++] = 'n';
        } else if (c == '\r') {
            g_esc[o++] = '\\';
            g_esc[o++] = 'r';
        } else if (c == '\t') {
            g_esc[o++] = '\\';
            g_esc[o++] = 't';
        } else if (c == '\b') {
            g_esc[o++] = '\\';
            g_esc[o++] = 'b';
        } else if (c == '\f') {
            g_esc[o++] = '\\';
            g_esc[o++] = 'f';
        } else if (cp < 0x20) {
            o += (size_t)snprintf(g_esc + o, CAP_LINE - o, "\\u%04x", cp);
        } else if (cp > 0xFFFF) {
            unsigned int v = cp - 0x10000;
            o += (size_t)snprintf(g_esc + o, CAP_LINE - o, "\\u%04x\\u%04x",
                                  0xD800 + (v >> 10), 0xDC00 + (v & 0x3FF));
        } else if (c >= 0x80) {
            o += (size_t)snprintf(g_esc + o, CAP_LINE - o, "\\u%04x", cp);
        } else {
            g_esc[o++] = (char)c;
        }
    }
    g_esc[o++] = '"';
    g_esc[o] = '\0';
    return g_esc;
}

/* Run a shell command with a hard timeout via cplus_system(). The shell
 * captures stdout/stderr in files and writes the real exit code to rc_path.
 * 124 is the timeout sentinel, matching subprocess.TimeoutExpired. */
static int exec_cmd(const char *cmd, int secs, const char *stdin_path,
                    const char *out_path, const char *err_path,
                    const char *rc_path, int *rc, int *timed_out) {
    char shell[MAX_CMD * 2];
    char rcbuf[64];
    long rclen;
    int n;
    truncate_file(rc_path);
    n = snprintf(shell, sizeof(shell),
                 "PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin "
                 "CPLUS_CLANG=/usr/bin/clang "
                 "timeout %d %s <\"%s\" >\"%s\" 2>\"%s\"; "
                 "printf '%%s' \"$?\" >\"%s\"",
                 secs, cmd, stdin_path, out_path, err_path, rc_path);
    if (n < 0 || (size_t)n >= sizeof(shell))
        return -1;
    cplus_system(shell);
    rclen = read_all(rc_path, rcbuf, sizeof(rcbuf));
    if (rclen < 0)
        return -1;
    rcbuf[rclen] = '\0';
    *rc = atoi(rcbuf);
    *timed_out = (*rc == 124);
    return 0;
}

static int exec_error(int rc, const char *errbuf) {
    if (rc == 127)
        return 1;
    if (rc == 126)
        return 1;
    if (strstr(errbuf, "command not found") != NULL)
        return 1;
    if (strstr(errbuf, "No such file or directory") != NULL)
        return 1;
    return 0;
}

/* ------------------------------------------------------------- discovery */

static int name_matches(const char *name, const char *group) {
    size_t n = strlen(name);
    int cp = n >= 3 && strcmp(name + n - 3, ".cp") == 0;
    int cc = n >= 2 && strcmp(name + n - 2, ".c") == 0;
    if (name[0] == '.')
        return 0;
    if (strcmp(group, "deep") == 0)
        return cp && strncmp(name, "deep_", 5) == 0;
    if (strcmp(group, "agent2") == 0 || strcmp(group, "regressions") == 0 ||
        strcmp(group, "fuzz") == 0)
        return cp;
    if (strcmp(group, "fuzz-c") == 0)
        return cc && strncmp(name, "f100_", 5) == 0;
    return 0;
}

static void add_item(const char *group, const char *dir_abs,
                     const char *dir_rel, const char *name,
                     const size_t dot_at) {
    struct Item *it;
    if (g_item_count >= MAX_ITEMS)
        return;
    it = &g_items[g_item_count++];
    memset(it, 0, sizeof(*it));
    snprintf(it->group, sizeof(it->group), "%s", group);
    snprintf(it->stem, sizeof(it->stem), "%.*s", (int)dot_at, name);
    snprintf(it->dir_abs, sizeof(it->dir_abs), "%s", dir_abs);
    snprintf(it->src_rel, sizeof(it->src_rel), "%s/%s", dir_rel, name);
    snprintf(it->src_abs, sizeof(it->src_abs), "%s/%s", dir_abs, name);
}

static int item_cmp(const void *a, const void *b) {
    const struct Item *x = (const struct Item *)a;
    const struct Item *y = (const struct Item *)b;
    return strcmp(x->src_rel, y->src_rel);
}

static void discover_one(const char *group, const char *dir_rel) {
    char dir_abs[MAX_PATH];
    DIR *d;
    struct dirent *de;
    snprintf(dir_abs, sizeof(dir_abs), "%s/%s", g_root, dir_rel);
    d = opendir(dir_abs);
    if (d == NULL)
        return;
    while ((de = readdir(d)) != NULL) {
        char full[MAX_PATH];
        size_t n = strlen(de->d_name);
        size_t dot;
        if (!name_matches(de->d_name, group))
            continue;
        snprintf(full, sizeof(full), "%s/%s", dir_abs, de->d_name);
        if (!is_regular(full))
            continue;
        dot = n;
        while (dot > 0 && de->d_name[dot - 1] != '.')
            dot--;
        if (dot == 0)
            dot = n;
        if (g_has_only && strstr(de->d_name, g_only) == NULL) {
            /* Python filters on stem, not filename; check the stem below. */
            char stem[MAX_NAME];
            snprintf(stem, sizeof(stem), "%.*s", (int)(dot - 1), de->d_name);
            if (strstr(stem, g_only) == NULL)
                continue;
        }
        add_item(group, dir_abs, dir_rel, de->d_name, dot - 1);
    }
    closedir(d);
}

static void discover(void) {
    if (strcmp(g_suite, "all") == 0 || strcmp(g_suite, "deep") == 0)
        discover_one("deep", "tests/stdlib");
    if (strcmp(g_suite, "all") == 0 || strcmp(g_suite, "agent2") == 0)
        discover_one("agent2", "tests/stdlib/agent2");
    if (strcmp(g_suite, "all") == 0 || strcmp(g_suite, "regressions") == 0)
        discover_one("regressions", "tests/stdlib/regressions");
    if (strcmp(g_suite, "all") == 0 || strcmp(g_suite, "fuzz") == 0) {
        discover_one("fuzz", "tests/stdlib/fuzz");
        discover_one("fuzz-c", "tests/crt/fuzz");
    }
    if (g_item_count > 1)
        qsort(g_items, (size_t)g_item_count, sizeof(struct Item), item_cmp);
}

/* ---------------------------------------------------------- build command */

static void make_build_command(const struct Item *it, const char *mode,
                               const char *source_text, const char *exe_abs,
                               char *cmd, size_t cap) {
    char buf[MAX_PATH];
    cmd[0] = '\0';
    if (strcmp(it->group, "fuzz-c") == 0) {
        sh_append(cmd, cap, "/usr/bin/clang");
        sh_append(cmd, cap, "-O1");
        sh_append(cmd, cap, "-g");
        sh_append(cmd, cap, "-std=c99");
        sh_append(cmd, cap, "-Wall");
        sh_append(cmd, cap, "-Wextra");
        sh_append(cmd, cap, "-pedantic");
        snprintf(buf, sizeof(buf), "-I%s/libc", g_root);
        sh_append(cmd, cap, buf);
        snprintf(buf, sizeof(buf), "-I%s/libc/include", g_root);
        sh_append(cmd, cap, buf);
        snprintf(buf, sizeof(buf), "-I%s/libc/install-include", g_root);
        sh_append(cmd, cap, buf);
        snprintf(buf, sizeof(buf), "-I%s/stdlib/safe", g_root);
        sh_append(cmd, cap, buf);
        snprintf(buf, sizeof(buf), "-I%s/third_party/safeclib/include", g_root);
        sh_append(cmd, cap, buf);
        snprintf(buf, sizeof(buf), "-I%s/third_party/intel-safestringlib/include", g_root);
        sh_append(cmd, cap, buf);
        sh_append(cmd, cap, it->src_abs);
        snprintf(buf, sizeof(buf), "%s/build/libc/libcplus_hardened.a", g_root);
        sh_append(cmd, cap, buf);
        snprintf(buf, sizeof(buf), "%s/build/safe/libcplus_safecrt.a", g_root);
        sh_append(cmd, cap, buf);
        sh_append(cmd, cap, "-lm");
        sh_append(cmd, cap, "-pthread");
        sh_append(cmd, cap, "-o");
        sh_append(cmd, cap, exe_abs);
        return;
    }
    sh_append(cmd, cap, g_cplus);
    sh_append(cmd, cap, "build");
    sh_append(cmd, cap, it->src_abs);
    if (strcmp(mode, "strict-none") == 0) {
        sh_append(cmd, cap, "--strict");
        sh_append(cmd, cap, "--sanitize=none");
    } else if (strcmp(mode, "default") == 0) {
        sh_append(cmd, cap, "--default");
    } else if (strcmp(mode, "ubsan") == 0) {
        sh_append(cmd, cap, "--strict");
        sh_append(cmd, cap, "--sanitize=undefined");
    } else if (strcmp(mode, "asan") == 0) {
        sh_append(cmd, cap, "--strict");
        sh_append(cmd, cap, "--sanitize=address");
    } else if (strcmp(mode, "debug") == 0) {
        sh_append(cmd, cap, "--debug");
        sh_append(cmd, cap, "--strict");
        sh_append(cmd, cap, "--sanitize=none");
    } else if (strcmp(mode, "release") == 0) {
        sh_append(cmd, cap, "--release");
        sh_append(cmd, cap, "--strict");
        sh_append(cmd, cap, "--sanitize=none");
    }
    if (strstr(source_text, "stdlib/json.hp") != NULL ||
        strstr(source_text, "yyjson.h") != NULL) {
        snprintf(buf, sizeof(buf), "-I%s/third_party/yyjson", g_root);
        sh_append(cmd, cap, buf);
        snprintf(buf, sizeof(buf), "-L%s/build/ext", g_root);
        sh_append(cmd, cap, buf);
        sh_append(cmd, cap, "-lyyjson");
    }
    sh_append(cmd, cap, "-o");
    sh_append(cmd, cap, exe_abs);
}

/* --------------------------------------------------------- record helpers */

static void count_class(const char *cls) {
    int i;
    for (i = 0; i < NCLASS; i++) {
        if (strcmp(CLASS_NAMES[i], cls) == 0) {
            if (!g_class_seen[i]) {
                g_class_seen[i] = 1;
                g_class_order[g_class_order_len++] = i;
            }
            g_counts[i]++;
            return;
        }
    }
}

static void write_record_json(FILE *out, const struct Record *r) {
    LBRESET();
    LBFMT("    {\n");
    LBFMT("      \"test\": %s,\n", jesc(r->test));
    LBFMT("      \"group\": %s,\n", jesc(r->group));
    LBFMT("      \"source\": %s,\n", jesc(r->source));
    LBFMT("      \"mode\": %s,\n", jesc(r->mode));
    LBFMT("      \"build_command\": %s,\n",
          r->has_build_command ? jesc(r->build_command) : "null");
    LBFMT("      \"build_exit\": ");
    if (r->has_build_exit)
        LBFMT("%d", r->build_exit);
    else
        LBFMT("null");
    LBFMT(",\n");
    LBFMT("      \"build_stderr\": %s,\n", jesc(r->build_stderr));
    LBFMT("      \"classification\": %s,\n", jesc(r->classification));
    LBFMT("      \"runtime_command\": %s,\n",
          r->has_runtime_command ? jesc(r->runtime_command) : "null");
    LBFMT("      \"runtime_exit\": ");
    if (r->has_runtime_exit)
        LBFMT("%d", r->runtime_exit);
    else
        LBFMT("null");
    LBFMT(",\n");
    LBFMT("      \"expected_exit\": %d,\n", r->expected_exit);
    if (r->exp_count == 0) {
        LBFMT("      \"expectation_files\": [],\n");
    } else {
        int i;
        LBFMT("      \"expectation_files\": [\n");
        for (i = 0; i < r->exp_count; i++) {
            LBFMT("        %s%s\n", jesc(r->expectation_files[i]),
                  i + 1 < r->exp_count ? "," : "");
        }
        LBFMT("      ],\n");
    }
    LBFMT("      \"reason\": %s,\n", jesc(r->reason));
    LBFMT("      \"timed_out\": %s,\n", r->timed_out ? "true" : "false");
    LBFMT("      \"stdin_fixture\": %s\n",
          r->has_stdin_fixture ? jesc(r->stdin_fixture) : "null");
    LBFMT("    }");
    cwrite(out, g_line, g_line_len);
}

static void write_record_txt(FILE *out, const struct Record *r) {
    char line[512];
    snprintf(line, sizeof(line), "%-42.42s %-12.12s %-11.11s %.*s\n",
             r->test, r->mode, r->classification, 70, r->reason);
    cwrite(out, line, strlen(line));
}

static void finalise_record(FILE *json_results, FILE *txt, struct Record *r) {
    if (g_results_written++)
        cwrite(json_results, ",\n", 2);
    write_record_json(json_results, r);
    write_record_txt(txt, r);
    count_class(r->classification);
    g_total++;
}

/* Emit the current record and move to the next source/mode. Kept as a macro
 * so C+ never warns about goto bypassing scoped cleanup. */
#define FINALIZE_AND_CONTINUE()                                               \
    do {                                                                      \
        finalise_record(resf, txtf, &rec);                                    \
        continue;                                                             \
    } while (0)

/* ------------------------------------------------------------------ setup */

static void set_timestamp(void) {
    struct timeval tv;
    struct tm tmval;
    int n;
    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &tmval);
    n = snprintf(g_generated, sizeof(g_generated),
                 "%04d-%02d-%02dT%02d:%02d:%02d.%06ld",
                 tmval.tm_year + 1900, tmval.tm_mon + 1, tmval.tm_mday,
                 tmval.tm_hour, tmval.tm_min, tmval.tm_sec,
                 (long)tv.tv_usec);
    if (n <= 0)
        snprintf(g_generated, sizeof(g_generated), "unknown");
}

static void get_head(const char *reports_dir) {
    char outp[MAX_PATH], errp[MAX_PATH], rcp[MAX_PATH];
    char line[MAX_CMD];
    char shell[MAX_CMD];
    int rc = 0, to = 0;
    long n;
    snprintf(outp, sizeof(outp), "%s/.git_head.out", reports_dir);
    snprintf(errp, sizeof(errp), "%s/.git_head.err", reports_dir);
    snprintf(rcp, sizeof(rcp), "%s/.git_head.rc", reports_dir);
    snprintf(shell, sizeof(shell),
             "cd \"%s\" && PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin "
             "timeout %d git rev-parse --short HEAD >\"%s\" 2>\"%s\"; "
             "printf '%%s' \"$?\" >\"%s\"",
             g_root, GIT_TIMEOUT, outp, errp, rcp);
    cplus_system(shell);
    n = read_all(outp, line, sizeof(line));
    if (n >= 0 && !to) {
        long e = n;
        while (e > 0 && (line[e - 1] == '\n' || line[e - 1] == '\r' ||
                         line[e - 1] == ' ' || line[e - 1] == '\t'))
            e--;
        line[e] = '\0';
        if (e > 0)
            snprintf(g_head, sizeof(g_head), "%s", line);
    }
    (void)rc;
}

static void ensure_dir(const char *path) {
    char shell[MAX_PATH + 32];
    snprintf(shell, sizeof(shell), "mkdir -p \"%s\"", path);
    cplus_system(shell);
}

static int mode_valid(const char *m) {
    size_t i;
    for (i = 0; i < sizeof(VALID_MODES) / sizeof(VALID_MODES[0]); i++) {
        if (strcmp(m, VALID_MODES[i]) == 0)
            return 1;
    }
    return 0;
}

static int suite_valid(const char *s) {
    size_t i;
    for (i = 0; i < sizeof(VALID_SUITES) / sizeof(VALID_SUITES[0]); i++) {
        if (strcmp(s, VALID_SUITES[i]) == 0)
            return 1;
    }
    return 0;
}

/* Python-style repr of the MODES list for error messages. */
static void mode_repr(char *out, size_t cap) {
    int i;
    size_t o = 0;
    o += (size_t)snprintf(out + o, cap - o, "[");
    for (i = 0; i < g_mode_count; i++) {
        o += (size_t)snprintf(out + o, cap - o, "%s'%s'",
                              i ? ", " : "", g_modes[i]);
    }
    snprintf(out + o, cap - o, "]");
}

static void parse_args(int argc, char **argv) {
    char resolved[MAX_PATH];
    int i = 2;
    if (realpath(argv[1], resolved) != NULL)
        snprintf(g_root, sizeof(g_root), "%s", resolved);
    else
        snprintf(g_root, sizeof(g_root), "%s", argv[1]);
    snprintf(g_cplus, sizeof(g_cplus), "%s/build/c+", g_root);
    snprintf(g_modes[0], sizeof(g_modes[0]), "strict-none");
    snprintf(g_modes[1], sizeof(g_modes[0]), "default");
    g_mode_count = 2;
    while (i < argc) {
        const char *a = argv[i];
        if (strcmp(a, "--suite") == 0 && i + 1 < argc) {
            snprintf(g_suite, sizeof(g_suite), "%s", argv[i + 1]);
            i += 2;
        } else if (strcmp(a, "--mode") == 0 && i + 1 < argc) {
            const char *p = argv[i + 1];
            g_mode_count = 0;
            while (*p != '\0' && g_mode_count < MAX_MODES) {
                const char *start;
                size_t len;
                while (*p == ' ' || *p == '\t')
                    p++;
                start = p;
                while (*p != '\0' && *p != ',')
                    p++;
                len = (size_t)(p - start);
                while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t'))
                    len--;
                if (len >= sizeof(g_modes[0])) {
                    fprintf(stderr, "error: unknown mode in [%s]\n", argv[i + 1]);
                    exit(2);
                }
                memcpy(g_modes[g_mode_count], start, len);
                g_modes[g_mode_count][len] = '\0';
                g_mode_count++;
                if (*p == ',')
                    p++;
            }
            if (g_mode_count == 0 && argv[i + 1][0] == '\0') {
                g_modes[0][0] = '\0';
                g_mode_count = 1;
            }
            i += 2;
        } else if (strcmp(a, "--only") == 0 && i + 1 < argc) {
            snprintf(g_only, sizeof(g_only), "%s", argv[i + 1]);
            g_has_only = 1;
            i += 2;
        } else if (strcmp(a, "--fast") == 0) {
            g_fast = 1;
            i += 1;
        } else {
            fprintf(stderr, "unknown option: %s\n", a);
            exit(2);
        }
    }
    if (!suite_valid(g_suite)) {
        fprintf(stderr, "error: unknown suite '%s' (expected one of agent2,all,deep,fuzz,regressions)\n",
                g_suite);
        exit(2);
    }
    for (i = 0; i < g_mode_count; i++) {
        if (!mode_valid(g_modes[i])) {
            char repr[256];
            mode_repr(repr, sizeof(repr));
            fprintf(stderr, "error: unknown mode in %s (expected one of asan,debug,default,release,strict-none,ubsan)\n",
                    repr);
            exit(2);
        }
    }
}

/* --------------------------------------------------------------- main run */

int main(int argc, char **argv) {
    char reports[MAX_PATH], logs[MAX_PATH], tmp_results[MAX_PATH];
    char empty_stdin[MAX_PATH];
    char json_path[MAX_PATH], txt_path[MAX_PATH];
    FILE *jsonf, *txtf, *resf;
    error e = error();
    int mi, ii, k;
    (void)e;
    if (argc < 2) {
        fprintf(stderr, "usage: run_adversarial <ROOT> [--suite ...] [--mode ...] [--only ...] [--fast]\n");
        return 2;
    }
    parse_args(argc, argv);
    discover();
    if (g_item_count == 0) {
        char only_repr[MAX_NAME + 2];
        if (g_has_only)
            snprintf(only_repr, sizeof(only_repr), "'%s'", g_only);
        else
            snprintf(only_repr, sizeof(only_repr), "None");
        fprintf(stderr,
                "error: no tests discovered (suite=%s only=%s); refusing to report a vacuous pass\n",
                g_suite, only_repr);
        return 2;
    }
    snprintf(reports, sizeof(reports), "%s/tests/stdlib/reports", g_root);
    snprintf(logs, sizeof(logs), "%s/logs", reports);
    ensure_dir(reports);
    ensure_dir(logs);
    snprintf(tmp_results, sizeof(tmp_results), "%s/.adversarial.results.tmp", reports);
    snprintf(json_path, sizeof(json_path), "%s/adversarial.json", reports);
    snprintf(txt_path, sizeof(txt_path), "%s/adversarial.txt", reports);
    snprintf(empty_stdin, sizeof(empty_stdin), "%s/.empty.stdin", logs);
    write_all(empty_stdin, "", 0);
    set_timestamp();
    get_head(reports);

    resf = fopen(tmp_results, "w");
    txtf = fopen(txt_path, "w");
    set_cloexec(resf);
    set_cloexec(txtf);
    if (resf == NULL || txtf == NULL) {
        if (resf != NULL) { FILE *c1 = resf; fclose(c1); }
        if (txtf != NULL) { FILE *c2 = txtf; fclose(c2); }
        fprintf(stderr, "error: cannot open report files\n");
        return 2;
    }
    fprintf(txtf, "Adversarial stdlib summary (%s) suite=%s\n", g_head, g_suite);
    fprintf(txtf, "modes=");
    for (mi = 0; mi < g_mode_count; mi++) {
        fprintf(txtf, "%s%s", mi ? "," : "", g_modes[mi]);
    }
    fprintf(txtf, " compiler=%s\n", g_cplus);

    for (mi = 0; mi < g_mode_count; mi++) {
        const char *mode = g_modes[mi];
        char work[MAX_PATH];
        int k;
        snprintf(work, sizeof(work), "%s/%s", logs, mode);
        ensure_dir(work);
        for (ii = 0; ii < g_item_count; ii++) {
            const struct Item *it = &g_items[ii];
            char exe[MAX_PATH], blog[MAX_PATH], olog[MAX_PATH], elog[MAX_PATH];
            char boutp[MAX_PATH], berrp[MAX_PATH], rcp[MAX_PATH];
            char stdinp[MAX_PATH];
            char rel[MAX_PATH];
            struct Record rec;
            const char *reason_skip = "libFuzzer entry point requires a fuzzing engine; use tests/stdlib/fuzz/run_fuzz.sh";
            int rc = 0, to = 0;
            long n, nerr;
            memset(&rec, 0, sizeof(rec));
            snprintf(rec.test, sizeof(rec.test), "%s", it->stem);
            snprintf(rec.group, sizeof(rec.group), "%s", it->group);
            snprintf(rec.source, sizeof(rec.source), "%s", it->src_rel);
            snprintf(rec.mode, sizeof(rec.mode), "%s", mode);
            rec.expected_exit = 0;
            snprintf(exe, sizeof(exe), "%s/%s", work, it->stem);
            snprintf(blog, sizeof(blog), "%s/%s.build.log", work, it->stem);
            snprintf(olog, sizeof(olog), "%s/%s.stdout", work, it->stem);
            snprintf(elog, sizeof(elog), "%s/%s.stderr", work, it->stem);
            snprintf(boutp, sizeof(boutp), "%s/%s.build.stdout.tmp", work, it->stem);
            snprintf(berrp, sizeof(berrp), "%s/%s.build.stderr.tmp", work, it->stem);
            snprintf(rcp, sizeof(rcp), "%s/%s.rc.tmp", work, it->stem);
            snprintf(stdinp, sizeof(stdinp), "%s", empty_stdin);

            /* Always clear stale artifacts before describing this run. */
            truncate_file(blog);
            truncate_file(olog);
            truncate_file(elog);
            remove(exe);
            n = read_all(it->src_abs, g_src, CAP_OUT - 1);
            if (n < 0)
                g_src[0] = '\0';
            g_src[n < 0 ? 0 : n] = '\0';

            /* libFuzzer skip rule. */
            if (strcmp(it->group, "fuzz") == 0 &&
                strstr(g_src, "LLVMFuzzerTestOneInput") != NULL &&
                strstr(g_src, "int main") == NULL) {
                char skipmsg[MAX_CMD];
                snprintf(skipmsg, sizeof(skipmsg), "SKIPPED: %s\n", reason_skip);
                write_all(blog, skipmsg, strlen(skipmsg));
                snprintf(rec.build_command, sizeof(rec.build_command),
                         "libFuzzer target (no main)");
                rec.has_build_command = 1;
                snprintf(rec.classification, sizeof(rec.classification), "SKIPPED");
                snprintf(rec.reason, sizeof(rec.reason), "%s", reason_skip);
                finalise_record(resf, txtf, &rec);
                continue;
            }

            /* Build. */
            make_build_command(it, mode, g_src, exe, rec.build_command,
                               sizeof(rec.build_command));
            rec.has_build_command = 1;
            if (exec_cmd(rec.build_command, BUILD_TIMEOUT, empty_stdin,
                         boutp, berrp, rcp, &rc, &to) != 0) {
                snprintf(rec.classification, sizeof(rec.classification), "BUILD_FAILURE");
                snprintf(rec.reason, sizeof(rec.reason), "exec error: runner wrapper failed");
                FINALIZE_AND_CONTINUE();
            }
            n = read_all(boutp, g_bout, CAP_OUT - 1);
            if (n < 0)
                n = 0;
            g_bout[n] = '\0';
            nerr = read_all(berrp, g_berr, CAP_ERR - 1);
            if (nerr < 0)
                nerr = 0;
            g_berr[nerr] = '\0';
            {
                char composed[CAP_OUT];
                size_t cl = 0;
                if ((size_t)n + 15 + (size_t)nerr + 1 < sizeof(composed)) {
                    memcpy(composed, g_bout, (size_t)n);
                    cl = (size_t)n;
                    memcpy(composed + cl, "\n--- stderr ---\n", 15);
                    cl += 15;
                    memcpy(composed + cl, g_berr, (size_t)nerr);
                    cl += (size_t)nerr;
                    composed[cl] = '\0';
                    write_all(blog, composed, cl);
                }
            }
            remove(boutp);
            remove(berrp);
            rec.has_build_exit = 1;
            rec.build_exit = rc;
            snprintf(rec.build_stderr, sizeof(rec.build_stderr), "%s", g_berr);
            rec.timed_out = to;
            if (to) {
                snprintf(rec.classification, sizeof(rec.classification), "BUILD_FAILURE");
                snprintf(rec.reason, sizeof(rec.reason), "build exceeded %ds", BUILD_TIMEOUT);
                FINALIZE_AND_CONTINUE();
            }
            if (exec_error(rc, g_berr)) {
                snprintf(rec.classification, sizeof(rec.classification), "BUILD_FAILURE");
                snprintf(rec.reason, sizeof(rec.reason),
                         "exec error: command not found (rc %d)", rc);
                FINALIZE_AND_CONTINUE();
            }
            if (rc != 0) {
                if (strstr(g_berr, "not supported by the selected backend") != NULL) {
                    snprintf(rec.classification, sizeof(rec.classification), "BLOCKED");
                    snprintf(rec.reason, sizeof(rec.reason), "sanitizer unsupported by backend");
                } else {
                    snprintf(rec.classification, sizeof(rec.classification), "BUILD_FAILURE");
                    snprintf(rec.reason, sizeof(rec.reason), "compiler exit %d", rc);
                }
                FINALIZE_AND_CONTINUE();
            }

            /* Stdin fixture: source directory first, then tests/stdlib. */
            snprintf(stdinp, sizeof(stdinp), "%s/%s.stdin", it->dir_abs, it->stem);
            if (!path_exists(stdinp)) {
                snprintf(stdinp, sizeof(stdinp), "%s/tests/stdlib/%s.stdin",
                         g_root, it->stem);
                if (!path_exists(stdinp))
                    snprintf(stdinp, sizeof(stdinp), "%s", empty_stdin);
            }
            if (strcmp(stdinp, empty_stdin) != 0) {
                snprintf(rel, sizeof(rel), "%s.stdin", it->stem);
                if (strstr(stdinp, "/tests/stdlib/") != NULL) {
                    snprintf(rec.stdin_fixture, sizeof(rec.stdin_fixture),
                             "tests/stdlib/%s.stdin", it->stem);
                } else {
                    const char *slash = strrchr(it->src_rel, '/');
                    if (slash != NULL) {
                        size_t dlen = (size_t)(slash - it->src_rel);
                        snprintf(rec.stdin_fixture, sizeof(rec.stdin_fixture),
                                 "%.*s/%s.stdin", (int)dlen, it->src_rel, it->stem);
                    }
                }
                rec.has_stdin_fixture = 1;
            }

            /* Runtime. */
            rec.runtime_command[0] = '\0';
            sh_append(rec.runtime_command, sizeof(rec.runtime_command), exe);
            rec.has_runtime_command = 1;
            if (exec_cmd(rec.runtime_command, RUN_TIMEOUT, stdinp, olog, elog,
                         rcp, &rc, &to) != 0) {
                snprintf(rec.classification, sizeof(rec.classification), "FAIL");
                snprintf(rec.reason, sizeof(rec.reason), "exec error: runner wrapper failed");
                FINALIZE_AND_CONTINUE();
            }
            rec.has_runtime_exit = 1;
            rec.runtime_exit = rc;
            rec.timed_out = to;

            /* Expected exit code. */
            snprintf(rel, sizeof(rel), "%s/%s.exit", it->dir_abs, it->stem);
            if (!path_exists(rel)) {
                snprintf(rel, sizeof(rel), "%s/tests/stdlib/%s.exit",
                         g_root, it->stem);
            }
            if (path_exists(rel)) {
                char xbuf[64];
                long xlen = read_all(rel, xbuf, sizeof(xbuf) - 1);
                if (xlen >= 0) {
                    xbuf[xlen] = '\0';
                    rec.expected_exit = atoi(xbuf);
                }
            }

            /* Byte-exact stdout / stderr expectations. */
            n = read_all(olog, g_out, CAP_OUT - 1);
            if (n < 0)
                n = 0;
            g_out[n] = '\0';
            nerr = read_all(elog, g_err, CAP_OUT - 1);
            if (nerr < 0)
                nerr = 0;
            g_err[nerr] = '\0';
            {
                const char *suffix[2] = {".expected", ".stderr.expected"};
                const char *label[2] = {"stdout", "stderr"};
                char mismatches[512];
                size_t mlen = 0;
                int k2;
                mismatches[0] = '\0';
                for (k2 = 0; k2 < 2; k2++) {
                    char exp_path[MAX_PATH];
                    char exp_rel[MAX_PATH];
                    const char *actual;
                    long actual_len;
                    long exp_len;
                    snprintf(exp_path, sizeof(exp_path), "%s/%s%s",
                             it->dir_abs, it->stem, suffix[k2]);
                    if (!path_exists(exp_path)) {
                        snprintf(exp_path, sizeof(exp_path),
                                 "%s/tests/stdlib/%s%s", g_root, it->stem,
                                 suffix[k2]);
                        if (!path_exists(exp_path))
                            continue;
                        snprintf(exp_rel, sizeof(exp_rel), "tests/stdlib/%s%s",
                                 it->stem, suffix[k2]);
                    } else {
                        const char *slash = strrchr(it->src_rel, '/');
                        if (slash != NULL) {
                            size_t dlen = (size_t)(slash - it->src_rel);
                            snprintf(exp_rel, sizeof(exp_rel), "%.*s/%s%s",
                                     (int)dlen, it->src_rel, it->stem, suffix[k2]);
                        } else {
                            snprintf(exp_rel, sizeof(exp_rel), "%s%s",
                                     it->stem, suffix[k2]);
                        }
                    }
                    if (rec.exp_count < 2)
                        snprintf(rec.expectation_files[rec.exp_count++],
                                 sizeof(rec.expectation_files[0]), "%s", exp_rel);
                    exp_len = read_all(exp_path, g_exp, CAP_OUT - 1);
                    if (exp_len < 0)
                        exp_len = 0;
                    g_exp[exp_len] = '\0';
                    if (k2 == 0) {
                        actual = g_out;
                        actual_len = n;
                    } else {
                        actual = g_err;
                        actual_len = nerr;
                    }
                    if (exp_len != actual_len ||
                        memcmp(g_exp, actual, (size_t)actual_len) != 0) {
                        int a = snprintf(mismatches + mlen,
                                         sizeof(mismatches) - mlen, "%s%s differs from %s",
                                         mlen ? "; " : "", label[k2], exp_rel);
                        if (a > 0 && mlen + (size_t)a < sizeof(mismatches))
                            mlen += (size_t)a;
                    }
                }
                if (to) {
                    snprintf(rec.classification, sizeof(rec.classification), "TIMEOUT");
                    snprintf(rec.reason, sizeof(rec.reason), "run exceeded %ds", RUN_TIMEOUT);
                } else if (exec_error(rc, g_err)) {
                    snprintf(rec.classification, sizeof(rec.classification), "FAIL");
                    snprintf(rec.reason, sizeof(rec.reason),
                             "exec error: command not found (rc %d)", rc);
                } else if (rc >= 128) {
                    snprintf(rec.classification, sizeof(rec.classification), "TIMEOUT");
                    snprintf(rec.reason, sizeof(rec.reason), "run rc %d", rc);
                } else if (rc != rec.expected_exit) {
                    snprintf(rec.classification, sizeof(rec.classification), "FAIL");
                    snprintf(rec.reason, sizeof(rec.reason), "exit %d != expected %d",
                             rc, rec.expected_exit);
                } else if (mlen > 0) {
                    snprintf(rec.classification, sizeof(rec.classification), "FAIL");
                    snprintf(rec.reason, sizeof(rec.reason), "%s", mismatches);
                } else {
                    snprintf(rec.classification, sizeof(rec.classification), "PASS");
                }
            }
            finalise_record(resf, txtf, &rec);
        }
    }
    fclose(resf);

    /* Assemble JSON: header, counts (first-appearance order), total, results. */
    jsonf = fopen(json_path, "w");
    if (jsonf == NULL) {
        fprintf(stderr, "error: cannot open %s\n", json_path);
        return 2;
    }
    LBRESET();
    LBFMT("{\n");
    LBFMT("  \"schema_version\": 1,\n");
    LBFMT("  \"generated_at\": %s,\n", jesc(g_generated));
    LBFMT("  \"head\": %s,\n", jesc(g_head));
    LBFMT("  \"suite\": %s,\n", jesc(g_suite));
    if (g_mode_count == 1) {
        LBFMT("  \"modes\": [\n    %s\n  ],\n", jesc(g_modes[0]));
    } else {
        LBFMT("  \"modes\": [\n");
        for (mi = 0; mi < g_mode_count; mi++) {
            LBFMT("    %s%s\n", jesc(g_modes[mi]),
                  mi + 1 < g_mode_count ? "," : "");
        }
        LBFMT("  ],\n");
    }
    LBFMT("  \"fast\": %s,\n", g_fast ? "true" : "false");
    LBFMT("  \"counts\": {\n");
    for (k = 0; k < g_class_order_len; k++) {
        int idx = g_class_order[k];
        LBFMT("    %s: %d%s\n", jesc(CLASS_NAMES[idx]), g_counts[idx],
              k + 1 < g_class_order_len ? "," : "");
    }
    LBFMT("  },\n");
    LBFMT("  \"total\": %d,\n", g_total);
    if (g_total == 0) {
        LBFMT("  \"results\": []\n");
    } else {
        LBFMT("  \"results\": [\n");
        cwrite(jsonf, g_line, g_line_len);
        FILE *resread = fopen(tmp_results, "r");
        if (resread != NULL) {
            char chunk[4096];
            size_t got;
            while ((got = fread(chunk, 1, sizeof(chunk), resread)) > 0)
                cwrite(jsonf, chunk, got);
            fclose(resread);
        }
        LBRESET();
        LBFMT("\n  ]\n");
        cwrite(jsonf, g_line, g_line_len);
    }
    LBRESET();
    LBFMT("}\n");
    cwrite(jsonf, g_line, g_line_len);
    fclose(jsonf);
    remove(tmp_results);

    /* Terminal summary line. */
    fprintf(txtf, "SUMMARY");
    {
        static const int order[6] = {5, 2, 1, 0, 4, 3}; /* alphabetical */
        int first = 1;
        for (k = 0; k < 6; k++) {
            int idx = order[k];
            if (g_counts[idx] > 0) {
                fprintf(txtf, " %s=%d", CLASS_NAMES[idx], g_counts[idx]);
                first = 0;
            }
        }
        (void)first;
    }
    fprintf(txtf, " TOTAL=%d\n", g_total);
    fflush(txtf);

    /* Echo the text report to stdout exactly like the Python runner. */
    {
        char text[CAP_LINE];
        long tlen = read_all(txt_path, text, sizeof(text) - 1);
        if (tlen >= 0) {
            text[tlen] = '\0';
            cwrite(stdout, text, (size_t)tlen);
        }
    }

    { FILE *c3 = txtf; fclose(c3); }
    {
        int bad = g_counts[1] + g_counts[2] + g_counts[3] + g_counts[5];
        return bad ? 1 : 0;
    }
}
