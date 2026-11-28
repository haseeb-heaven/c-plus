/* Native C+ aggregate regression runner for Agent 2.
 *
 * Behavioural replacement for tests/stdlib/agent2/run_all.py.  Discovery,
 * strict/no-sanitize builds, stdin/expectation fixtures, timeout handling,
 * classification, JSON summary, and terminal table are implemented in C+
 * using cplus_system() (declared by tests/native/cplus_test_native.hp) and
 * stdlib/file.hp for every log file.  No Python is involved at runtime.
 *
 * run_all_native.sh resolves the repository root and passes the same nine
 * positional arguments the Python implementation received from run_all.sh.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "tests/native/cplus_test_native.hp"

#define MAX_TESTS 128
#define PATH_CAP 900
#define CMD_CAP 4096
#define OUT_CAP 16384
#define LOG_CAP 16384
#define REASON_CAP 1024
#define DIFF_CAP 2048
#define SRC_CAP 131072
#define NAME_CAP 128
#define MODULE_CAP 64

#define CL_PASS 0
#define CL_FAIL 1
#define CL_BUILD_FAILURE 2
#define CL_TIMEOUT 3
#define CL_SKIPPED 4
#define CL_BLOCKED 5

struct result_t {
    char name[NAME_CAP];
    char group[16];
    char module[MODULE_CAP];
    char source[PATH_CAP];          /* relative to repo root */
    char source_abs[PATH_CAP];
    char parent[PATH_CAP];          /* absolute directory of source */
    char build_command[CMD_CAP];
    int  build_exit;
    char build_stdout[LOG_CAP];
    char build_stderr[LOG_CAP];
    int  has_expectation;
    char expectation[PATH_CAP];     /* relative to repo root */
    int  expected_exit;
    int  has_runtime;
    char runtime_command[CMD_CAP];
    int  runtime_exit;
    char stdout[OUT_CAP];
    char stderr[OUT_CAP];
    int  classification;
    char reason[REASON_CAP];
};

static const char *class_name(int c) {
    switch (c) {
        case CL_PASS: return "PASS";
        case CL_FAIL: return "FAIL";
        case CL_BUILD_FAILURE: return "BUILD_FAILURE";
        case CL_TIMEOUT: return "TIMEOUT";
        case CL_SKIPPED: return "SKIPPED";
        case CL_BLOCKED: return "BLOCKED";
        default: return "UNKNOWN";
    }
}

/* system() returns a wait(2)-style status; convert to the exit code Python's
 * subprocess.run().returncode would have produced. */
static int norm_rc(int sysrc) {
    if (sysrc < 0) return -1;
    if ((sysrc & 0x7f) != 0) return 128 + (sysrc & 0x7f);
    return (sysrc >> 8) & 0xff;
}

static int file_exists(const char *path) {
    error e = error();
    file f = file(path, "r");
    return f.is_open() ? 1 : 0;
}

/* Exact byte read (embedded NULs survive), safely truncated to cap-1 bytes. */
static long read_exact(const char *path, char *buf, size_t cap) {
    error e = error();
    file f = file(path, "r");
    FILE *fp;
    long sz;
    size_t got;
    if (!f.is_open()) return -1;
    fp = f.native();
    if (fseek(fp, 0, SEEK_END) != 0) return -1;
    sz = ftell(fp);
    if (sz < 0) return -1;
    if (fseek(fp, 0, SEEK_SET) != 0) return -1;
    if ((size_t)sz >= cap) sz = (long)(cap - 1);
    memset(buf, 0, cap);
    got = fread(buf, 1, (size_t)sz, fp);
    return (long)got;
}

static int write_file(const char *path, const char *data, size_t len) {
    error e = error();
    file f = file(path, "w");
    if (!f.is_open()) return -1;
    if (len > 0 && f.write_bytes(data, len, &e) != STDLIB_OK) return -1;
    return 0;
}

static int append_file(const char *path, const char *data, size_t len) {
    error e = error();
    file f = file(path, "a");
    if (!f.is_open()) return -1;
    if (len > 0 && f.write_bytes(data, len, &e) != STDLIB_OK) return -1;
    return 0;
}

static void sb_add(char *buf, size_t cap, const char *s) {
    size_t len = strlen(buf);
    size_t n = strlen(s);
    if (len >= cap - 1) return;
    if (len + n >= cap) n = cap - 1 - len;
    memcpy(buf + len, s, n);
    len += n;
    buf[len] = '\0';
}

static void sb_addq(char *buf, size_t cap, const char *s) {
    sb_add(buf, cap, "\"");
    sb_add(buf, cap, s);
    sb_add(buf, cap, "\"");
}

