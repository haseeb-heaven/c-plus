/*
 * Exhaustive edge-case test for the C+ rich diagnostic engine (safe_diag).
 *
 * Every case runs in a forked child with stderr redirected to a pipe. The
 * parent applies byte/substring assertions to the captured output. No case may
 * crash, hang, or emit an unexpected diagnostic.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "safe_diag.h"

static int g_pass = 0;
static int g_fail = 0;

/* Run fn in a child with env vars applied, return captured stderr in out. */
static int capture(void (*fn)(void), const char *diag, const char *color,
                   const char *no_color, char *out, size_t outsz) {
    int fd[2];
    pid_t pid;
    ssize_t n;
    size_t total = 0;
    if (pipe(fd) != 0) return -1;
    pid = fork();
    if (pid < 0) { close(fd[0]); close(fd[1]); return -1; }
    if (pid == 0) {
        close(fd[0]);
        if (dup2(fd[1], STDERR_FILENO) < 0) _exit(2);
        close(fd[1]);
        if (diag) setenv("CPLUS_DIAG", diag, 1); else unsetenv("CPLUS_DIAG");
        if (color) setenv("CPLUS_COLOR", color, 1); else unsetenv("CPLUS_COLOR");
        if (no_color) setenv("NO_COLOR", no_color, 1); else unsetenv("NO_COLOR");
        cplus_diag_init_from_env();
        fn();
        fflush(stderr);
        _exit(0);
    }
    close(fd[1]);
    while (total < outsz - 1 &&
           (n = read(fd[0], out + total, outsz - 1 - total)) > 0)
        total += (size_t)n;
    out[total] = '\0';
    close(fd[0]);
    { int st = 0; waitpid(pid, &st, 0);
      if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) return -1; }
    return (int)total;
}

static void check(const char *name, int ok, const char *detail) {
    if (ok) { ++g_pass; printf("PASS %s\n", name); }
    else { ++g_fail; printf("FAIL %s %s\n", name, detail ? detail : ""); }
}

static int has(const char *hay, const char *needle) {
    return hay && strstr(hay, needle) != NULL;
}

static int has_esc(const char *s) { return s && strchr(s, '\x1b') != NULL; }

/* ------------------------------------------------------------- emit cases */
static void emit_violation(void) {
    cplus_diag_violation_at("cplus_memcpy_s", "buffer overflow refused",
                            "stdlib/safe/src/safe_memory.c", 79,
                            "cplus_safe_report_overflow");
}
static void emit_report(void) {
    cplus_diag_report_at("undefined behavior detected", "runtime/cplus_runtime.c",
                         148, "__ubsan_on_report");
}
static void emit_log(void) {
    cplus_diag_log_at(1, "strcpy_s", "null pointer", "safe_string.c", 32,
                      "check_dest");
}
static void emit_null(void) { cplus_diag_emit(NULL, stderr); }
static void emit_unknown(void) {
    cplus_diag_record r;
    r.kind = CPLUS_DIAG_VIOLATION; r.level = 1; r.code = 9999;
    r.api = NULL; r.detail = NULL; r.file = NULL; r.line = 0; r.func = NULL;
    cplus_diag_emit(&r, stderr);
}
static char g_long_detail[12000];
static void emit_long(void) {
    cplus_diag_violation_at("api", g_long_detail, "f.c", 1, "fn");
}

