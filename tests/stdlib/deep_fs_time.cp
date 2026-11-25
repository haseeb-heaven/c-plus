/* Deep behavioural contract tests for stdlib/fs.hp and stdlib/time.hp.
 *
 * Self-checking: every expectation is asserted in-process; the process exits
 * non-zero if any check fails.  No .expected file is needed.  Paths are unique
 * per pid so the test is safe to run in parallel with other agents.
 *
 * Classification used throughout (see docs/stdlib/DEEP_FS_TIME_CONTRACT.md):
 *   SUCCESS     - STDLIB_OK (0), error record clear.
 *   RECOVERABLE - STDLIB_FAIL (-1) with a populated error record (category and
 *                 cplus_exit_code); the process keeps running.
 *   FATAL       - a cplus_fatal exit code (101..109).  None of the exercised
 *                 inputs are allowed to be fatal; reaching the end proves it.
 */
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"
#include "stdlib/fs.hp"
#include "stdlib/time.hp"

static int g_checks = 0;
static int g_failures = 0;

static void expect(int condition, const char *label) {
    g_checks++;
    if (!condition) {
        g_failures++;
        fprintf(stderr, "FAIL %s\n", label);
    }
}

/* ---- deterministic failure-injection: error callback capture -------------- */
static int cb_calls = 0;
static int cb_cat = -1;
static cplus_exit_code cb_code = CPLUS_EXIT_OK;
static void capture_error(int category, cplus_exit_code code,
                          const char *message) {
    (void)message;
    cb_calls++;
    cb_cat = category;
    cb_code = code;
}

/* ---- interrupted-sleep child --------------------------------------------- */
static volatile sig_atomic_t g_alarm_seen = 0;
static void on_alarm(int signo) {
    (void)signo;
    g_alarm_seen = 1;
}