static void relative_path(const char *abs, const char *root,
                          char *out, size_t cap) {
    size_t rlen = strlen(root);
    size_t alen = strlen(abs);
    if (alen > rlen && strncmp(abs, root, rlen) == 0 && abs[rlen] == '/') {
        const char *rel = abs + rlen + 1;
        size_t n = strlen(rel);
        if (n >= cap) n = cap - 1;
        memcpy(out, rel, n);
        out[n] = '\0';
    } else {
        size_t n = alen;
        if (n >= cap) n = cap - 1;
        memcpy(out, abs, n);
        out[n] = '\0';
    }
}

static void split_parent(const char *abs, char *out, size_t cap) {
    size_t n = strlen(abs);
    while (n > 0 && abs[n - 1] != '/') --n;
    if (n > 0) --n;
    if (n >= cap) n = cap - 1;
    memcpy(out, abs, n);
    {
        char nul = '\0';
        __builtin_memcpy(out + n, &nul, sizeof(nul));
    }
}

static void stem_of(const char *abs, char *out, size_t cap) {
    const char *slash = strrchr(abs, '/');
    const char *base = slash != NULL ? slash + 1 : abs;
    size_t n = strlen(base);
    if (n > 4 && strcmp(base + n - 3, ".cp") == 0) n -= 3;
    if (n >= cap) n = cap - 1;
    memcpy(out, base, n);
    out[n] = '\0';
}

/* Mirrors module_of() from run_all.py (substring order matters). */
static void module_of(const char *group, const char *name,
                      char *out, size_t cap) {
    static const char *mods[] = {
        "collections", "error", "file", "fs", "io", "json", "log",
        "net", "string", "thread", "vector", "cross"
    };
    size_t i;
    if (strcmp(group, "deep") != 0) {
        snprintf(out, cap, "%s", group);
        return;
    }
    for (i = 0; i < sizeof(mods) / sizeof(mods[0]); ++i) {
        if (strstr(name, mods[i]) != NULL) {
            snprintf(out, cap, "%s", mods[i]);
            return;
        }
    }
    snprintf(out, cap, "other");
}

/* Execute through cplus_system() with optional timeout, stdin file, and
 * stdout/stderr redirection.  Returns the normalised exit code. */
static int run_capture(const char *cmd, int timeout, const char *stdin_file,
                       const char *out_file, const char *err_file) {
    char exec_cmd[CMD_CAP + 1024];
    char tbuf[32];
    exec_cmd[0] = '\0';
    if (timeout > 0) {
        snprintf(tbuf, sizeof(tbuf), "timeout %ds ", timeout);
        sb_add(exec_cmd, sizeof(exec_cmd), tbuf);
    }
    sb_add(exec_cmd, sizeof(exec_cmd),
           "env PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin ");
    sb_add(exec_cmd, sizeof(exec_cmd), "CPLUS_CLANG=/usr/bin/clang ");
    sb_add(exec_cmd, sizeof(exec_cmd), cmd);
    if (stdin_file != NULL && stdin_file[0] != '\0') {
        sb_add(exec_cmd, sizeof(exec_cmd), " <");
        sb_addq(exec_cmd, sizeof(exec_cmd), stdin_file);
    } else {
        sb_add(exec_cmd, sizeof(exec_cmd), " </dev/null");
    }
    sb_add(exec_cmd, sizeof(exec_cmd), " >");
    sb_addq(exec_cmd, sizeof(exec_cmd), out_file);
    sb_add(exec_cmd, sizeof(exec_cmd), " 2>");
    sb_addq(exec_cmd, sizeof(exec_cmd), err_file);
    return norm_rc(cplus_system(exec_cmd));
}

/* Write one JSON string with json.dumps()-compatible escaping. */
static void json_string(file *f, const char *s) {
    char chunk[4096];
    size_t o = 0;
    f->write("\"");
    while (*s != '\0') {
        unsigned char c = (unsigned char)*s++;
        if (o > sizeof(chunk) - 16) {
            chunk[o] = '\0';
            f->write(chunk);
            o = 0;
        }
        if (c == '"' || c == '\\') {
            chunk[o++] = '\\';
            chunk[o++] = (char)c;
        } else if (c == '\n') {
            chunk[o++] = '\\';
            chunk[o++] = 'n';
        } else if (c == '\r') {
            chunk[o++] = '\\';
            chunk[o++] = 'r';
        } else if (c == '\t') {
            chunk[o++] = '\\';
            chunk[o++] = 't';
        } else if (c < 0x20) {
            o += (size_t)snprintf(chunk + o, sizeof(chunk) - o,
                                  "\\u%04x", (unsigned)c);
        } else {
            chunk[o++] = (char)c;
        }
    }
    if (o > 0) {
        chunk[o] = '\0';
        f->write(chunk);
    }
    f->write("\"");
}

static void json_int(file *f, const char *key, int value, int comma) {
    char line[128];
    snprintf(line, sizeof(line), "      \"%s\": %d%s\n", key, value,
             comma ? "," : "");
    f->write(line);
}

