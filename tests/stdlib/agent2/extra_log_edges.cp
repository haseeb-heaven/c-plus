/* Additional, genuinely uncovered edge paths for stdlib/log.hp.
 *
 * Scope (only cases beyond the checked-in log_test.cp / deep_log_task5.cp
 * coverage, plus the edges requested for this task):
 *   1  NULL field arrays with field_count > 0 (keys NULL, values NULL, both)
 *      -> safe, fail-closed STDLIB_FAIL/INVALID, validation BEFORE filtering;
 *      field_count == 0 with NULL arrays is a safe no-op success;
 *   2  NULL key entries ("?" substitution) and LLONG_MIN/LLONG_MAX values;
 *   3  very long field names / field lists / file names -> BOUNDS failure at
 *      each of the three snprintf stages, no partial line flushed;
 *   4  empty and NULL messages; 255/256/483 byte messages accepted, 484 and
 *      4095 byte messages rejected with the "line truncated" marker;
 *   5  invalid log levels (parse, name, and writes with severity 99 / -1);
 *   6  1000 writes to ONE owned file sink with no fd leak and exact line count;
 *   7  log_stdout()/log_stderr() creation, borrowed-sink safe drop, and the
 *      process streams remaining open (no output is written to them so stdout
 *      and stderr stay deterministic);
 *   8  enable/disable parity of return value, error state and libc errno.
 *
 * Every temp file is PID-unique, removed before use and asserted absent after
 * use. Every check prints "<tag> <0|1>". The final line is
 * "EXTRA_LOG_EDGES <checks> <failures>". Exit is 0 only on zero failures.
 */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/log.hp"

static int ex_checks = 0;
static int ex_failures = 0;

static void ex_check(const char *tag, int cond) {
    ex_checks++;
    if (!cond)
        ex_failures++;
    print(tag, cond ? 1 : 0);
}

static int ex_path_absent(const char *path) {
    FILE *probe = fopen(path, "r");
    if (probe == NULL)
        return 1;
    fclose(probe);
    return 0;
}

static int ex_count_fds(void) {
    int count = 0;
    int fd;
    for (fd = 0; fd < 256; fd++) {
        if (fcntl(fd, F_GETFD) != -1)
            count++;
    }
    return count;
}

/* Count lines; reject any line that is not newline-terminated or that reaches
 * LOG_LINE_SIZE (a truncated line must never be flushed). */