int main(void) {
    char out[32768];
    char buf[512];
    int n, i, known;

    /* 1-3: LEGACY output is byte-identical to the historical emitters. */
    n = capture(emit_violation, "legacy", NULL, NULL, out, sizeof(out));
    check("legacy_violation_exact",
          n == (int)strlen("[CPLUS-SAFE] cplus_memcpy_s: buffer overflow refused\n") &&
          strcmp(out, "[CPLUS-SAFE] cplus_memcpy_s: buffer overflow refused\n") == 0,
          out);

    n = capture(emit_report, "legacy", NULL, NULL, out, sizeof(out));
    check("legacy_report_exact",
          n == (int)strlen("[CPLUS-RUNTIME] runtime error: undefined behavior detected\n") &&
          strcmp(out, "[CPLUS-RUNTIME] runtime error: undefined behavior detected\n") == 0,
          out);

    n = capture(emit_log, "legacy", NULL, NULL, out, sizeof(out));
    check("legacy_log_exact",
          strcmp(out, "[SAFECRT][1] strcpy_s: null pointer\n") == 0, out);

    /* 4: rich mode, color never -> detail + code + location + explanation, no ESC. */
    n = capture(emit_violation, "rich", "never", NULL, out, sizeof(out));
    check("rich_content",
          n > 0 && has(out, "fatal[101]") && has(out, "CPLUS_EXIT_BOUNDS") &&
          has(out, "--> stdlib/safe/src/safe_memory.c:79 in cplus_safe_report_overflow") &&
          has(out, "buffer overflow refused") && has(out, "Hint:") &&
          has(out, "ESNOSPC") == 0, out);
    check("rich_no_color_when_never", !has_esc(out), out);

    /* 5: rich + color always -> ANSI sequences and a reset. */
    n = capture(emit_violation, "rich", "always", NULL, out, sizeof(out));
    check("rich_color_always", has_esc(out) && has(out, "\x1b[0m"), out);
    check("rich_color_header", has(out, "\x1b[1;91mfatal[101]"), out);

    /* 6: NO_COLOR overrides CPLUS_COLOR=always. */
    n = capture(emit_violation, "rich", "always", "1", out, sizeof(out));
    check("no_color_overrides_always", !has_esc(out) && has(out, "fatal[101]"), out);

    /* 7: plain mode -> rich content without color. */
    n = capture(emit_violation, "plain", "always", NULL, out, sizeof(out));
    check("plain_mode_no_color", !has_esc(out) && has(out, "CPLUS_EXIT_BOUNDS") &&
          has(out, "Hint:"), out);

    /* 8: auto color with piped stderr -> no ESC. */
    n = capture(emit_violation, "rich", "auto", NULL, out, sizeof(out));
    check("auto_color_pipe_no_esc", !has_esc(out), out);

    /* 9: report rich uses the controlled-failure code and its explanation. */
    n = capture(emit_report, "rich", "never", NULL, out, sizeof(out));
    check("rich_report_code", has(out, "error[70]") && has(out, "EX_SOFTWARE") &&
          has(out, "runtime/cplus_runtime.c:148 in __ubsan_on_report") &&
          has(out, "Hint:"), out);

    /* 10: SAFECRT error level maps to EINVAL with an explanation. */
    n = capture(emit_log, "rich", "never", NULL, out, sizeof(out));
    check("rich_log_code", has(out, "error[22]") && has(out, "EINVAL") &&
          has(out, "safe_string.c:32 in check_dest"), out);

    /* 11: unknown code is explained generically, no crash on NULL fields. */
    n = capture(emit_unknown, "rich", "never", NULL, out, sizeof(out));
    check("rich_unknown_code", has(out, "UNKNOWN") && has(out, "9999") &&
          has(out, "(unnamed)") && has(out, "(no detail)") && has(out, "Hint:"), out);

    /* 12: NULL record is safe in every mode. */
    n = capture(emit_null, "legacy", NULL, NULL, out, sizeof(out));
    check("null_record_legacy", has(out, "[CPLUS-RUNTIME]") && n > 0, out);
    n = capture(emit_null, "rich", "never", NULL, out, sizeof(out));
    check("null_record_rich", n > 0 && !has_esc(out), out);

    /* 13: very long detail is accepted without crash (output stays bounded). */
    memset(g_long_detail, 'A', sizeof(g_long_detail) - 1);
    g_long_detail[sizeof(g_long_detail) - 1] = '\0';
    n = capture(emit_long, "rich", "never", NULL, out, sizeof(out));
    check("long_detail_bounded", n > 0 && n <= (int)sizeof(out) - 1, out);

    /* 14-16: code explanation table. */
    check("explain_known", cplus_diag_explain(406, buf, sizeof(buf)) == 1 &&
          has(buf, "ESNOSPC") && has(buf, "406") && has(buf, "Hint:"), buf);
    check("explain_unknown", cplus_diag_explain(9999, buf, sizeof(buf)) == 0 &&
          has(buf, "UNKNOWN"), buf);
    check("explain_zero_size", cplus_diag_explain(406, NULL, 0) == 1 &&
          cplus_diag_explain(9999, NULL, 0) == 0, "");

    /* 17: every documented code has a name, meaning and hint. */
    {
        int codes[] = {0,12,22,34,70,75,95,400,401,403,406,409,
                       101,102,103,104,105,106,107,108,109};
        int ok = 1;
        for (i = 0; i < (int)(sizeof(codes)/sizeof(codes[0])); ++i) {
            if (cplus_diag_explain(codes[i], buf, sizeof(buf)) != 1 ||
                strcmp(cplus_diag_code_name(codes[i]), "UNKNOWN") == 0 ||
                cplus_diag_hint(codes[i])[0] == '\0' || !has(buf, "Hint:")) {
                ok = 0;
                printf("  code %d -> %s\n", codes[i], buf);
            }
        }
        check("all_codes_explained", ok, "");
    }

    /* 18: config setters/getters round-trip and override env. */
    cplus_diag_init_from_env();
    cplus_diag_set_mode(CPLUS_DIAG_PLAIN);
    check("set_mode", cplus_diag_get_mode() == CPLUS_DIAG_PLAIN, "");
    cplus_diag_set_color(CPLUS_DIAG_COLOR_NEVER);
    check("set_color", cplus_diag_get_color() == CPLUS_DIAG_COLOR_NEVER &&
          cplus_diag_color_enabled(stderr) == 0, "");
    cplus_diag_set_mode(CPLUS_DIAG_LEGACY);
    cplus_diag_set_color(CPLUS_DIAG_COLOR_AUTO);

    /* 19: garbage env values leave the programmatic mode intact. */
    n = capture(emit_violation, "garbage", "garbage", NULL, out, sizeof(out));
    check("garbage_env_default_legacy",
          strcmp(out, "[CPLUS-SAFE] cplus_memcpy_s: buffer overflow refused\n") == 0,
          out);

    printf("DIAG_TEST %d %d %d\n", g_pass + g_fail, g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
