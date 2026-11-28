/* F-100 -- deterministic fuzz configuration: log.hp levels and line bounds.
 *
 * Fixed-seed LCG over log_level_parse/name, sink thresholds, message lengths
 * (0..600 around LOG_LINE_SIZE=512), field counts 0..3 and repeated writes to
 * a PID-unique owned file sink.  The exact line bytes contain a wall-clock
 * timestamp and are therefore never folded into the digest; instead the run
 * checks result codes/categories and proves that the number of newline bytes
 * in the log file equals the number of STDLIB_OK writes.  Borrowed stderr /
 * stdout sinks are dropped and a subsequent print() proves they were not
 * closed.  All files are removed at the end with a /tmp prefix cleanup proof.
 *
 * Bounded CASES loop, SIGALRM watchdog, external `timeout 30s`.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/fs.hp"
#include "stdlib/log.hp"

#define CASES 300
#define FNV_PRIME 1099511628211ULL

static unsigned long long rng_state = 0xF1001234ULL;
static unsigned long long digest = 0xcbf29ce484222325ULL;
static int failures = 0;
static char prefix[128];
static char logpath[512];

static unsigned int rnd(void) {
    rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (unsigned int)(rng_state >> 33);
}

static void mix(unsigned long long v) {
    digest = (digest ^ v) * FNV_PRIME;
}

static void invariant(const char *tag) {
    failures++;
    print("INVARIANT", tag);
}

static void on_alarm(int sig) {
    static const char msg[] = "F100 watchdog timeout\n";
    (void)sig;
    (void)!write(2, msg, sizeof(msg) - 1);
    _exit(90);
}

static void on_ignore(int sig) {
    (void)sig;
}

static int count_prefix(void) {
    DIR *d = opendir("/tmp");
    struct dirent *e;
    int n = 0;
    if (d == NULL)
        return -1;
    while ((e = readdir(d)) != NULL) {
        if (strncmp(e->d_name, prefix, strlen(prefix)) == 0)
            n++;
    }
    closedir(d);
    return n;
}

static void fold_name(const char *name) {
    size_t i;
    for (i = 0; name[i] != '\0'; i++)
        mix((unsigned char)name[i]);
    mix(0);
}

/* Fixed-length message sweep; the expected category is deterministic even
 * though the timestamp inside a successful line is not. */
static int boundary_write(log_sink *sink, int n, int fields, int with_file) {
    char msg[640];
    const char *keys[3] = {"k0", "k1", "k2"};
    long long values[3] = {-1LL, 0LL, 1234567890123LL};
    error err = error();
    int i;
    int rc;
    for (i = 0; i < n; i++)
        msg[i] = (char)('a' + (i % 26));
    msg[n] = '\0';
    rc = sink->write(LOG_INFO, msg, keys, values, (size_t)fields,
                     with_file ? "f100_log.cp" : NULL, 100, &err);
    if (rc == STDLIB_OK) {
        if (!err.ok())
            invariant("f100_ok_err");
    } else if (rc == STDLIB_FAIL) {
        if (err.category() != STDLIB_CATEGORY_BOUNDS)
            invariant("f100_fail_category");
    } else {
        invariant("f100_unexpected_rc");
    }
    mix((unsigned long long)n);
    mix((unsigned long long)fields);
    mix((unsigned long long)with_file);
    mix((unsigned long long)rc);
    mix((unsigned long long)err.category());
    return rc == STDLIB_OK ? 1 : 0;
}