static int ex_file_lines(const char *path, int *out_lines, int *out_maxlen) {
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

static int ex_file_contains(const char *path, const char *needle) {
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

struct ex_call {
    int ret;
    int err_ok;
    int cat;
    int code;
    int libc_errno;
};

/* Invoke one sink method with errno zeroed immediately before the call. */
static struct ex_call ex_invoke(log_sink *sink, int kind, const char *msg,
                                error *err) {
    struct ex_call out;
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
    out.cat = err->category();
    out.code = err->code();
    out.libc_errno = errno;
    return out;
}

int main(void) {
    error ex_err = error();
    char ex_path[128];
    char ex_msg[4096];
    char ex_longkey[700];
    const char *ex_keys[3];
    long long ex_vals[3];
    const char *ex_many_keys[128];
    long long ex_many_vals[128];
    int ex_lines = 0;
    int ex_maxlen = 0;
    int ex_i = 0;
    int ex_ok = 1;
    int ex_rc = 0;
    int ex_fd_base = 0;
    int ex_fd_open = 0;
    int ex_out_fd = 0;
    int ex_err_fd = 0;
    struct ex_call ex_disabled;
    struct ex_call ex_enabled;

    snprintf(ex_path, sizeof(ex_path), "/tmp/cplus_extra_log_edges_%d.log",
             (int)getpid());
    unlink(ex_path);

    /* ---- 1. NULL field arrays with field_count > 0 ----------------------- */
    unlink(ex_path);
    {
        log_sink ex_nullarr_sink = log_open_file(ex_path, LOG_FATAL, &ex_err);
        ex_check("ex_nullarr_open", ex_nullarr_sink.valid());
        ex_keys[0] = "k";
        ex_vals[0] = 1;

        /* Validation happens before severity filtering: this INFO write is
         * below the FATAL threshold yet must still be rejected. */
        ex_rc = ex_nullarr_sink.write(LOG_INFO, "nullkeys", NULL, ex_vals, 1,
                                      NULL, 0, &ex_err);
        ex_check("ex_nullarr_keys_null",
                 ex_rc == STDLIB_FAIL && ex_err.failed_now() &&
                     ex_err.category() == STDLIB_CATEGORY_INVALID &&
                     ex_err.code() == CPLUS_EXIT_UNSUPPORTED &&
                     ex_err.what() != NULL &&
                     strstr(ex_err.what(), "null field arrays") != NULL);

        ex_nullarr_sink.set_level(LOG_TRACE);
        ex_rc = ex_nullarr_sink.write(LOG_INFO, "nullvals", ex_keys, NULL, 1,
                                      NULL, 0, &ex_err);
        ex_check("ex_nullarr_values_null",
                 ex_rc == STDLIB_FAIL && ex_err.failed_now() &&
                     ex_err.category() == STDLIB_CATEGORY_INVALID &&
                     ex_err.code() == CPLUS_EXIT_UNSUPPORTED);

        ex_rc = ex_nullarr_sink.write(LOG_INFO, "bothnull", NULL, NULL, 2,
                                      NULL, 0, &ex_err);
        ex_check("ex_nullarr_both_null",
                 ex_rc == STDLIB_FAIL && ex_err.failed_now() &&
                     ex_err.category() == STDLIB_CATEGORY_INVALID);

        /* field_count == 0 makes the arrays irrelevant and is safe. */
        ex_rc = ex_nullarr_sink.write(LOG_INFO, "zero-null-arrays", NULL,
                                      NULL, 0, NULL, 0, &ex_err);
        ex_check("ex_nullarr_zero_fields_null_arrays",
                 ex_rc == STDLIB_OK && ex_err.ok());
        ex_rc = ex_nullarr_sink.write(LOG_INFO, "zero-live-arrays", ex_keys,
                                      ex_vals, 0, NULL, 0, &ex_err);
        ex_check("ex_nullarr_zero_fields_live_arrays",
                 ex_rc == STDLIB_OK && ex_err.ok());
        ex_nullarr_sink.drop();
        ex_check("ex_nullarr_drop", !ex_nullarr_sink.valid() &&
                                        ex_nullarr_sink.native() == NULL);
    }
    ex_file_lines(ex_path, &ex_lines, &ex_maxlen);
    ex_check("ex_nullarr_only_successes_flushed", ex_lines == 2);
    ex_check("ex_nullarr_success_content",
             ex_file_contains(ex_path, "zero-null-arrays") &&
                 ex_file_contains(ex_path, "zero-live-arrays"));
    ex_check("ex_nullarr_failed_content_absent",
             !ex_file_contains(ex_path, "nullkeys") &&
                 !ex_file_contains(ex_path, "nullvals") &&
                 !ex_file_contains(ex_path, "bothnull"));

    /* ---- 2. NULL key entries and extreme values -------------------------- */
    unlink(ex_path);
    {
        log_sink ex_field_sink = log_open_file(ex_path, LOG_TRACE, &ex_err);
        ex_check("ex_field_open", ex_field_sink.valid());
        ex_keys[0] = NULL;
        ex_vals[0] = 42;
        ex_keys[1] = "neg";
        ex_vals[1] = -7;
        ex_keys[2] = NULL;
        ex_vals[2] = LLONG_MIN;
        ex_rc = ex_field_sink.write(LOG_INFO, "null-key", ex_keys, ex_vals, 3,
                                    "fk.c", 11, &ex_err);
        ex_check("ex_field_null_key_write", ex_rc == STDLIB_OK && ex_err.ok());

        ex_keys[0] = "max";
        ex_vals[0] = LLONG_MAX;
        ex_keys[1] = "min";
        ex_vals[1] = LLONG_MIN;
        ex_rc = ex_field_sink.write(LOG_INFO, "extreme", ex_keys, ex_vals, 2,
                                    NULL, 0, &ex_err);
        ex_check("ex_field_extreme_write", ex_rc == STDLIB_OK && ex_err.ok());
        ex_field_sink.drop();
    }
    ex_check("ex_field_question_substitution",
             ex_file_contains(ex_path, "?=42") &&
                 ex_file_contains(ex_path, "?=-9223372036854775808"));
    ex_check("ex_field_no_null_literal",
             !ex_file_contains(ex_path, "(null)"));
    ex_check("ex_field_extreme_values",
             ex_file_contains(ex_path, "max=9223372036854775807") &&
                 ex_file_contains(ex_path, "min=-9223372036854775808"));
    ex_check("ex_field_location", ex_file_contains(ex_path, "(fk.c:11)"));
    ex_file_lines(ex_path, &ex_lines, &ex_maxlen);
    ex_check("ex_field_lines", ex_file_lines(ex_path, &ex_lines, &ex_maxlen) &&
                                   ex_lines == 2 &&
                                   ex_maxlen < LOG_LINE_SIZE);

    /* ---- 3. long field name / field list / file name truncation ---------- */
    unlink(ex_path);
    {
        log_sink ex_trunc_sink = log_open_file(ex_path, LOG_TRACE, &ex_err);
        ex_check("ex_trunc_open", ex_trunc_sink.valid());
        ex_rc = ex_trunc_sink.write(LOG_INFO, "before", NULL, NULL, 0, NULL, 0,
                                    &ex_err);
        ex_check("ex_trunc_before", ex_rc == STDLIB_OK && ex_err.ok());

        memset(ex_longkey, 'K', sizeof(ex_longkey) - 1);
        ex_longkey[sizeof(ex_longkey) - 1] = '\0';
        ex_keys[0] = ex_longkey;
        ex_vals[0] = 1;
        ex_rc = ex_trunc_sink.write(LOG_INFO, "long-key", ex_keys, ex_vals, 1,
                                    NULL, 0, &ex_err);
        ex_check("ex_trunc_long_key_rejected",
                 ex_rc == STDLIB_FAIL && ex_err.failed_now() &&
                     ex_err.category() == STDLIB_CATEGORY_BOUNDS &&
                     ex_err.code() == CPLUS_EXIT_BOUNDS &&
                     ex_err.what() != NULL &&
                     strstr(ex_err.what(), "truncated") != NULL);

        for (ex_i = 0; ex_i < 128; ex_i++) {
            ex_many_keys[ex_i] = "field";
            ex_many_vals[ex_i] = (long long)ex_i;
        }
        ex_rc = ex_trunc_sink.write(LOG_INFO, "many-fields", ex_many_keys,
                                    ex_many_vals, 128, NULL, 0, &ex_err);
        ex_check("ex_trunc_many_fields_rejected",
                 ex_rc == STDLIB_FAIL && ex_err.category() ==
                                             STDLIB_CATEGORY_BOUNDS);

        ex_rc = ex_trunc_sink.write(LOG_INFO, "long-file", NULL, NULL, 0,
                                    ex_longkey, 7, &ex_err);
        ex_check("ex_trunc_long_file_rejected",
                 ex_rc == STDLIB_FAIL && ex_err.category() ==
                                             STDLIB_CATEGORY_BOUNDS);

        ex_rc = ex_trunc_sink.write(LOG_INFO, "after", NULL, NULL, 0, NULL, 0,
                                    &ex_err);
        ex_check("ex_trunc_after_recovery", ex_rc == STDLIB_OK && ex_err.ok());
        ex_trunc_sink.drop();
    }
    ex_file_lines(ex_path, &ex_lines, &ex_maxlen);
    ex_check("ex_trunc_no_partial_lines",
             ex_lines == 2 && ex_maxlen < LOG_LINE_SIZE);
    ex_check("ex_trunc_content",
             ex_file_contains(ex_path, "before") &&
                 ex_file_contains(ex_path, "after") &&
                 !ex_file_contains(ex_path, "long-key") &&
                 !ex_file_contains(ex_path, "many-fields") &&
                 !ex_file_contains(ex_path, "long-file"));

    /* ---- 4. empty / NULL / boundary-length messages ---------------------- */
    unlink(ex_path);
    {
        log_sink ex_len_sink = log_open_file(ex_path, LOG_TRACE, &ex_err);
        ex_check("ex_len_open", ex_len_sink.valid());

        ex_rc = ex_len_sink.write(LOG_INFO, "", NULL, NULL, 0, NULL, 0,
                                  &ex_err);
        ex_check("ex_len_empty_string", ex_rc == STDLIB_OK && ex_err.ok());
        ex_rc = ex_len_sink.write(LOG_INFO, NULL, NULL, NULL, 0, NULL, 0,
                                  &ex_err);
        ex_check("ex_len_null_message", ex_rc == STDLIB_OK && ex_err.ok());

        memset(ex_msg, 'A', sizeof(ex_msg));
        memcpy(ex_msg, "M255-", 5);
        ex_msg[255] = '\0';
        ex_rc = ex_len_sink.write(LOG_INFO, ex_msg, NULL, NULL, 0, NULL, 0,
                                  &ex_err);
        ex_check("ex_len_255", ex_rc == STDLIB_OK && ex_err.ok());

        memset(ex_msg, 'B', sizeof(ex_msg));
        memcpy(ex_msg, "M256-", 5);
        ex_msg[256] = '\0';
        ex_rc = ex_len_sink.write(LOG_INFO, ex_msg, NULL, NULL, 0, NULL, 0,
                                  &ex_err);
        ex_check("ex_len_256", ex_rc == STDLIB_OK && ex_err.ok());

        /* Prefix is 20-byte stamp + ' ' + %-5s + ' ' = 27 bytes; with the
         * trailing '\n' and NUL the last accepted length is 483 bytes. */
        memset(ex_msg, 'D', sizeof(ex_msg));
        memcpy(ex_msg, "M483-", 5);
        ex_msg[483] = '\0';
        ex_rc = ex_len_sink.write(LOG_INFO, ex_msg, NULL, NULL, 0, NULL, 0,
                                  &ex_err);
        ex_check("ex_len_483_accepted", ex_rc == STDLIB_OK && ex_err.ok());

        memset(ex_msg, 'E', sizeof(ex_msg));
        memcpy(ex_msg, "M484-", 5);
        ex_msg[484] = '\0';
        ex_rc = ex_len_sink.write(LOG_INFO, ex_msg, NULL, NULL, 0, NULL, 0,
                                  &ex_err);
        ex_check("ex_len_484_rejected",
                 ex_rc == STDLIB_FAIL && ex_err.category() ==
                                             STDLIB_CATEGORY_BOUNDS);

        memset(ex_msg, 'C', sizeof(ex_msg));
        memcpy(ex_msg, "M4095-", 6);
        ex_msg[4095] = '\0';
        ex_rc = ex_len_sink.write(LOG_INFO, ex_msg, NULL, NULL, 0, NULL, 0,
                                  &ex_err);
        ex_check("ex_len_4095_rejected", ex_rc == STDLIB_FAIL);
        ex_check("ex_len_truncation_marker",
                 ex_err.failed_now() &&
                     ex_err.category() == STDLIB_CATEGORY_BOUNDS &&
                     ex_err.code() == CPLUS_EXIT_BOUNDS &&
                     ex_err.what() != NULL &&
                     strstr(ex_err.what(), "truncated") != NULL);

        ex_rc = ex_len_sink.info("after-oversize", &ex_err);
        ex_check("ex_len_post_overflow_write",
                 ex_rc == STDLIB_OK && ex_err.ok());
        ex_len_sink.drop();
    }
    ex_file_lines(ex_path, &ex_lines, &ex_maxlen);
    ex_check("ex_len_lines_wellformed",
             ex_lines == 6 && ex_maxlen < LOG_LINE_SIZE);
    ex_check("ex_len_small_present",
             ex_file_contains(ex_path, "M255-") &&
                 ex_file_contains(ex_path, "M256-") &&
                 ex_file_contains(ex_path, "M483-"));
    ex_check("ex_len_oversize_absent",
             !ex_file_contains(ex_path, "M484-") &&
                 !ex_file_contains(ex_path, "M4095-") &&
                 ex_file_contains(ex_path, "after-oversize"));

    /* ---- 5. invalid log level values ------------------------------------- */
    ex_rc = log_level_parse("nonsense", &ex_err);
    ex_check("ex_level_parse_invalid",
             ex_rc == -1 && ex_err.failed_now() &&
                 ex_err.category() == STDLIB_CATEGORY_INVALID &&
                 ex_err.code() == CPLUS_EXIT_UNSUPPORTED);
    ex_rc = log_level_parse(NULL, &ex_err);
    ex_check("ex_level_parse_null",
             ex_rc == -1 && ex_err.category() == STDLIB_CATEGORY_INVALID);
    ex_check("ex_level_name_invalid",
             strcmp(log_level_name(99), "UNKNOWN") == 0 &&
                 strcmp(log_level_name(-1), "UNKNOWN") == 0 &&
                 strcmp(log_level_name(LOG_LEVELS), "UNKNOWN") == 0);

    unlink(ex_path);
    {
        log_sink ex_lvl_sink = log_open_file(ex_path, LOG_TRACE, &ex_err);
        ex_check("ex_level_sink_open", ex_lvl_sink.valid());
        ex_rc = ex_lvl_sink.write(99, "invalid-severity", NULL, NULL, 0, NULL,
                                  0, &ex_err);
        ex_check("ex_level_write_99", ex_rc == STDLIB_OK && ex_err.ok());
        ex_rc = ex_lvl_sink.write(-1, "negative-severity", NULL, NULL, 0, NULL,
                                  0, &ex_err);
        ex_check("ex_level_write_negative", ex_rc == STDLIB_OK && ex_err.ok());
        ex_lvl_sink.drop();
    }
    ex_check("ex_level_unknown_in_file",
             ex_file_contains(ex_path, "UNKNOWN") &&
                 ex_file_contains(ex_path, "invalid-severity"));
    ex_check("ex_level_negative_filtered",
             !ex_file_contains(ex_path, "negative-severity"));
    ex_file_lines(ex_path, &ex_lines, &ex_maxlen);
    ex_check("ex_level_lines", ex_lines == 1);

    /* ---- 6. 1000 writes to one file sink, no fd leak --------------------- */
    unlink(ex_path);
    ex_fd_base = ex_count_fds();
    {
        log_sink ex_bulk_sink = log_open_file(ex_path, LOG_INFO, &ex_err);
        ex_check("ex_bulk_open", ex_bulk_sink.valid());
        ex_fd_open = ex_count_fds();
        ex_check("ex_bulk_one_fd", ex_fd_open == ex_fd_base + 1);
        ex_ok = 1;
        for (ex_i = 0; ex_i < 1000; ex_i++) {
            if (ex_bulk_sink.info("bulk", &ex_err) != STDLIB_OK || !ex_err.ok())
                ex_ok = 0;
        }
        ex_check("ex_bulk_1000_writes", ex_ok);
        ex_check("ex_bulk_no_write_fd_leak", ex_count_fds() == ex_fd_open);
        ex_bulk_sink.drop();
        ex_check("ex_bulk_drop", !ex_bulk_sink.valid() &&
                                     ex_bulk_sink.native() == NULL);
    }
    ex_check("ex_bulk_fd_released", ex_count_fds() == ex_fd_base);
    ex_file_lines(ex_path, &ex_lines, &ex_maxlen);
    ex_check("ex_bulk_exact_line_count",
             ex_lines == 1000 && ex_maxlen < LOG_LINE_SIZE);

    /* ---- 7. stdout/stderr borrowed sinks: creation and safe drop --------- */
    ex_out_fd = fileno(stdout);
    ex_err_fd = fileno(stderr);
    ex_fd_base = ex_count_fds();
    {
        log_sink ex_out_sink = log_stdout(LOG_WARN);
        ex_check("ex_stdout_valid",
                 ex_out_sink.valid() && ex_out_sink.native() == stdout &&
                     ex_out_sink.level() == LOG_WARN);
        ex_check("ex_stdout_enabled",
                 ex_out_sink.enabled(LOG_WARN) &&
                     ex_out_sink.enabled(LOG_FATAL) &&
                     !ex_out_sink.enabled(LOG_INFO));
        ex_out_sink.drop();
        ex_out_sink.drop();
        ex_check("ex_stdout_drop", !ex_out_sink.valid() &&
                                       ex_out_sink.native() == NULL);
        ex_rc = ex_out_sink.info("no-output", &ex_err);
        ex_check("ex_stdout_post_drop_write",
                 ex_rc == STDLIB_FAIL &&
                     ex_err.category() == STDLIB_CATEGORY_INVALID);
    }
    ex_check("ex_stdout_stream_open", fcntl(ex_out_fd, F_GETFD) != -1);
    {
        log_sink ex_err_sink = log_stderr(LOG_ERROR);
        ex_check("ex_stderr_valid",
                 ex_err_sink.valid() && ex_err_sink.native() == stderr &&
                     ex_err_sink.level() == LOG_ERROR);
        ex_check("ex_stderr_enabled",
                 ex_err_sink.enabled(LOG_ERROR) &&
                     !ex_err_sink.enabled(LOG_WARN));
        ex_err_sink.drop();
        ex_err_sink.drop();
        ex_check("ex_stderr_drop", !ex_err_sink.valid() &&
                                       ex_err_sink.native() == NULL);
    }
    ex_check("ex_stderr_stream_open", fcntl(ex_err_fd, F_GETFD) != -1);
    ex_check("ex_borrowed_fd_stable", ex_count_fds() == ex_fd_base);

    /* ---- 8. enable/disable parity of return value and errno -------------- */
    unlink(ex_path);
    {
        const char *ex_parity_tags[4] = {"ex_parity_write", "ex_parity_info",
                                         "ex_parity_warn",
                                         "ex_parity_failure"};
        log_sink ex_parity_sink = log_open_file(ex_path, LOG_FATAL, &ex_err);
        ex_check("ex_parity_open", ex_parity_sink.valid());
        for (ex_i = 0; ex_i < 4; ex_i++) {
            ex_parity_sink.set_level(LOG_FATAL);
            ex_disabled = ex_invoke(&ex_parity_sink, ex_i, "parity", &ex_err);
            ex_parity_sink.set_level(LOG_TRACE);
            ex_enabled = ex_invoke(&ex_parity_sink, ex_i, "parity", &ex_err);
            ex_check(ex_parity_tags[ex_i],
                     ex_disabled.ret == ex_enabled.ret &&
                         ex_disabled.ret == STDLIB_OK &&
                         ex_disabled.err_ok && ex_enabled.err_ok &&
                         ex_disabled.cat == ex_enabled.cat &&
                         ex_disabled.code == ex_enabled.code &&
                         ex_disabled.libc_errno == ex_enabled.libc_errno &&
                         ex_disabled.libc_errno == 0);
        }
        ex_parity_sink.drop();
    }
    ex_file_lines(ex_path, &ex_lines, &ex_maxlen);
    ex_check("ex_parity_only_enabled_written", ex_lines == 4);

    /* ---- cleanup proof --------------------------------------------------- */
    unlink(ex_path);
    ex_check("ex_cleanup_absent", ex_path_absent(ex_path));

    print("EXTRA_LOG_EDGES", ex_checks, ex_failures);
    return ex_failures == 0 ? 0 : 1;
}