static void test_fs(const char *pidtag) {
    error err = error();
    char src[160], dst[160], dir[160], tmpl[160], badparent[160];
    int rc;

    /* SUCCESS queries on a known directory. */
    expect(fs_exists("/tmp") == 1, "fs.exists-tmp");
    expect(fs_is_dir("/tmp") == 1, "fs.isdir-tmp");
    expect(fs_is_file("/tmp") == 0, "fs.isfile-tmp");

    /* NULL paths are RECOVERABLE for fallible calls, inert for queries. */
    expect(fs_exists(NULL) == 0, "fs.exists-null");
    expect(fs_is_file(NULL) == 0, "fs.isfile-null");
    expect(fs_is_dir(NULL) == 0, "fs.isdir-null");
    expect(fs_size(NULL, &err) == -1 && err.category() == STDLIB_CATEGORY_INVALID &&
               err.code() == CPLUS_EXIT_UNSUPPORTED,
           "fs.size-null");
    expect(fs_remove(NULL, &err) == STDLIB_FAIL &&
               err.category() == STDLIB_CATEGORY_INVALID,
           "fs.remove-null");
    expect(fs_rename(NULL, "x", &err) == STDLIB_FAIL &&
               err.category() == STDLIB_CATEGORY_INVALID,
           "fs.rename-null-from");
    expect(fs_rename("x", NULL, &err) == STDLIB_FAIL &&
               err.category() == STDLIB_CATEGORY_INVALID,
           "fs.rename-null-to");
    expect(fs_mkdir(NULL, &err) == STDLIB_FAIL &&
               err.category() == STDLIB_CATEGORY_INVALID,
           "fs.mkdir-null");
    /* A NULL error pointer must be tolerated, never fatal. */
    expect(fs_size(NULL, NULL) == -1 && fs_remove(NULL, NULL) == STDLIB_FAIL &&
               fs_rename(NULL, NULL, NULL) == STDLIB_FAIL &&
               fs_mkdir(NULL, NULL) == STDLIB_FAIL &&
               fs_mkdtemp(NULL, 0, NULL) == STDLIB_FAIL,
           "fs.null-error-pointer");

    /* Missing paths: queries are false; sized stat is RECOVERABLE IO. */
    snprintf(src, sizeof(src), "/tmp/cplus_deep_fs_%s_missing", pidtag);
    expect(fs_exists(src) == 0 && fs_is_file(src) == 0 && fs_is_dir(src) == 0,
           "fs.missing-queries");
    expect(fs_size(src, &err) == -1 && err.category() == STDLIB_CATEGORY_IO &&
               err.code() == CPLUS_EXIT_IO,
           "fs.missing-size");

    /* RECOVERABLE failures: removing/renaming a missing path. */
    expect(fs_remove(src, &err) == STDLIB_FAIL &&
               err.category() == STDLIB_CATEGORY_IO &&
               err.code() == CPLUS_EXIT_IO,
           "fs.remove-missing");
    snprintf(dst, sizeof(dst), "/tmp/cplus_deep_fs_%s_missing_dst", pidtag);
    expect(fs_rename(src, dst, &err) == STDLIB_FAIL &&
               err.category() == STDLIB_CATEGORY_IO,
           "fs.rename-missing-src");

    /* SUCCESS: create two files, then rename over an existing destination. */
    snprintf(src, sizeof(src), "/tmp/cplus_deep_fs_%s_src", pidtag);
    snprintf(dst, sizeof(dst), "/tmp/cplus_deep_fs_%s_dst", pidtag);
    file fsrc = file(src, "w");
    if (fsrc.is_open())
        fsrc.write("SRC");
    fsrc.drop();
    file fdst = file(dst, "w");
    if (fdst.is_open())
        fdst.write("DST");
    fdst.drop();
    expect(fs_is_file(src) == 1 && fs_is_file(dst) == 1, "fs.created-files");
    expect(fs_size(src, &err) == 3 && fs_size(dst, &err) == 3, "fs.sizes-before");
    rc = fs_rename(src, dst, &err);
    expect(rc == STDLIB_OK && err.ok(), "fs.rename-existing-ok");
    expect(fs_exists(src) == 0 && fs_is_file(dst) == 1 &&
               fs_size(dst, &err) == 3,
           "fs.rename-overwrites");
    /* RECOVERABLE: second rename of the now-missing source fails. */
    expect(fs_rename(src, dst, &err) == STDLIB_FAIL &&
               err.category() == STDLIB_CATEGORY_IO,
           "fs.rename-twice");

    /* RECOVERABLE: rename into a nonexistent parent directory. */
    snprintf(badparent, sizeof(badparent),
             "/tmp/cplus_deep_fs_%s_no_dir/child", pidtag);
    expect(fs_rename(dst, badparent, &err) == STDLIB_FAIL &&
               err.category() == STDLIB_CATEGORY_IO,
           "fs.rename-bad-parent");
    expect(fs_is_file(dst) == 1, "fs.rename-bad-parent-atomic");

    /* mkdtemp SUCCESS and invalid templates (RECOVERABLE INVALID). */
    snprintf(tmpl, sizeof(tmpl), "/tmp/cplus_deep_fs_%s_XXXXXX", pidtag);
    expect(fs_mkdtemp(tmpl, sizeof(tmpl), &err) == STDLIB_OK && fs_is_dir(tmpl),
           "fs.mkdtemp-ok");
    char shortbuf[8] = "abcdefg";
    expect(fs_mkdtemp(shortbuf, sizeof(shortbuf), &err) == STDLIB_FAIL &&
               err.category() == STDLIB_CATEGORY_INVALID,
           "fs.mkdtemp-no-suffix");
    expect(fs_mkdtemp(NULL, 32, &err) == STDLIB_FAIL &&
               err.category() == STDLIB_CATEGORY_INVALID,
           "fs.mkdtemp-null");
    expect(fs_mkdtemp(tmpl, 6, &err) == STDLIB_FAIL &&
               err.category() == STDLIB_CATEGORY_INVALID,
           "fs.mkdtemp-small-size");
    /* RECOVERABLE IO: template is valid but its parent directory is absent. */
    char baddir[160];
    snprintf(baddir, sizeof(baddir),
             "/tmp/cplus_deep_fs_%s_no_dir/XXXXXX", pidtag);
    expect(fs_mkdtemp(baddir, sizeof(baddir), &err) == STDLIB_FAIL &&
               err.category() == STDLIB_CATEGORY_IO &&
               err.code() == CPLUS_EXIT_IO,
           "fs.mkdtemp-bad-parent");

    /* mkdir SUCCESS, plus RECOVERABLE IO when it already exists. */
    snprintf(dir, sizeof(dir), "/tmp/cplus_deep_fs_%s_dir", pidtag);
    expect(fs_mkdir(dir, &err) == STDLIB_OK && fs_is_dir(dir), "fs.mkdir-ok");
    expect(fs_mkdir(dir, &err) == STDLIB_FAIL &&
               err.category() == STDLIB_CATEGORY_IO,
           "fs.mkdir-exists");

    /* Deterministic failure injection via the stdlib error callback. */
    cb_calls = 0;
    cb_cat = -1;
    cb_code = CPLUS_EXIT_OK;
    stdlib_set_error_callback(capture_error);
    (void)fs_size(NULL, &err);
    stdlib_set_error_callback(NULL);
    expect(cb_calls == 1 && cb_cat == STDLIB_CATEGORY_INVALID &&
               cb_code == CPLUS_EXIT_UNSUPPORTED,
           "fs.error-callback");

    /* No leftover temp artifacts: fs_remove is unlink-only, so directories
     * must be removed with rmdir().  Every path created above is checked. */
    expect(fs_remove(src, &err) == STDLIB_FAIL, "fs.remove-already-gone");
    expect(fs_remove(dst, &err) == STDLIB_OK && fs_exists(dst) == 0,
           "fs.cleanup-file");
    expect(fs_remove(dir, &err) == STDLIB_FAIL && fs_is_dir(dir) == 1,
           "fs.remove-cannot-rmdir");
    expect(rmdir(dir) == 0 && fs_exists(dir) == 0, "fs.cleanup-mkdir");
    expect(rmdir(tmpl) == 0 && fs_exists(tmpl) == 0, "fs.cleanup-mkdtemp");
    expect(fs_exists(badparent) == 0 && fs_exists(baddir) == 0,
           "fs.no-failed-op-artifacts");
}