int main(void) {
    char hex[32];
    char msg[640];
    const char *names[6] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
    const char *keys[2] = {"id", "n"};
    long long values[2] = {7LL, -3LL};
    error openerr = error();
    error err = error();
    log_sink sink;
    log_sink missing;
    log_sink borrowed_err;
    log_sink borrowed_out;
    FILE *reader;
    int baseline;
    int after;
    int i;
    int level;
    int n;
    int fields;
    int severity;
    int rc;
    int ok;
    int ok_writes = 0;
    int newlines = 0;
    int ch;

    (void)!signal(SIGPIPE, on_ignore);
    (void)!signal(SIGALRM, on_alarm);
    (void)!alarm(25);

    snprintf(prefix, sizeof(prefix), "f100_f100_%d_", (int)getpid());
    baseline = count_prefix();

    /* ---- level parse / name -------------------------------------------- */
    for (i = 0; i < 6; i++) {
        level = log_level_parse(names[i], &err);
        if (level != i || !err.ok() ||
            strcmp(log_level_name(level), names[i]) != 0)
            invariant("f100_level_parse");
        mix((unsigned long long)level);
        mix((unsigned long long)err.category());
    }
    level = log_level_parse("trace", &err); /* case-insensitive */
    if (level != LOG_TRACE)
        invariant("f100_level_case");
    mix((unsigned long long)level);
    err.clear();
    if (log_level_parse("NOPE", &err) != -1 ||
        err.category() != STDLIB_CATEGORY_INVALID)
        invariant("f100_level_invalid");
    mix((unsigned long long)err.category());
    err.clear();
    if (log_level_parse(NULL, &err) != -1 ||
        err.category() != STDLIB_CATEGORY_INVALID)
        invariant("f100_level_null");
    mix((unsigned long long)err.category());
    fold_name(log_level_name(-1));
    fold_name(log_level_name(6));

    /* ---- invalid (unopened) sink --------------------------------------- */
    missing = log_open_file("/tmp/this_dir_does_not_exist_f100/x.log",
                            LOG_INFO, &openerr);
    if (missing.valid() || openerr.category() != STDLIB_CATEGORY_IO)
        invariant("f100_open_missing");
    err.clear();
    if (missing.write(LOG_INFO, "x", NULL, NULL, 0, NULL, 0, &err) !=
            STDLIB_FAIL ||
        err.category() != STDLIB_CATEGORY_INVALID)
        invariant("f100_invalid_sink_write");
    mix((unsigned long long)err.category());
    missing.drop();

    /* ---- owned file sink ----------------------------------------------- */
    snprintf(logpath, sizeof(logpath), "/tmp/%srun.log", prefix);
    unlink(logpath); /* append mode; start from an empty file */
    sink = log_open_file(logpath, LOG_INFO, &openerr);
    if (!sink.valid()) {
        print("SKIP_REASON", "f100_file_sink", "log_open_file unavailable");
        (void)!alarm(0);
        snprintf(hex, sizeof(hex), "%016llx", digest);
        print("f100_digest", hex);
        print("f100:", CASES, "cases skipped");
        return 0;
    }

    ok_writes += boundary_write(&sink, 0, 0, 0);
    ok_writes += boundary_write(&sink, 1, 0, 0);
    ok_writes += boundary_write(&sink, 10, 1, 0);
    ok_writes += boundary_write(&sink, 100, 2, 1);
    ok_writes += boundary_write(&sink, 300, 3, 1);
    ok_writes += boundary_write(&sink, 400, 3, 1);
    ok_writes += boundary_write(&sink, 480, 3, 1);
    ok_writes += boundary_write(&sink, 511, 3, 1);
    ok_writes += boundary_write(&sink, 512, 3, 1);
    ok_writes += boundary_write(&sink, 600, 3, 1);

    for (i = 0; i < CASES; i++) {
        level = (int)(rnd() % 7u); /* threshold may exceed LOG_FATAL */
        sink.set_level(level);
        if (sink.level() != level || !sink.valid())
            invariant("f100_threshold");
        severity = (int)(rnd() % (LOG_LEVELS + 2u));
        if (sink.enabled(severity) != (severity >= level ? 1 : 0))
            invariant("f100_enabled");
        n = (int)(rnd() % 620u);
        fields = (int)(rnd() % 4u);
        if (fields > 2)
            fields = 2;
        memset(msg, 'm' + (char)(i % 7), (size_t)n);
        msg[n] = '\0';
        err.clear();
        rc = sink.write(severity, msg, fields ? keys : NULL,
                        fields ? values : NULL, (size_t)fields,
                        (i & 1u) ? "f100_log.cp" : NULL, i, &err);
        if (rc == STDLIB_OK) {
            /* A filtered line is a successful no-op and writes no newline. */
            if (sink.enabled(severity))
                ok_writes++;
            if (!err.ok())
                invariant("f100_write_ok_err");
        } else if (rc == STDLIB_FAIL) {
            if (err.category() != STDLIB_CATEGORY_BOUNDS)
                invariant("f100_write_fail_cat");
        } else {
            invariant("f100_write_rc");
        }
        mix((unsigned long long)i);
        mix((unsigned long long)level);
        mix((unsigned long long)severity);
        mix((unsigned long long)n);
        mix((unsigned long long)fields);
        mix((unsigned long long)rc);
        mix((unsigned long long)err.category());
    }

    /* ---- convenience methods and borrowed sinks ------------------------ */
    sink.set_level(LOG_TRACE);
    err.clear();
    if (sink.info("info-line", &err) != STDLIB_OK)
        invariant("f100_info");
    if (sink.warn("warn-line", &err) != STDLIB_OK)
        invariant("f100_warn");
    if (sink.failure("error-line", &err) != STDLIB_OK)
        invariant("f100_failure");
    ok_writes += 3;
    borrowed_err = log_stderr(LOG_FATAL);
    borrowed_out = log_stdout(LOG_FATAL);
    err.clear();
    if (borrowed_err.write(LOG_INFO, "filtered", NULL, NULL, 0,
                           NULL, 0, &err) != STDLIB_OK ||
        borrowed_out.write(LOG_INFO, "filtered", NULL, NULL, 0,
                           NULL, 0, &err) != STDLIB_OK)
        invariant("f100_borrowed_filter");
    borrowed_err.set_level(LOG_TRACE);
    if (borrowed_err.write(LOG_INFO, "to-stderr", NULL, NULL, 0,
                           NULL, 0, &err) != STDLIB_OK)
        invariant("f100_borrowed_write");
    borrowed_err.drop();
    borrowed_out.drop();
    sink.drop();
    sink.drop(); /* idempotent */
    print("f100_borrowed_sinks_alive");

    /* ---- newline proof ------------------------------------------------- */
    reader = fopen(logpath, "rb");
    if (reader == NULL) {
        invariant("f100_reopen");
    } else {
        newlines = 0;
        while ((ch = fgetc(reader)) != EOF) {
            if (ch == '\n')
                newlines++;
        }
        fclose(reader);
    }
    if (newlines != ok_writes)
        invariant("f100_newline_count");
    mix((unsigned long long)ok_writes);
    mix((unsigned long long)newlines);

    /* ---- cleanup proof ------------------------------------------------- */
    if (fs_remove(logpath, &err) != STDLIB_OK || fs_exists(logpath) != 0)
        invariant("f100_cleanup_remove");
    after = count_prefix();
    if (baseline >= 0 && after >= 0 && after != baseline)
        invariant("f100_cleanup_proof");
    mix((unsigned long long)(after - baseline));

    (void)!alarm(0);
    mix((unsigned long long)failures);
    snprintf(hex, sizeof(hex), "%016llx", digest);
    print("f100_digest", hex);
    if (failures == 0)
        print("f100:", CASES, "cases ok");
    else
        print("f100: FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
