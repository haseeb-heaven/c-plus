/* method_tests/json_log_time/log_methods.cp
 *
 * One deterministic case per public function/method declared in stdlib/log.hp.
 * Each case CALLS the method and verifies its result, then prints:
 *   log <method> PASS|FAIL
 * The process exits nonzero if any case fails.  Log lines contain a wall-clock
 * timestamp, so verification checks fixed substrings (level, message, fields,
 * source location) rather than whole-line or time values.
 */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/log.hp"

static int failures = 0;

#define REPORT(method, cond)                                                   \
    do {                                                                       \
        int _ok = (cond) ? 1 : 0;                                              \
        print("log", method, _ok ? "PASS" : "FAIL");                           \
        if (!_ok)                                                              \
            failures++;                                                        \
    } while (0)

/* Reads a log file and reports whether a fixed substring occurs on any line. */
static int jlt_contains(const char *path, const char *needle) {
    FILE *jlt_stream = fopen(path, "r");
    char jlt_line[LOG_LINE_SIZE];
    int jlt_found = 0;
    if (jlt_stream != NULL) {
        while (fgets(jlt_line, sizeof(jlt_line), jlt_stream) != NULL) {
            if (strstr(jlt_line, needle) != NULL)
                jlt_found = 1;
        }
        fclose(jlt_stream);
    }
    return jlt_found;
}

static const char *PATH_WRITE = "/tmp/cplus_jlt_log_write.log";
static const char *PATH_INFO = "/tmp/cplus_jlt_log_info.log";
static const char *PATH_WARN = "/tmp/cplus_jlt_log_warn.log";
static const char *PATH_ERROR = "/tmp/cplus_jlt_log_error.log";
static const char *PATH_DROP = "/tmp/cplus_jlt_log_drop.log";
static const char *PATH_OPEN = "/tmp/cplus_jlt_log_open.log";