static void json_str_field(file *f, const char *key, const char *value,
                           int comma) {
    char line[128];
    snprintf(line, sizeof(line), "      \"%s\": ", key);
    f->write(line);
    json_string(f, value);
    f->write(comma ? ",\n" : "\n");
}

static void json_nullable(file *f, const char *key, const char *value,
                          int present, int comma) {
    char line[128];
    snprintf(line, sizeof(line), "      \"%s\": ", key);
    f->write(line);
    if (present) json_string(f, value);
    else f->write("null");
    f->write(comma ? ",\n" : "\n");
}

static int classify(int brc, int rrc, int out_match, int err_match,
                    int exit_match) {
    if (brc == 124 || brc >= 128) return CL_TIMEOUT;
    if (brc != 0) return CL_BUILD_FAILURE;
    if (rrc == 124 || rrc >= 128) return CL_TIMEOUT;
    if (rrc != 0 || !out_match || !err_match || !exit_match) return CL_FAIL;
    return CL_PASS;
}

static void discover_manifest(const char *cmd, const char *manifest,
                              const char *group, struct result_t *records,
                              int *count, const char *root,
                              const char *select_test,
                              const char *select_module) {
    char cmdline[CMD_CAP];
    char text[SRC_CAP];
    char *line;
    char st0 = '\0';
    char sm0 = '\0';
    long n;
    __builtin_memcpy(&st0, select_test, sizeof(st0));
    __builtin_memcpy(&sm0, select_module, sizeof(sm0));
    cmdline[0] = '\0';
    sb_add(cmdline, sizeof(cmdline), cmd);
    sb_add(cmdline, sizeof(cmdline), " >");
    sb_addq(cmdline, sizeof(cmdline), manifest);
    cplus_system(cmdline);

    n = read_exact(manifest, text, sizeof(text));
    if (n <= 0) return;
    line = text;
    while (*line != '\0' && *count < MAX_TESTS) {
        char path[PATH_CAP];
        char *end = strchr(line, '\n');
        size_t len;
        if (end != NULL) *end = '\0';
        len = strlen(line);
        if (len > 2) {
            /* Self-exclusion: run_all.cp is this runner, not a regression
             * case.  Without the guard the native runner would compile and
             * execute itself recursively (the Python runner predates it). */
            int is_self = strstr(line, "/agent2/run_all.cp") != NULL;
            snprintf(path, sizeof(path), "%s", line);
            if (!is_self && file_exists(path)) {
                char name[NAME_CAP];
                char stem[NAME_CAP];
                char module[MODULE_CAP];
                stem_of(path, stem, sizeof(stem));
                snprintf(name, sizeof(name), "%s", stem);
                module_of(group, stem, module, sizeof(module));
                if ((st0 == '\0' || strcmp(select_test, name) == 0) &&
                    (sm0 == '\0' || strcmp(select_module, module) == 0)) {
                    struct result_t *r;
                    /* Obtain the element address by byte offset rather than a
                     * pointer subscript: the pointer parameter has no provable
                     * extent under --strict.  (Copying a pointer value through
                     * __builtin_memcpy is not usable here: the strict build
                     * produces a null pointer, so arithmetic is required.) */
                    r = (struct result_t *)((char *)records +
                                            (size_t)(*count) * sizeof(struct result_t));
                    memset(r, 0, sizeof(*r));
                    snprintf(r->name, sizeof(r->name), "%s", name);
                    snprintf(r->group, sizeof(r->group), "%s", group);
                    snprintf(r->module, sizeof(r->module), "%s", module);
                    snprintf(r->source_abs, sizeof(r->source_abs), "%s", path);
                    split_parent(path, r->parent, sizeof(r->parent));
                    relative_path(path, root, r->source, sizeof(r->source));
                    ++(*count);
                }
            }
        }
        if (end == NULL) break;
        line = end + 1;
    }
}