static void test_time(void) {
    error err = error();
    char buf[64];
    int rc;

    /* Timestamp boundaries with exact expected strings. */
    rc = time_format_iso8601(0ULL, buf, sizeof(buf), &err);
    expect(rc == STDLIB_OK && strcmp(buf, "1970-01-01T00:00:00Z") == 0,
           "time.epoch-zero");
    rc = time_format_iso8601(999999999ULL, buf, sizeof(buf), &err);
    expect(rc == STDLIB_OK && strcmp(buf, "1970-01-01T00:00:00Z") == 0,
           "time.sub-second-truncated");
    rc = time_format_iso8601(1000000000ULL, buf, sizeof(buf), &err);
    expect(rc == STDLIB_OK && strcmp(buf, "1970-01-01T00:00:01Z") == 0,
           "time.one-second");
    rc = time_format_iso8601(1700000000000000000ULL, buf, sizeof(buf), &err);
    expect(rc == STDLIB_OK && strcmp(buf, "2023-11-14T22:13:20Z") == 0,
           "time.known-value");

    /* Large / wrapped-negative inputs: the API is unsigned nanoseconds, so a
     * negative used by a caller wraps to a large positive and is formatted as
     * a (future) date rather than rejected.  Assert a well-formed ISO string.
     * This is a documented consequence, not a defect. */
    rc = time_format_iso8601(18446744073709551615ULL, buf, sizeof(buf), &err);
    expect(rc == STDLIB_OK && strlen(buf) == 20 && buf[19] == 'Z' &&
               buf[4] == '-' && buf[7] == '-' && buf[10] == 'T' &&
               buf[13] == ':' && buf[16] == ':' && strncmp(buf, "1969", 4) != 0,
           "time.ullong-max");
    rc = time_format_iso8601((unsigned long long)(-1LL), buf, sizeof(buf), &err);
    expect(rc == STDLIB_OK && strlen(buf) == 20 && buf[19] == 'Z',
           "time.wrapped-negative");

    /* Small output buffers: 0/1/exact/truncated. */
    char small[40];
    memset(small, 'X', sizeof(small));
    small[sizeof(small) - 1] = '\0';
    rc = time_format_iso8601(1700000000000000000ULL, small, 0, &err);
    expect(rc == STDLIB_FAIL && err.category() == STDLIB_CATEGORY_INVALID &&
               err.code() == CPLUS_EXIT_UNSUPPORTED && small[0] == 'X',
           "time.buffer-zero");
    rc = time_format_iso8601(1700000000000000000ULL, small, 1, &err);
    expect(rc == STDLIB_FAIL && err.category() == STDLIB_CATEGORY_BOUNDS &&
               err.code() == CPLUS_EXIT_BOUNDS && small[0] == '\0',
           "time.buffer-one");
    rc = time_format_iso8601(1700000000000000000ULL, small, 4, &err);
    expect(rc == STDLIB_FAIL && err.category() == STDLIB_CATEGORY_BOUNDS &&
               small[0] == '\0',
           "time.buffer-four");
    rc = time_format_iso8601(1700000000000000000ULL, small, 20, &err);
    expect(rc == STDLIB_FAIL && err.category() == STDLIB_CATEGORY_BOUNDS &&
               small[0] == '\0',
           "time.buffer-truncated");
    rc = time_format_iso8601(1700000000000000000ULL, small, 21, &err);
    expect(rc == STDLIB_OK && strcmp(small, "2023-11-14T22:13:20Z") == 0 &&
               small[21] == 'X',
           "time.buffer-exact-21");
    memset(small, 'X', sizeof(small));
    small[sizeof(small) - 1] = '\0';
    rc = time_format_iso8601(1700000000000000000ULL, small, 32, &err);
    expect(rc == STDLIB_OK && strcmp(small, "2023-11-14T22:13:20Z") == 0 &&
               small[21] == 'X',
           "time.buffer-roomy-no-overrun");

    /* NULL buffer is RECOVERABLE INVALID; NULL error pointer is tolerated. */
    rc = time_format_iso8601(1000000000ULL, NULL, 32, &err);
    expect(rc == STDLIB_FAIL && err.category() == STDLIB_CATEGORY_INVALID,
           "time.null-buffer");
    expect(time_format_iso8601(1000000000ULL, buf, sizeof(buf), NULL) == STDLIB_OK,
           "time.null-error");

    /* Clocks: positive and monotonic ordering. */
    expect(time_now_ns() > 0 && time_now_ms() > 0 &&
               time_monotonic_ns() > 0 && time_monotonic_ms() > 0,
           "time.clocks-positive");
    unsigned long long m0 = time_monotonic_ns();
    time_sleep_ms(20);
    unsigned long long m1 = time_monotonic_ns();
    expect(m1 >= m0 && (m1 - m0) >= 10000000ULL, "time.monotonic-order");
}

