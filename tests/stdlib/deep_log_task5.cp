/* Independent deep contract tests for stdlib/log.hp -- Task 5 uncovered cases.
 *
 * This file deliberately contains ONLY cases not already exercised by
 * log_test.cp.  That test covers basic level filtering, structured fields,
 * RAII/borrowed sinks, one invalid-path open, and cleanup.  The following are
 * new here:
 *
 *   1. log_level_parse: all six valid names in mixed case, numeric strings,
 *      empty string and NULL -- exact return value, category and exit code;
 *   2. log_level_name: every level plus out-of-range values;
 *   3. log_open_file with a NULL path and with an unopenable path -> invalid
 *      sink, exact error category/code, safe writes on the dead sink, cleanup;
 *   4. threshold boundary: levels below the threshold suppressed (but still
 *      STDLIB_OK), levels at/above emitted, verified from the file;
 *   5. boundary message lengths (empty, 255, 256, 4095 bytes) into a file sink
 *      -- line integrity, truncation marker (STDLIB_CATEGORY_BOUNDS /
 *      CPLUS_EXIT_BOUNDS) for the oversized case, and file still usable after;
 *   6. enable/disable parity of return value and error state (C+ error object
 *      AND libc errno) for every sink method: write, info, warn, failure;
 *   7. 32 repeated open/write/drop cycles with no fd leak and exact line count.
 *
 * All temp files use PID-unique names and every file created is unlinked and
 * asserted absent at the end.
 *
 * SELF-CHECKING: every t5_check() prints "<tag> <1|0>" (1 = pass).  The last
 * line is "LOG_TASK5 <checks> <failures>".  Exit 0 means zero failures.  Every
 * binary run is bounded externally with `timeout 30`.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/log.hp"

static int t5_checks = 0;
static int t5_failures = 0;

static void t5_check(const char *tag, int cond) {
    t5_checks++;
    if (!cond)
        t5_failures++;
    print(tag, cond ? 1 : 0);
}

static int t5_path_absent(const char *path) {
    FILE *probe = fopen(path, "r");
    if (probe == NULL)
        return 1;
    fclose(probe);
    return 0;
}

static int t5_count_fds(void) {
    int count = 0;
    int fd;
    for (fd = 0; fd < 256; fd++) {
        if (fcntl(fd, F_GETFD) != -1)
            count++;
    }
    return count;
}

/* Count lines and verify each one is newline-terminated and shorter than
 * LOG_LINE_SIZE (no runaway/truncated line was flushed). */
static int t5_file_lines_wellformed(const char *path, int *out_lines,
                                    int *out_maxlen) {
    FILE *reader = fopen(path, "r");
    char buf[1024];
    int lines = 0;
    int maxlen = 0;
    int ok = 1;
    if (reader == NULL)
        return 0;
    while (fgets(buf, sizeof(buf), reader) != NULL) {
        size_t n = strlen(buf);
        lines++;
        if (n > 0 && buf[n - 1] == '\n')
            buf[n - 1] = '\0';
        else
            ok = 0;
        n = strlen(buf);
        if (n > (size_t)maxlen)
            maxlen = (int)n;
        if (n >= LOG_LINE_SIZE)
            ok = 0;
    }
    fclose(reader);
    *out_lines = lines;
    *out_maxlen = maxlen;
    return ok;
}

static int t5_file_contains(const char *path, const char *needle) {
    FILE *reader = fopen(path, "r");
    char buf[1024];
    int found = 0;
    if (reader == NULL)
        return 0;
    while (fgets(buf, sizeof(buf), reader) != NULL) {
        if (strstr(buf, needle) != NULL) {
            found = 1;
            break;
        }
    }
    fclose(reader);
    return found;
}

struct t5_call_outcome {
    int ret;
    int err_ok;
    int code;
    int cat;
    int libc_errno;
};

/* Invoke one sink method, capturing its return value, C+ error state and libc
 * errno (reset to 0 immediately before the call). */
static struct t5_call_outcome t5_invoke(log_sink *sink, int kind,
                                        const char *msg, error *err) {
    struct t5_call_outcome out;
    errno = 0;
    if (kind == 0)
        out.ret = sink->write(LOG_INFO, msg, NULL, NULL, 0, NULL, 0, err);
    else if (kind == 1)
        out.ret = sink->info(msg, err);
    else if (kind == 2)
        out.ret = sink->warn(msg, err);
    else
        out.ret = sink->failure(msg, err);
    out.err_ok = err->ok();
    out.code = err->code();
    out.cat = err->category();
    out.libc_errno = errno;
    return out;
}