int main(int argc, char **argv) {
    const char *root;
    const char *results;
    const char *cplus;
    int build_timeout;
    int run_timeout;
    const char *select_test;
    const char *select_module;
    const char *arg_build;
    const char *arg_run;
    const char *arg_clean;
    int clean;

    char std_dir[PATH_CAP];
    char ag_dir[PATH_CAP];
    char run_dir[PATH_CAP];
    char manifest_deep[PATH_CAP];
    char manifest_agent[PATH_CAP];
    char cmd[CMD_CAP];
    char text[SRC_CAP];
    char head[64];
    char timestamp[64];
    char linebuf[CMD_CAP];

    struct result_t *records;
    int count = 0;
    int i;
    int counts[6] = {0, 0, 0, 0, 0, 0};
    int bad;
    struct timeval tv;
    struct tm tm_now;

    if (argc < 10) {
        fprintf(stderr,
                "usage: run_all_native ROOT RESULTS CPLUS BUILD_TIMEOUT "
                "RUN_TIMEOUT TEST MODULE CLEAN KEEP_LOGS\n");
        return 2;
    }
    __builtin_memcpy(&root, argv + 1, sizeof(root));
    __builtin_memcpy(&results, argv + 2, sizeof(results));
    __builtin_memcpy(&cplus, argv + 3, sizeof(cplus));
    __builtin_memcpy(&arg_build, argv + 4, sizeof(arg_build));
    __builtin_memcpy(&arg_run, argv + 5, sizeof(arg_run));
    __builtin_memcpy(&select_test, argv + 6, sizeof(select_test));
    __builtin_memcpy(&select_module, argv + 7, sizeof(select_module));
    __builtin_memcpy(&arg_clean, argv + 8, sizeof(arg_clean));
    build_timeout = atoi(arg_build);
    run_timeout = atoi(arg_run);
    clean = strcmp(arg_clean, "1") == 0;
    /* argv[9] KEEP_LOGS is accepted for CLI parity; logs are always kept. */
    if (build_timeout <= 0) build_timeout = 90;
    if (run_timeout <= 0) run_timeout = 30;

    if (!file_exists(cplus)) {
        fprintf(stderr, "BLOCKED: compiler missing at %s\n", cplus);
        return 1;
    }

    snprintf(std_dir, sizeof(std_dir), "%s/tests/stdlib", root);
    snprintf(ag_dir, sizeof(ag_dir), "%s/tests/stdlib/agent2", root);
    snprintf(run_dir, sizeof(run_dir), "%s/run", results);
    snprintf(manifest_deep, sizeof(manifest_deep), "%s/.native_deep.manifest", results);
    snprintf(manifest_agent, sizeof(manifest_agent), "%s/.native_agent.manifest", results);

    cmd[0] = '\0';
    sb_add(cmd, sizeof(cmd), "mkdir -p ");
    sb_addq(cmd, sizeof(cmd), run_dir);
    cplus_system(cmd);

    if (clean) {
        cmd[0] = '\0';
        sb_add(cmd, sizeof(cmd), "find ");
        sb_addq(cmd, sizeof(cmd), results);
        sb_add(cmd, sizeof(cmd), " -maxdepth 1 -type f -delete; find ");
        sb_addq(cmd, sizeof(cmd), run_dir);
        sb_add(cmd, sizeof(cmd), " -maxdepth 1 -type f -delete");
        cplus_system(cmd);
    }

    records = cplus_new_array(struct result_t, MAX_TESTS);

    cmd[0] = '\0';
    sb_add(cmd, sizeof(cmd), "/bin/ls -1 ");
    sb_addq(cmd, sizeof(cmd), std_dir);
    sb_add(cmd, sizeof(cmd), "/deep_*.cp 2>/dev/null | LC_ALL=C /usr/bin/sort");
    discover_manifest(cmd, manifest_deep, "deep", records, &count, root,
                      select_test, select_module);

    cmd[0] = '\0';
    sb_add(cmd, sizeof(cmd), "/bin/ls -1 ");
    sb_addq(cmd, sizeof(cmd), ag_dir);
    sb_add(cmd, sizeof(cmd), "/*.cp 2>/dev/null | LC_ALL=C /usr/bin/sort");
    discover_manifest(cmd, manifest_agent, "agent2", records, &count, root,
                      select_test, select_module);

    /* Curated existing_tests.txt preserves file order (Python does not sort). */
    {
        char mf[PATH_CAP];
        snprintf(mf, sizeof(mf), "%s/existing_tests.txt", ag_dir);
        if (read_exact(mf, text, sizeof(text)) > 0) {
            char *line = text;
            while (*line != '\0' && count < MAX_TESTS) {
                char *end = strchr(line, '\n');
                char name[NAME_CAP];
                if (end != NULL) *end = '\0';
                {
                    char *p = line;
                    char *q;
                    while (*p == ' ' || *p == '\t' || *p == '\r') ++p;
                    q = p + strlen(p);
                    while (q > p && (q[-1] == ' ' || q[-1] == '\t' || q[-1] == '\r'))
                        --q;
                    {
                        size_t n = (size_t)(q - p);
                        if (n >= sizeof(name)) n = sizeof(name) - 1;
                        memcpy(name, p, n);
                        name[n] = '\0';
                    }
                }
                if (name[0] != '\0' && name[0] != '#') {
                    char path[PATH_CAP];
                    snprintf(path, sizeof(path), "%s/%s.cp", std_dir, name);
                    if (file_exists(path)) {
                        char module[MODULE_CAP];
                        module_of("existing", name, module, sizeof(module));
                        if ((select_test[0] == '\0' || strcmp(select_test, name) == 0) &&
                            (select_module[0] == '\0' || strcmp(select_module, module) == 0)) {
                            struct result_t *r = &records[count];
                            memset(r, 0, sizeof(*r));
                            snprintf(r->name, sizeof(r->name), "%s", name);
                            snprintf(r->group, sizeof(r->group), "existing");
                            snprintf(r->module, sizeof(r->module), "%s", module);
                            snprintf(r->source_abs, sizeof(r->source_abs), "%s", path);
                            split_parent(path, r->parent, sizeof(r->parent));
                            relative_path(path, root, r->source, sizeof(r->source));
                            ++count;
                        }
                    }
                }
                if (end == NULL) break;
                line = end + 1;
            }
        }
    }

    for (i = 0; i < count; ++i) {
        struct result_t *r = &records[i];
        char exe[PATH_CAP];
        char build_log[PATH_CAP];
        char stdout_log[PATH_CAP];
        char stderr_log[PATH_CAP];
        char diff_log[PATH_CAP];
        char cmd_log[PATH_CAP];
        char build_out[PATH_CAP];
        char build_err[PATH_CAP];
        char stdin_file[PATH_CAP];
        char exp_path[PATH_CAP];
        char exp_err_path[PATH_CAP];
        char exp_err_rel[PATH_CAP];
        char exp_exit_path[PATH_CAP];
        char src_text[SRC_CAP];
        char stdin_data[OUT_CAP];
        char exp_data[OUT_CAP];
        char exp_err_data[OUT_CAP];
        char out_data[OUT_CAP];
        char err_data[OUT_CAP];
        char extra[CMD_CAP];
        char diffbuf[REASON_CAP + DIFF_CAP];
        long src_len, out_len, err_len, exp_len, exp_err_len, stdin_len;
        int brc, rrc;
        int exp_err_found = 0;
        int exp_exit = 0;
        int stdout_diff = 0;
        int stderr_diff = 0;
        int diff_count = 0;
        int exit_match;

        snprintf(exe, sizeof(exe), "%s/%s", run_dir, r->name);
        snprintf(build_log, sizeof(build_log), "%s/%s.build.log", results, r->name);
        snprintf(stdout_log, sizeof(stdout_log), "%s/%s.stdout", results, r->name);
        snprintf(stderr_log, sizeof(stderr_log), "%s/%s.stderr", results, r->name);
        snprintf(diff_log, sizeof(diff_log), "%s/%s.diff", results, r->name);
        snprintf(cmd_log, sizeof(cmd_log), "%s/%s.commands", results, r->name);
        snprintf(build_out, sizeof(build_out), "%s/.native_build_out", results);
        snprintf(build_err, sizeof(build_err), "%s/.native_build_err", results);

        remove(exe);
        remove(build_log);
        remove(stdout_log);
        remove(stderr_log);
        remove(diff_log);

        src_len = read_exact(r->source_abs, src_text, sizeof(src_text));
        extra[0] = '\0';
        if (src_len > 0 &&
            (strstr(src_text, "stdlib/json.hp") != NULL ||
             strstr(src_text, "yyjson.h") != NULL)) {
            sb_add(extra, sizeof(extra), " -I");
            sb_add(extra, sizeof(extra), root);
            sb_add(extra, sizeof(extra), "/third_party/yyjson -L");
            sb_add(extra, sizeof(extra), root);
            sb_add(extra, sizeof(extra), "/build/ext -lyyjson");
        }

        cmd[0] = '\0';
        sb_add(cmd, sizeof(cmd), cplus);
        sb_add(cmd, sizeof(cmd), " build ");
        sb_add(cmd, sizeof(cmd), r->source_abs);
        sb_add(cmd, sizeof(cmd), " --strict --sanitize=none");
        sb_add(cmd, sizeof(cmd), extra);
        sb_add(cmd, sizeof(cmd), " -o ");
        sb_add(cmd, sizeof(cmd), exe);
        snprintf(r->build_command, sizeof(r->build_command), "%s", cmd);

        cmd[0] = '\0';
        sb_add(cmd, sizeof(cmd), "BUILD: ");
        sb_add(cmd, sizeof(cmd), r->build_command);
        sb_add(cmd, sizeof(cmd), "\n");
        write_file(cmd_log, cmd, strlen(cmd));

        brc = run_capture(r->build_command, build_timeout, NULL,
                          build_out, build_err);
        r->build_exit = brc;
        {
            long bol = read_exact(build_out, text, sizeof(text));
            long bel = read_exact(build_err, linebuf, sizeof(linebuf));
            size_t a = bol > 0 ? (size_t)bol : 0;
            size_t b = bel > 0 ? (size_t)bel : 0;
            if (a >= sizeof(r->build_stdout)) a = sizeof(r->build_stdout) - 1;
            if (b >= sizeof(r->build_stderr)) b = sizeof(r->build_stderr) - 1;
            if (a > 0) memcpy(r->build_stdout, text, a);
            r->build_stdout[a] = '\0';
            if (b > 0) memcpy(r->build_stderr, linebuf, b);
            r->build_stderr[b] = '\0';

            cmd[0] = '\0';
            if (a > 0) {
                sb_add(cmd, sizeof(cmd), r->build_stdout);
            }
            sb_add(cmd, sizeof(cmd), "\n--- stderr ---\n");
            if (b > 0) {
                sb_add(cmd, sizeof(cmd), r->build_stderr);
            }
            write_file(build_log, cmd, strlen(cmd));
        }

        if (brc != 0) {
            r->classification = (brc == 124 || brc >= 128) ? CL_TIMEOUT
                                                            : CL_BUILD_FAILURE;
            snprintf(r->reason, sizeof(r->reason), "compiler exit %d", brc);
            counts[r->classification]++;
            continue;
        }

        /* stdin fixture: STD first, then AG; copy into a private temp file. */
        stdin_len = 0;
        snprintf(stdin_file, sizeof(stdin_file), "%s/%s.stdin", std_dir, r->name);
        if (file_exists(stdin_file)) {
            stdin_len = read_exact(stdin_file, stdin_data, sizeof(stdin_data));
        } else {
            snprintf(stdin_file, sizeof(stdin_file), "%s/%s.stdin", ag_dir, r->name);
            if (file_exists(stdin_file))
                stdin_len = read_exact(stdin_file, stdin_data, sizeof(stdin_data));
        }
        {
            char tmp[PATH_CAP];
            snprintf(tmp, sizeof(tmp), "%s/.native_stdin", results);
            if (stdin_len > 0) write_file(tmp, stdin_data, (size_t)stdin_len);
            else { tmp[0] = '\0'; }
            snprintf(stdin_file, sizeof(stdin_file), "%s", tmp);
        }

        snprintf(r->runtime_command, sizeof(r->runtime_command), "%s", exe);
        r->has_runtime = 1;
        cmd[0] = '\0';
        sb_add(cmd, sizeof(cmd), "RUN: ");
        sb_add(cmd, sizeof(cmd), exe);
        sb_add(cmd, sizeof(cmd), "\n");
        append_file(cmd_log, cmd, strlen(cmd));

        rrc = run_capture(exe, run_timeout, stdin_file, stdout_log, stderr_log);
        out_len = read_exact(stdout_log, out_data, sizeof(out_data));
        err_len = read_exact(stderr_log, err_data, sizeof(err_data));
        if (out_len < 0) out_len = 0;
        if (err_len < 0) err_len = 0;
        if (out_len >= (long)sizeof(r->stdout)) out_len = (long)sizeof(r->stdout) - 1;
        if (err_len >= (long)sizeof(r->stderr)) err_len = (long)sizeof(r->stderr) - 1;
        memcpy(r->stdout, out_data, (size_t)out_len);
        r->stdout[out_len] = '\0';
        memcpy(r->stderr, err_data, (size_t)err_len);
        r->stderr[err_len] = '\0';

        /* Expectation fixtures: last matching base wins; .exit first wins. */
        {
            const char *bases[3];
            int bi;
            bases[0] = r->parent;
            bases[1] = std_dir;
            bases[2] = ag_dir;
            for (bi = 0; bi < 3; ++bi) {
                snprintf(exp_path, sizeof(exp_path), "%s/%s.expected",
                         bases[bi], r->name);
                if (file_exists(exp_path)) {
                    r->has_expectation = 1;
                    relative_path(exp_path, root, r->expectation,
                                  sizeof(r->expectation));
                }
                snprintf(exp_err_path, sizeof(exp_err_path),
                         "%s/%s.stderr.expected", bases[bi], r->name);
                if (file_exists(exp_err_path)) {
                    exp_err_found = 1;
                    relative_path(exp_err_path, root, exp_err_rel, sizeof(exp_err_rel));
                }
            }
            for (bi = 0; bi < 3; ++bi) {
                snprintf(exp_exit_path, sizeof(exp_exit_path), "%s/%s.exit",
                         bases[bi], r->name);
                if (file_exists(exp_exit_path)) {
                    char eb[64];
                    long el = read_exact(exp_exit_path, eb, sizeof(eb));
                    if (el > 0) exp_exit = atoi(eb);
                    break;
                }
            }
        }
        r->expected_exit = exp_exit;

        diffbuf[0] = '\0';
        if (r->has_expectation) {
            snprintf(exp_path, sizeof(exp_path), "%s/%s", root, r->expectation);
            exp_len = read_exact(exp_path, exp_data, sizeof(exp_data));
            if (exp_len < 0) exp_len = 0;
            if (exp_len != out_len || memcmp(exp_data, out_data, (size_t)exp_len) != 0) {
                snprintf(linebuf, sizeof(linebuf), "stdout differs from %s\n",
                         r->expectation);
                sb_add(diffbuf, sizeof(diffbuf), linebuf);
                stdout_diff = 1;
                ++diff_count;
            }
        }
        if (exp_err_found) {
            snprintf(exp_err_path, sizeof(exp_err_path), "%s/%s", root, exp_err_rel);
            exp_err_len = read_exact(exp_err_path, exp_err_data,
                                     sizeof(exp_err_data));
            if (exp_err_len < 0) exp_err_len = 0;
            if (exp_err_len != err_len ||
                memcmp(exp_err_data, err_data, (size_t)exp_err_len) != 0) {
                snprintf(linebuf, sizeof(linebuf), "stderr differs from %s\n", exp_err_rel);
                sb_add(diffbuf, sizeof(diffbuf), linebuf);
                stderr_diff = 1;
                ++diff_count;
            }
        }
        if (diff_count > 0) write_file(diff_log, diffbuf, strlen(diffbuf));

        exit_match = (rrc == exp_exit);
        r->runtime_exit = rrc;
        r->classification = classify(brc, rrc, !stdout_diff, !stderr_diff,
                                     exit_match);

        if (diff_count > 0) {
            char reason[REASON_CAP];
            char *p = diffbuf;
            reason[0] = '\0';
            while (*p != '\0') {
                char *e = strchr(p, '\n');
                if (e != NULL) *e = '\0';
                if (reason[0] != '\0') sb_add(reason, sizeof(reason), "; ");
                sb_add(reason, sizeof(reason), p);
                if (e == NULL) break;
                p = e + 1;
            }
            snprintf(r->reason, sizeof(r->reason), "%s", reason);
        } else if (!exit_match) {
            snprintf(r->reason, sizeof(r->reason), "exit %d != %d", rrc, exp_exit);
        }
        counts[r->classification]++;
    }

    /* Head and timestamp.  Overwrite the deep manifest with git output. */
    head[0] = '\0';
    cmd[0] = '\0';
    sb_add(cmd, sizeof(cmd), "git -C ");
    sb_addq(cmd, sizeof(cmd), root);
    sb_add(cmd, sizeof(cmd), " rev-parse --short HEAD 2>/dev/null >");
    sb_addq(cmd, sizeof(cmd), manifest_deep);
    cplus_system(cmd);
    {
        long hl = read_exact(manifest_deep, text, sizeof(text));
        if (hl > 0) {
            char *p = text;
            char *e;
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
            e = strchr(p, '\n');
            if (e != NULL) *e = '\0';
            snprintf(head, sizeof(head), "%s", p);
        }
    }
    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &tm_now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &tm_now);
    {
        char us[32];
        snprintf(us, sizeof(us), ".%06ld", (long)tv.tv_usec);
        sb_add(timestamp, sizeof(timestamp), us);
    }

    /* summary.json: manual writer matching Python json.dumps(indent=2). */
    {
        error e = error();
        char json_path[PATH_CAP];
        char line[256];
        int j;
        snprintf(json_path, sizeof(json_path), "%s/summary.json", results);
        {
            file out = file(json_path, "w");
            if (!out.is_open()) {
                fprintf(stderr, "cannot write summary.json\n");
                return 1;
            }
            out.write("{\n");
            snprintf(line, sizeof(line), "  \"schema_version\": 1,\n");
            out.write(line);
            snprintf(line, sizeof(line), "  \"generated_at\": ");
            out.write(line); json_string(&out, timestamp); out.write(",\n");
            snprintf(line, sizeof(line), "  \"head\": ");
            out.write(line); json_string(&out, head); out.write(",\n");
            snprintf(line, sizeof(line), "  \"compiler\": ");
            out.write(line); json_string(&out, cplus); out.write(",\n");
            snprintf(line, sizeof(line), "  \"build_timeout\": %d,\n", build_timeout);
            out.write(line);
            snprintf(line, sizeof(line), "  \"run_timeout\": %d,\n", run_timeout);
            out.write(line);
            out.write("  \"counts\": {\n");
            snprintf(line, sizeof(line), "    \"PASS\": %d,\n", counts[CL_PASS]);
            out.write(line);
            snprintf(line, sizeof(line), "    \"FAIL\": %d,\n", counts[CL_FAIL]);
            out.write(line);
            snprintf(line, sizeof(line), "    \"BUILD_FAILURE\": %d,\n",
                     counts[CL_BUILD_FAILURE]);
            out.write(line);
            snprintf(line, sizeof(line), "    \"TIMEOUT\": %d,\n", counts[CL_TIMEOUT]);
            out.write(line);
            snprintf(line, sizeof(line), "    \"SKIPPED\": %d,\n", counts[CL_SKIPPED]);
            out.write(line);
            snprintf(line, sizeof(line), "    \"BLOCKED\": %d\n", counts[CL_BLOCKED]);
            out.write(line);
            out.write("  },\n");
            snprintf(line, sizeof(line), "  \"total\": %d,\n", count);
            out.write(line);
            out.write("  \"results\": [\n");
            for (j = 0; j < count; ++j) {
                struct result_t *r = &records[j];
                int last = (j == count - 1);
                out.write("    {\n");
                json_str_field(&out, "name", r->name, 1);
                json_str_field(&out, "group", r->group, 1);
                json_str_field(&out, "module", r->module, 1);
                json_str_field(&out, "source", r->source, 1);
                json_str_field(&out, "build_command", r->build_command, 1);
                json_int(&out, "build_exit", r->build_exit, 1);
                json_str_field(&out, "build_stdout", r->build_stdout, 1);
                json_str_field(&out, "build_stderr", r->build_stderr, 1);
                json_nullable(&out, "expectation", r->expectation,
                              r->has_expectation, 1);
                json_int(&out, "expected_exit", r->expected_exit, 1);
                json_nullable(&out, "runtime_command", r->runtime_command,
                              r->has_runtime, 1);
                if (r->has_runtime)
                    json_int(&out, "runtime_exit", r->runtime_exit, 1);
                else
                    out.write("      \"runtime_exit\": null,\n");
                json_str_field(&out, "stdout", r->stdout, 1);
                json_str_field(&out, "stderr", r->stderr, 1);
                json_str_field(&out, "classification",
                               class_name(r->classification), 1);
                json_str_field(&out, "reason", r->reason, 0);
                out.write("    }");
                out.write(last ? "\n" : ",\n");
            }
            out.write("  ]\n");
            out.write("}\n");
            out.flush(&e);
        }
    }

    /* summary.txt plus terminal echo. */
    {
        char txt_path[PATH_CAP];
        error e = error();
        file out;
        snprintf(txt_path, sizeof(txt_path), "%s/summary.txt", results);
        out = file(txt_path, "w");
        if (!out.is_open()) {
            fprintf(stderr, "cannot write summary.txt\n");
            return 1;
        }
        snprintf(linebuf, sizeof(linebuf),
                 "Agent 2 aggregate regression summary (%s)\n", head);
        out.write(linebuf);
        snprintf(linebuf, sizeof(linebuf),
                 "compiler=%s build_timeout=%ds run_timeout=%ds\n",
                 cplus, build_timeout, run_timeout);
        out.write(linebuf);
        out.write("\n");
        snprintf(linebuf, sizeof(linebuf),
                 "%-40s %-9s %-11s %-14s BUILD RUN DETAIL\n",
                 "TEST", "GROUP", "MODULE", "CLASS");
        out.write(linebuf);
        for (i = 0; i < count; ++i) {
            struct result_t *r = &records[i];
            char detail[REASON_CAP];
            char rexit[16];
            size_t n = strlen(r->reason);
            if (n >= sizeof(detail)) n = sizeof(detail) - 1;
            memcpy(detail, r->reason, n);
            detail[n] = '\0';
            if (r->has_runtime) snprintf(rexit, sizeof(rexit), "%d", r->runtime_exit);
            else snprintf(rexit, sizeof(rexit), "null");
            snprintf(linebuf, sizeof(linebuf),
                     "%-40s %-9s %-11s %-14s %5d %-3s %.70s\n",
                     r->name, r->group, r->module,
                     class_name(r->classification), r->build_exit,
                     rexit, detail);
            out.write(linebuf);
        }
        out.write("\n");
        snprintf(linebuf, sizeof(linebuf),
                 "SUMMARY PASS=%d FAIL=%d BUILD_FAILURE=%d TIMEOUT=%d "
                 "SKIPPED=%d BLOCKED=%d TOTAL=%d\n",
                 counts[CL_PASS], counts[CL_FAIL], counts[CL_BUILD_FAILURE],
                 counts[CL_TIMEOUT], counts[CL_SKIPPED], counts[CL_BLOCKED],
                 count);
        out.write(linebuf);
        out.flush(&e);

        {
            char echo[OUT_CAP];
            long n = read_exact(txt_path, echo, sizeof(echo));
            if (n > 0) fwrite(echo, 1, (size_t)n, stdout);
        }
    }

    /* Remove transient helper files so results/ has the same file set as
     * the Python runner produced. */
    {
        char tmp[PATH_CAP];
        remove(manifest_deep);
        remove(manifest_agent);
        snprintf(tmp, sizeof(tmp), "%s/.native_build_out", results);
        remove(tmp);
        snprintf(tmp, sizeof(tmp), "%s/.native_build_err", results);
        remove(tmp);
        snprintf(tmp, sizeof(tmp), "%s/.native_stdin", results);
        remove(tmp);
    }

    bad = counts[CL_FAIL] + counts[CL_BUILD_FAILURE] + counts[CL_TIMEOUT] +
          counts[CL_SKIPPED] + counts[CL_BLOCKED];
    return bad ? 1 : 0;
}