int main(void) {
    error err = error();

    /* log_level_name: all six fixed names plus the unknown fallback. */
    REPORT("log_level_name",
           strcmp(log_level_name(LOG_TRACE), "TRACE") == 0 &&
               strcmp(log_level_name(LOG_DEBUG), "DEBUG") == 0 &&
               strcmp(log_level_name(LOG_INFO), "INFO") == 0 &&
               strcmp(log_level_name(LOG_WARN), "WARN") == 0 &&
               strcmp(log_level_name(LOG_ERROR), "ERROR") == 0 &&
               strcmp(log_level_name(LOG_FATAL), "FATAL") == 0 &&
               strcmp(log_level_name(99), "UNKNOWN") == 0);

    /* log_level_parse: case-insensitive names, bad name, NULL. */
    {
        int lp_trace = log_level_parse("trace", &err);
        int lp_fatal = log_level_parse("FATAL", &err);
        int lp_bad = log_level_parse("nonsense", &err);
        int lp_bad_err = err.failed_now() &&
                         err.category() == STDLIB_CATEGORY_INVALID;
        int lp_null = log_level_parse((const char *)0, &err);
        REPORT("log_level_parse", lp_trace == LOG_TRACE && lp_fatal == LOG_FATAL &&
                                      lp_bad == -1 && lp_bad_err &&
                                      lp_null == -1 && err.failed_now());
    }

    /* log_open_file: owned sink valid + threshold; bad path reports IO. */
    {
        unlink(PATH_OPEN);
        log_sink op_sink = log_open_file(PATH_OPEN, LOG_WARN, &err);
        int op_ok = op_sink.valid() && err.ok() && op_sink.level() == LOG_WARN;
        log_sink op_bad = log_open_file("/no/such/dir/cplus_jlt.log", LOG_INFO, &err);
        op_ok = op_ok && !op_bad.valid() && err.category() == STDLIB_CATEGORY_IO;
        REPORT("log_open_file", op_ok);
        op_sink.drop();
        op_bad.drop();
        unlink(PATH_OPEN);
    }

    /* log_stderr: borrowed sink borrows stderr and never closes it. */
    {
        int se_fd = fileno(stderr);
        log_sink se_sink = log_stderr(LOG_WARN);
        int se_ok = se_sink.valid() && se_sink.native() == stderr &&
                    se_sink.level() == LOG_WARN && se_sink.enabled(LOG_ERROR) &&
                    !se_sink.enabled(LOG_INFO);
        se_sink.drop();
        se_ok = se_ok && fcntl(se_fd, F_GETFD) != -1;
        REPORT("log_stderr", se_ok);
    }

    /* log_stdout: borrowed sink borrows stdout and never closes it. */
    {
        int so_fd = fileno(stdout);
        log_sink so_sink = log_stdout(LOG_FATAL);
        int so_ok = so_sink.valid() && so_sink.native() == stdout &&
                    so_sink.level() == LOG_FATAL && so_sink.enabled(LOG_FATAL) &&
                    !so_sink.enabled(LOG_ERROR);
        so_sink.drop();
        so_ok = so_ok && fcntl(so_fd, F_GETFD) != -1;
        REPORT("log_stdout", so_ok);
    }

    /* log_sink::log_sink: NULL stream invalid, real stream valid. */
    {
        log_sink ctor_null = log_sink((FILE *)0, 0, LOG_INFO);
        log_sink ctor_real = log_sink(stderr, 0, LOG_WARN);
        REPORT("log_sink::log_sink", !ctor_null.valid() && ctor_real.valid() &&
                                         ctor_real.native() == stderr);
        ctor_null.drop();
        ctor_real.drop();
    }

    /* log_sink::valid: true with a stream, false without one. */
    {
        unlink(PATH_DROP);
        log_sink va_sink = log_open_file(PATH_DROP, LOG_INFO, &err);
        log_sink va_null = log_sink((FILE *)0, 0, LOG_INFO);
        REPORT("log_sink::valid", va_sink.valid() && !va_null.valid());
        va_sink.drop();
        unlink(PATH_DROP);
    }

    /* log_sink::native: returns the exact stream handed to the constructor. */
    {
        log_sink na_sink = log_sink(stdout, 0, LOG_INFO);
        REPORT("log_sink::native", na_sink.native() == stdout);
        na_sink.drop();
    }

    /* log_sink::level: reports the threshold passed at construction. */
    {
        log_sink lv_sink = log_sink(stderr, 0, LOG_DEBUG);
        REPORT("log_sink::level", lv_sink.level() == LOG_DEBUG);
        lv_sink.drop();
    }

    /* log_sink::set_level: changes threshold and filtering behaviour. */
    {
        log_sink sl_sink = log_sink(stderr, 0, LOG_INFO);
        int sl_before = sl_sink.enabled(LOG_DEBUG);
        sl_sink.set_level(LOG_DEBUG);
        int sl_after = sl_sink.enabled(LOG_DEBUG);
        sl_sink.set_level(LOG_FATAL);
        REPORT("log_sink::set_level", sl_before == 0 && sl_after == 1 &&
                                          sl_sink.level() == LOG_FATAL &&
                                          !sl_sink.enabled(LOG_ERROR));
        sl_sink.drop();
    }

    /* log_sink::enabled: severity >= threshold on a valid sink only. */
    {
        log_sink en_sink = log_sink(stderr, 0, LOG_WARN);
        log_sink en_null = log_sink((FILE *)0, 0, LOG_WARN);
        REPORT("log_sink::enabled", en_sink.enabled(LOG_WARN) &&
                                        en_sink.enabled(LOG_FATAL) &&
                                        !en_sink.enabled(LOG_INFO) &&
                                        !en_null.enabled(LOG_FATAL));
        en_sink.drop();
        en_null.drop();
    }

    /* log_sink::write: fields + location written, low severity filtered. */
    {
        const char *wr_keys[2];
        long long wr_values[2];
        int wr_visible;
        int wr_hidden;
        unlink(PATH_WRITE);
        log_sink wr_sink = log_open_file(PATH_WRITE, LOG_INFO, &err);
        wr_keys[0] = "user";
        wr_values[0] = 42;
        wr_keys[1] = "attempt";
        wr_values[1] = 3;
        wr_visible = wr_sink.write(LOG_INFO, "visible-message", wr_keys, wr_values,
                                   2, "t.c", 7, &err);
        wr_hidden = wr_sink.write(LOG_TRACE, "hidden-message", (const char *const *)0,
                                   (const long long *)0, 0, (const char *)0, 0, &err);
        wr_sink.drop();
        REPORT("log_sink::write",
               wr_visible == STDLIB_OK && wr_hidden == STDLIB_OK && err.ok() &&
                   jlt_contains(PATH_WRITE, "INFO") &&
                   jlt_contains(PATH_WRITE, "visible-message") &&
                   jlt_contains(PATH_WRITE, "user=42") &&
                   jlt_contains(PATH_WRITE, "attempt=3") &&
                   jlt_contains(PATH_WRITE, "t.c:7") &&
                   !jlt_contains(PATH_WRITE, "hidden-message"));
        unlink(PATH_WRITE);
    }

    /* log_sink::info: writes an INFO line with the message. */
    {
        int in_rc;
        unlink(PATH_INFO);
        log_sink in_sink = log_open_file(PATH_INFO, LOG_TRACE, &err);
        in_rc = in_sink.info("INFO_MARK_1", &err);
        in_sink.drop();
        REPORT("log_sink::info",
               in_rc == STDLIB_OK && err.ok() &&
                   jlt_contains(PATH_INFO, "INFO") &&
                   jlt_contains(PATH_INFO, "INFO_MARK_1"));
        unlink(PATH_INFO);
    }

    /* log_sink::warn: writes a WARN line with the message. */
    {
        int wn_rc;
        unlink(PATH_WARN);
        log_sink wn_sink = log_open_file(PATH_WARN, LOG_TRACE, &err);
        wn_rc = wn_sink.warn("WARN_MARK_2", &err);
        wn_sink.drop();
        REPORT("log_sink::warn",
               wn_rc == STDLIB_OK && err.ok() &&
                   jlt_contains(PATH_WARN, "WARN") &&
                   jlt_contains(PATH_WARN, "WARN_MARK_2"));
        unlink(PATH_WARN);
    }

    /* log_sink::failure: writes an ERROR line with the message. */
    {
        int fl_rc;
        unlink(PATH_ERROR);
        log_sink fl_sink = log_open_file(PATH_ERROR, LOG_TRACE, &err);
        fl_rc = fl_sink.failure("ERROR_MARK_3", &err);
        fl_sink.drop();
        REPORT("log_sink::failure",
               fl_rc == STDLIB_OK && err.ok() &&
                   jlt_contains(PATH_ERROR, "ERROR") &&
                   jlt_contains(PATH_ERROR, "ERROR_MARK_3"));
        unlink(PATH_ERROR);
    }

    /* log_sink::drop: closes owned file, is idempotent, leaves borrowed open. */
    {
        int dr_fd = fileno(stderr);
        int dr_rc;
        unlink(PATH_DROP);
        log_sink dr_sink = log_open_file(PATH_DROP, LOG_INFO, &err);
        dr_rc = dr_sink.info("drop-check", &err);
        dr_sink.drop();
        int dr_invalid = !dr_sink.valid();
        dr_sink.drop();
        log_sink dr_borrowed = log_sink(stderr, 0, LOG_INFO);
        dr_borrowed.drop();
        REPORT("log_sink::drop", dr_rc == STDLIB_OK && dr_invalid &&
                                     !dr_sink.valid() &&
                                     jlt_contains(PATH_DROP, "drop-check") &&
                                     fcntl(dr_fd, F_GETFD) != -1);
        unlink(PATH_DROP);
    }

    print("RESULT", "failures", failures);
    return failures != 0;
}