static int t5_parse_failure(const char *text, error *err) {
    int got = log_level_parse(text, err);
    return got == -1 && err->failed_now() &&
           err->category() == STDLIB_CATEGORY_INVALID &&
           err->code() == CPLUS_EXIT_UNSUPPORTED;
}

int main(void) {
    error t5_err = error();
    char t5_pidtag[32];
    char t5_path[96];
    int t5_lines = 0;
    int t5_maxlen = 0;

    snprintf(t5_pidtag, sizeof(t5_pidtag), "%d", (int)getpid());
    snprintf(t5_path, sizeof(t5_path), "cplus_deep_log_task5_%s.log",
             t5_pidtag);
    unlink(t5_path);

    /* ---- 1. log_level_parse --------------------------------------------- */
    {
        int t5_ok_all =
            log_level_parse("TRACE", &t5_err) == LOG_TRACE && t5_err.ok() &&
            log_level_parse("Debug", &t5_err) == LOG_DEBUG && t5_err.ok() &&
            log_level_parse("iNfO", &t5_err) == LOG_INFO && t5_err.ok() &&
            log_level_parse("wArN", &t5_err) == LOG_WARN && t5_err.ok() &&
            log_level_parse("ERROR", &t5_err) == LOG_ERROR && t5_err.ok() &&
            log_level_parse("FaTaL", &t5_err) == LOG_FATAL && t5_err.ok();
        t5_check("t5_parse_valid_mixed_case", t5_ok_all);
        t5_check("t5_parse_numeric_0", t5_parse_failure("0", &t5_err));
        t5_check("t5_parse_numeric_3", t5_parse_failure("3", &t5_err));
        t5_check("t5_parse_numeric_5", t5_parse_failure("5", &t5_err));
        t5_check("t5_parse_numeric_neg", t5_parse_failure("-1", &t5_err));
        t5_check("t5_parse_empty", t5_parse_failure("", &t5_err));
        t5_check("t5_parse_null", t5_parse_failure(NULL, &t5_err));
    }

    /* ---- 2. log_level_name ---------------------------------------------- */
    {
        int t5_names_ok =
            strcmp(log_level_name(LOG_TRACE), "TRACE") == 0 &&
            strcmp(log_level_name(LOG_DEBUG), "DEBUG") == 0 &&
            strcmp(log_level_name(LOG_INFO), "INFO") == 0 &&
            strcmp(log_level_name(LOG_WARN), "WARN") == 0 &&
            strcmp(log_level_name(LOG_ERROR), "ERROR") == 0 &&
            strcmp(log_level_name(LOG_FATAL), "FATAL") == 0;
        int t5_unknown_ok =
            strcmp(log_level_name(-1), "UNKNOWN") == 0 &&
            strcmp(log_level_name(LOG_LEVELS), "UNKNOWN") == 0 &&
            strcmp(log_level_name(99), "UNKNOWN") == 0;
        t5_check("t5_name_all_levels", t5_names_ok);
        t5_check("t5_name_out_of_range", t5_unknown_ok);
    }

    /* ---- 3. log_open_file invalid / NULL paths -------------------------- */
    {
        log_sink t5_null_sink = log_open_file(NULL, LOG_INFO, &t5_err);
        int t5_null_st;
        t5_check("t5_open_null_invalid",
                 !t5_null_sink.valid() && t5_null_sink.native() == NULL);
        t5_check("t5_open_null_error",
                 t5_err.failed_now() &&
                     t5_err.category() == STDLIB_CATEGORY_INVALID &&
                     t5_err.code() == CPLUS_EXIT_UNSUPPORTED);
        t5_null_st = t5_null_sink.info("x", &t5_err);
        t5_check("t5_open_null_write",
                 t5_null_st == STDLIB_FAIL &&
                     t5_err.category() == STDLIB_CATEGORY_INVALID &&
                     t5_err.code() == CPLUS_EXIT_UNSUPPORTED);
        t5_null_st = t5_null_sink.write(LOG_INFO, "x", NULL, NULL, 0, NULL, 0,
                                        &t5_err);
        t5_check("t5_open_null_write_direct",
                 t5_null_st == STDLIB_FAIL && t5_err.failed_now());
        t5_null_st = t5_null_sink.warn("x", &t5_err);
        t5_check("t5_open_null_warn",
                 t5_null_st == STDLIB_FAIL && t5_err.failed_now());
        t5_null_st = t5_null_sink.failure("x", &t5_err);
        t5_check("t5_open_null_failure",
                 t5_null_st == STDLIB_FAIL && t5_err.failed_now());
        t5_check("t5_open_null_disabled", !t5_null_sink.enabled(LOG_FATAL));
        t5_check("t5_open_null_level", t5_null_sink.level() == LOG_INFO);
        t5_null_sink.drop();
        t5_null_sink.drop();

        {
            log_sink t5_bad_sink = log_open_file("/no/such/dir/task5/x.log",
                                                 LOG_WARN, &t5_err);
            t5_check("t5_open_bad_invalid",
                     !t5_bad_sink.valid() && t5_bad_sink.native() == NULL);
            t5_check("t5_open_bad_error",
                     t5_err.failed_now() &&
                         t5_err.category() == STDLIB_CATEGORY_IO &&
                         t5_err.code() == CPLUS_EXIT_IO);
            t5_check("t5_open_bad_disabled",
                     !t5_bad_sink.enabled(LOG_FATAL));
            t5_check("t5_open_bad_level", t5_bad_sink.level() == LOG_WARN);
            t5_check("t5_open_bad_write",
                     t5_bad_sink.failure("x", &t5_err) == STDLIB_FAIL &&
                         t5_err.category() == STDLIB_CATEGORY_INVALID);
            t5_bad_sink.drop();
        }
    }

    /* ---- 4. threshold boundary ------------------------------------------ */
    unlink(t5_path);
    {
        log_sink t5_threshold_sink = log_open_file(t5_path, LOG_WARN, &t5_err);
        int t5_below;
        int t5_above;
        t5_check("t5_threshold_open",
                 t5_threshold_sink.valid() &&
                     t5_threshold_sink.level() == LOG_WARN);
        t5_below =
            t5_threshold_sink.write(LOG_TRACE, "below_trace", NULL, NULL, 0,
                                    NULL, 0, &t5_err) == STDLIB_OK &&
            t5_threshold_sink.write(LOG_DEBUG, "below_debug", NULL, NULL, 0,
                                    NULL, 0, &t5_err) == STDLIB_OK &&
            t5_threshold_sink.write(LOG_INFO, "below_info", NULL, NULL, 0,
                                    NULL, 0, &t5_err) == STDLIB_OK &&
            t5_err.ok();
        t5_check("t5_threshold_below_ok", t5_below);
        t5_above =
            t5_threshold_sink.write(LOG_WARN, "at_warn", NULL, NULL, 0,
                                    NULL, 0, &t5_err) == STDLIB_OK &&
            t5_threshold_sink.write(LOG_ERROR, "above_error", NULL, NULL, 0,
                                    NULL, 0, &t5_err) == STDLIB_OK &&
            t5_threshold_sink.write(LOG_FATAL, "above_fatal", NULL, NULL, 0,
                                    NULL, 0, &t5_err) == STDLIB_OK &&
            t5_err.ok();
        t5_check("t5_threshold_above_ok", t5_above);
    }
    t5_check("t5_threshold_lines",
             t5_file_lines_wellformed(t5_path, &t5_lines, &t5_maxlen) &&
                 t5_lines == 3);
    t5_check("t5_threshold_no_below", !t5_file_contains(t5_path, "below_"));
    t5_check("t5_threshold_has_above",
             t5_file_contains(t5_path, "at_warn") &&
                 t5_file_contains(t5_path, "above_error") &&
                 t5_file_contains(t5_path, "above_fatal"));

    /* ---- 5. boundary message lengths ------------------------------------ */
    unlink(t5_path);
    {
        char t5_msg[4096];
        int t5_empty_rc;
        int t5_255_rc;
        int t5_256_rc;
        int t5_4095_rc;
        int t5_after_rc;
        log_sink t5_len_sink = log_open_file(t5_path, LOG_TRACE, &t5_err);
        t5_check("t5_len_open", t5_len_sink.valid());

        memset(t5_msg, 'A', sizeof(t5_msg));
        t5_msg[0] = '\0';
        t5_empty_rc = t5_len_sink.write(LOG_INFO, t5_msg, NULL, NULL, 0, NULL,
                                        0, &t5_err);
        t5_check("t5_len_empty", t5_empty_rc == STDLIB_OK && t5_err.ok());

        memset(t5_msg, 'A', sizeof(t5_msg));
        memcpy(t5_msg, "M255-", 5);
        t5_msg[255] = '\0';
        t5_255_rc = t5_len_sink.write(LOG_INFO, t5_msg, NULL, NULL, 0, NULL, 0,
                                      &t5_err);
        t5_check("t5_len_255", t5_255_rc == STDLIB_OK && t5_err.ok());

        memset(t5_msg, 'B', sizeof(t5_msg));
        memcpy(t5_msg, "M256-", 5);
        t5_msg[256] = '\0';
        t5_256_rc = t5_len_sink.write(LOG_INFO, t5_msg, NULL, NULL, 0, NULL, 0,
                                      &t5_err);
        t5_check("t5_len_256", t5_256_rc == STDLIB_OK && t5_err.ok());

        memset(t5_msg, 'C', sizeof(t5_msg));
        memcpy(t5_msg, "M4095-", 6);
        t5_msg[4095] = '\0';
        t5_4095_rc = t5_len_sink.write(LOG_INFO, t5_msg, NULL, NULL, 0, NULL,
                                       0, &t5_err);
        t5_check("t5_len_4095_rejected", t5_4095_rc == STDLIB_FAIL);
        t5_check("t5_len_4095_bounds",
                 t5_err.failed_now() &&
                     t5_err.category() == STDLIB_CATEGORY_BOUNDS &&
                     t5_err.code() == CPLUS_EXIT_BOUNDS);
        t5_check("t5_len_truncation_marker",
                 t5_err.what() != NULL &&
                     strstr(t5_err.what(), "truncated") != NULL);

        t5_after_rc = t5_len_sink.info("after-overflow", &t5_err);
        t5_check("t5_len_after_overflow",
                 t5_after_rc == STDLIB_OK && t5_err.ok());
    }
    t5_check("t5_len_lines_wellformed",
             t5_file_lines_wellformed(t5_path, &t5_lines, &t5_maxlen) &&
                 t5_lines == 4 && t5_maxlen < LOG_LINE_SIZE);
    t5_check("t5_len_has_255_256",
             t5_file_contains(t5_path, "M255-") &&
                 t5_file_contains(t5_path, "M256-"));
    t5_check("t5_len_no_oversized", !t5_file_contains(t5_path, "M4095-"));

    /* ---- 6. enable/disable parity for every sink method ----------------- */
    unlink(t5_path);
    {
        const char *t5_parity_tags[4] = {
            "t5_parity_write", "t5_parity_info", "t5_parity_warn",
            "t5_parity_failure"};
        int t5_kind;
        log_sink t5_parity_sink = log_open_file(t5_path, LOG_FATAL, &t5_err);
        t5_check("t5_parity_open", t5_parity_sink.valid());
        for (t5_kind = 0; t5_kind < 4; t5_kind++) {
            struct t5_call_outcome t5_disabled;
            struct t5_call_outcome t5_enabled;
            t5_parity_sink.set_level(LOG_FATAL); /* all four severities off */
            t5_disabled = t5_invoke(&t5_parity_sink, t5_kind, "parity", &t5_err);
            t5_parity_sink.set_level(LOG_TRACE); /* all four severities on */
            t5_enabled = t5_invoke(&t5_parity_sink, t5_kind, "parity", &t5_err);
            t5_check(t5_parity_tags[t5_kind],
                     t5_disabled.ret == t5_enabled.ret &&
                         t5_disabled.ret == STDLIB_OK &&
                         t5_disabled.err_ok && t5_enabled.err_ok &&
                         t5_disabled.code == t5_enabled.code &&
                         t5_disabled.code == CPLUS_EXIT_OK &&
                         t5_disabled.cat == t5_enabled.cat &&
                         t5_disabled.cat == STDLIB_CATEGORY_OK &&
                         t5_disabled.libc_errno == t5_enabled.libc_errno &&
                         t5_disabled.libc_errno == 0);
        }
    }
    t5_check("t5_parity_lines",
             t5_file_lines_wellformed(t5_path, &t5_lines, &t5_maxlen) &&
                 t5_lines == 4);

    /* ---- 7. repeated open/write/drop cycles: no fd leak ----------------- */
    unlink(t5_path);
    {
        int t5_i;
        int t5_fd_before = t5_count_fds();
        for (t5_i = 0; t5_i < 32; t5_i++) {
            log_sink t5_cycle_sink = log_open_file(t5_path, LOG_INFO, &t5_err);
            if (!t5_cycle_sink.valid())
                break;
            if (t5_cycle_sink.info("cycle-line", &t5_err) != STDLIB_OK)
                break;
            t5_cycle_sink.drop();
        }
        t5_check("t5_cycles_complete", t5_i == 32);
        t5_check("t5_cycles_fd_stable", t5_count_fds() == t5_fd_before);
    }
    t5_check("t5_cycles_lines",
             t5_file_lines_wellformed(t5_path, &t5_lines, &t5_maxlen) &&
                 t5_lines == 32);

    /* ---- cleanup proof -------------------------------------------------- */
    unlink(t5_path);
    t5_check("t5_cleanup_no_leftovers", t5_path_absent(t5_path));

    print("LOG_TASK5", t5_checks, t5_failures);
    return t5_failures == 0 ? 0 : 1;
}
