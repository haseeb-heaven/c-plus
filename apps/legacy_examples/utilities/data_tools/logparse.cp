/* logparse.cp -- bounded log parser with level filtering and refusals.
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/legacy_examples/utilities/data_tools/logparse.cp \
 *     --strict --sanitize=none -I. \
 *     -o apps/legacy_examples/utilities/data_tools/.run/logparse
 *
 * Usage:
 *   logparse <log-path> TRACE|DEBUG|INFO|WARN|ERROR|FATAL|all
 *
 * Each log line is `TIMESTAMP LEVEL MESSAGE` where TIMESTAMP is the fixed
 * 20-byte form `YYYY-MM-DDTHH:MM:SSZ`, LEVEL is one of the six known tokens
 * and MESSAGE is a non-empty remainder. Reads use the checked file::read_line
 * API. Over-long lines, malformed lines, bad timestamps and unknown levels
 * are ALL refused with a stable token. Output is deterministic: fixed argv,
 * fixed file bytes, no clock, no files written, no shell.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "stdlib/file.hp"

#define LP_LINE  256
#define LP_PATH  512
#define LP_LEVEL  8
#define LP_TS    20
#define LP_MAX   256

static char *lp_arg(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

static void lp_refuse(error *err, const char *code, cplus_exit_code status) {
    error_fail(err, status, code);
    print("LOG error", code);
}

static int lp_copy(const char *src, char *dst, size_t cap, size_t *out_len) {
    string s = string(src);
    size_t n;
    int ok = 0;
    if (s.valid()) {
        n = s.size();
        if (n > 0 && n + 1 <= cap) {
            if (s.substr(0, n, dst, cap) == 0) {
                *out_len = n;
                ok = 1;
            }
        }
    }
    s.drop();
    return ok;
}

static int lp_digits(const char *s, size_t from, size_t count) {
    size_t i;
    for (i = 0; i < count; i++) {
        if (s[from + i] < '0' || s[from + i] > '9')
            return 0;
    }
    return 1;
}

/* Validate the fixed 20-byte UTC timestamp layout. */
static int lp_valid_ts(const char *ts) {
    if (strlen(ts) != LP_TS)
        return 0;
    if (!lp_digits(ts, 0, 4) || ts[4] != '-' ||
        !lp_digits(ts, 5, 2) || ts[7] != '-' ||
        !lp_digits(ts, 8, 2) || ts[10] != 'T' ||
        !lp_digits(ts, 11, 2) || ts[13] != ':' ||
        !lp_digits(ts, 14, 2) || ts[16] != ':' ||
        !lp_digits(ts, 17, 2) || ts[19] != 'Z')
        return 0;
    return 1;
}

static int lp_known_level(const char *level) {
    return strcmp(level, "TRACE") == 0 || strcmp(level, "DEBUG") == 0 ||
           strcmp(level, "INFO") == 0  || strcmp(level, "WARN") == 0 ||
           strcmp(level, "ERROR") == 0 || strcmp(level, "FATAL") == 0;
}

int main(int argc, char **argv) {
    error err = error();
    file stream;
    char path[LP_PATH];
    char filter[LP_LEVEL];
    char line[LP_LINE];
    char ts[LP_TS + 1];
    char level[LP_LEVEL];
    size_t path_len = 0;
    size_t filter_len = 0;
    size_t n;
    size_t i;
    size_t sp1;
    size_t sp2;
    size_t shown = 0;
    size_t total = 0;
    int want_all;
    int rc;

    if (argc != 3) {
        lp_refuse(&err, "usage", CPLUS_EXIT_UNSUPPORTED);
        return 2;
    }
    if (!lp_copy(lp_arg(argv, 1), path, sizeof(path), &path_len) ||
        !lp_copy(lp_arg(argv, 2), filter, sizeof(filter), &filter_len)) {
        lp_refuse(&err, "bad-arg", CPLUS_EXIT_BOUNDS);
        return 1;
    }
    want_all = strcmp(filter, "all") == 0;
    if (!want_all && !lp_known_level(filter)) {
        lp_refuse(&err, "bad-level", CPLUS_EXIT_UNSUPPORTED);
        return 1;
    }

    stream = file(path, "r");
    if (!stream.is_open()) {
        lp_refuse(&err, "cannot-open", CPLUS_EXIT_IO);
        return 1;
    }

    for (;;) {
        rc = stream.read_line(line, sizeof(line), &err);
        if (rc == STDLIB_EOF)
            break;
        if (rc != STDLIB_OK) {
            const char *why = err.code() == (int)CPLUS_EXIT_BOUNDS
                                  ? "line-too-long" : "read-error";
            stream.drop();
            lp_refuse(&err, why, CPLUS_EXIT_BOUNDS);
            return 1;
        }
        n = strlen(line);
        if (n == 0) {
            stream.drop();
            lp_refuse(&err, "malformed", CPLUS_EXIT_UNSUPPORTED);
            return 1;
        }

        /* First space terminates the 20-byte timestamp. */
        sp1 = 0;
        for (i = 0; i < n; i++) {
            if (line[i] == ' ')
                break;
            sp1 = i + 1;
        }
        if (sp1 != LP_TS) {
            stream.drop();
            lp_refuse(&err, "malformed", CPLUS_EXIT_UNSUPPORTED);
            return 1;
        }
        for (i = 0; i < LP_TS; i++)
            ts[i] = line[i];
        ts[LP_TS] = '\0';
        if (!lp_valid_ts(ts)) {
            stream.drop();
            lp_refuse(&err, "bad-timestamp", CPLUS_EXIT_UNSUPPORTED);
            return 1;
        }

        /* Level starts after the separator, then runs to the next space. */
        sp2 = sp1 + 1;
        while (sp2 < n && line[sp2] == ' ')
            sp2++;
        if (sp2 >= n) {
            stream.drop();
            lp_refuse(&err, "malformed", CPLUS_EXIT_UNSUPPORTED);
            return 1;
        }
        i = 0;
        while (sp2 + i < n && line[sp2 + i] != ' ') {
            if (i > LP_LEVEL - 1) {
                stream.drop();
                lp_refuse(&err, "malformed", CPLUS_EXIT_UNSUPPORTED);
                return 1;
            }
            level[i] = line[sp2 + i];
            i++;
        }
        level[i] = '\0';
        if (!lp_known_level(level)) {
            stream.drop();
            lp_refuse(&err, "bad-level", CPLUS_EXIT_UNSUPPORTED);
            return 1;
        }

        /* Message starts after the level and must be non-empty. */
        sp1 = sp2 + i;
        while (sp1 < n && line[sp1] == ' ')
            sp1++;
        if (sp1 >= n) {
            stream.drop();
            lp_refuse(&err, "malformed", CPLUS_EXIT_UNSUPPORTED);
            return 1;
        }

        total++;
        if (total > LP_MAX) {
            stream.drop();
            lp_refuse(&err, "too-many-lines", CPLUS_EXIT_BOUNDS);
            return 1;
        }
        if (want_all || strcmp(level, filter) == 0) {
            print("LOG match", shown, line);
            shown++;
        }
    }

    if (stream.close(&err) != STDLIB_OK) {
        stream.drop();
        lp_refuse(&err, "close-error", CPLUS_EXIT_IO);
        return 1;
    }
    stream.drop();

    print("LOG count", filter, (int)shown);
    return 0;
}