static int test_sleep_eintr(void) {
    /* Parent watchdog + forked child: a SIGALRM interrupts nanosleep at 50ms;
     * time_sleep_ms must resume and sleep the full 300ms, not return early. */
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid == 0) {
        struct sigaction sa;
        struct itimerval it;
        unsigned long long t0, t1;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = on_alarm;
        sigaction(SIGALRM, &sa, NULL);
        it.it_value.tv_sec = 0;
        it.it_value.tv_usec = 50000;
        it.it_interval.tv_sec = 0;
        it.it_interval.tv_usec = 0;
        setitimer(ITIMER_REAL, &it, NULL);
        t0 = time_monotonic_ms();
        time_sleep_ms(300);
        t1 = time_monotonic_ms();
        _exit(g_alarm_seen && (t1 - t0) >= 200 ? 0 : 1);
    }
    int status = 0, waited = 0, i;
    for (i = 0; i < 100; i++) { /* 100 * 50ms = 5s watchdog */
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) { waited = 1; break; }
        if (r < 0 && errno == EINTR) continue;
        usleep(50000);
    }
    if (!waited) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
    }
    return waited && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

int main(void) {
    char pidtag[32];
    snprintf(pidtag, sizeof(pidtag), "%d", (int)getpid());

    test_fs(pidtag);
    test_time();
    expect(test_sleep_eintr() == 1, "time.sleep-eintr-resume");

    printf("deep_fs_time: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
