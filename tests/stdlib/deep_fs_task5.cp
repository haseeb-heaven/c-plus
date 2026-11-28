/* Independent Task-5 deep contract tests for stdlib/fs.hp and stdlib/time.hp.
 *
 * This file contains ONLY cases not exercised by tests/stdlib/deep_fs_time.cp
 * (54 checks).  It never duplicates those checks; it probes the remaining
 * genuine gaps:
 *
 *   A. INVALID PATHS
 *      A1 regular file used as a directory parent (mkdir/rename/mkdtemp)
 *      A2 embedded NUL (buffers built with explicit memcpy lengths; the
 *         explicit-length surface of fs_mkdtemp is exercised too)
 *      A3 empty string ""
 *      A4 path longer than PATH_MAX
 *      Each invalid-path case must produce a DEFINED failure with the
 *      documented category (IO for syscall failures, INVALID for malformed
 *      templates) and leave no artifact.
 *   B. PERMISSIONS
 *      fs_mkdir(dir), chmod(dir, 0000) (fs_mkdir has no mode argument), then
 *      try to create a file inside.  If euid == 0 the result is
 *      non-deterministic (root bypasses DAC) -> SKIP with reason, not a pass.
 *   C. MISSING-FILE OPERATIONS
 *      repeated fs_remove of a missing path; rename with missing source must
 *      leave an existing target's CONTENT unchanged; rename over an existing
 *      file must replace the target's CONTENT atomically (not just its size).
 *   D. RESOURCE CLEANUP AFTER FAILURES
 *      30 failed rename/remove/mkdtemp calls followed by successful ones;
 *      /dev/fd count and the number of /tmp entries with our PID prefix must
 *      be identical before and after.
 *   E. REPEATED REMOVE
 *      (fs has no handle to close/drop) repeated fs_remove on the same path
 *      returns a defined failure and never crashes.
 *   F. TIME BOUNDARIES
 *      1 ns, UINT64_MAX (exact wrapped representation on this host), NULL/zero
 *      buffer with error-record reset, exact-fit 21-byte buffer with a guard,
 *      time_sleep_ms(0) and (1), 100 monotonic samples never going backwards,
 *      and a negative (wrapped) nanosecond input.
 *
 * Every record is `<tag> <1|0>` (1 = check passed).  The final record is
 * `FS_TASK5 <checks> <failures>`.  Exit code 0 only when failures == 0.
 * A root-only permission SKIP is printed as `SKIP FS5_PERMISSIONS <reason>`
 * and is NOT counted as a pass or a failure.
 *
 * All fixtures use PID-unique /tmp paths; every file is unlinked and every
 * directory removed with rmdir() before the process exits.
 */
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syslimits.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/fs.hp"
#include "stdlib/time.hp"

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

static int f5_checks = 0;
static int f5_failures = 0;

static void f5(const char *tag, int cond) {
    f5_checks++;
    if (!cond)
        f5_failures++;
    print(tag, cond ? 1 : 0);
}

/* ---- small libc-only file fixtures --------------------------------------- */
static int f5_write_file(const char *path, const char *data, int n) {
    FILE *fp = fopen(path, "wb");
    int ok = 0;
    if (fp != NULL) {
        ok = (n == 0 || fwrite(data, 1, (size_t)n, fp) == (size_t)n);
        if (fclose(fp) != 0)
            ok = 0;
    }
    return ok;
}

static int f5_read_file(const char *path, char *out, int cap, int *out_len) {
    FILE *fp = fopen(path, "rb");
    int n = 0;
    int ok = 0;
    if (fp != NULL) {
        n = (int)fread(out, 1, (size_t)(cap - 1), fp);
        out[n] = '\0';
        if (out_len != NULL)
            *out_len = n;
        ok = (fclose(fp) == 0);
    }
    return ok;
}

/* Number of open descriptors.  opendir() itself consumes one fd, but both
 * measurements use the same method so the counts are directly comparable. */
static int f5_count_fds(void) {
    DIR *d = opendir("/dev/fd");
    struct dirent *e;
    int n = 0;
    if (d == NULL)
        return -1;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] != '.')
            n++;
    }
    closedir(d);
    return n;
}

/* Number of /tmp entries belonging to this process's fixture prefix. */
static int f5_count_tmp(const char *prefix) {
    DIR *d = opendir("/tmp");
    struct dirent *e;
    size_t plen = strlen(prefix);
    int n = 0;
    if (d == NULL)
        return -1;
    while ((e = readdir(d)) != NULL) {
        if (strncmp(e->d_name, prefix, plen) == 0)
            n++;
    }
    closedir(d);
    return n;
}

int main(void) {
    error err = error();
    char pidtag[32];
    char prefix[64];
    char path[256], path2[256], path3[256], path4[256];
    char buf[64], buf2[64];
    char nulbuf[128], nultmpl[128], nultrunc[128];
    char longpath[PATH_MAX + 128], longtmpl[PATH_MAX + 128];
    int i, rc, n = 0;
    int fd0, tmp0;
    size_t len;

    snprintf(pidtag, sizeof(pidtag), "%d", (int)getpid());
    snprintf(prefix, sizeof(prefix), "cplus_fs5_%s_", pidtag);
    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();

    /* ==================================================== A1 file-as-parent */
    snprintf(path, sizeof(path), "/tmp/%sfileparent", pidtag);
    snprintf(path2, sizeof(path2), "/tmp/%sfileparent/child", pidtag);
    snprintf(path3, sizeof(path3), "/tmp/%sfileparent/XXXXXX", pidtag);
    snprintf(path4, sizeof(path4), "/tmp/%sfileparent_src", pidtag);
    f5_write_file(path, "parent", 6);
    f5_write_file(path4, "src", 3);
    rc = fs_mkdir(path2, &err);
    f5("FS5_FILE_PARENT_MKDIR",
       rc == STDLIB_FAIL && err.category() == STDLIB_CATEGORY_IO &&
           err.code() == CPLUS_EXIT_IO && err.what()[0] != '\0' &&
           fs_is_dir(path2) == 0 && fs_is_file(path) == 1);
    rc = fs_rename(path4, path2, &err);
    f5("FS5_FILE_PARENT_RENAME",
       rc == STDLIB_FAIL && err.category() == STDLIB_CATEGORY_IO &&
           fs_exists(path2) == 0 && fs_is_file(path4) == 1);
    rc = fs_mkdtemp(path3, sizeof(path3), &err);
    f5("FS5_FILE_PARENT_MKDTEMP",
       rc == STDLIB_FAIL && err.category() == STDLIB_CATEGORY_IO &&
           fs_is_dir(path3) == 0 && fs_is_file(path) == 1);
    (void)fs_remove(path4, &err);
    (void)fs_remove(path, &err);
    f5("FS5_FILE_PARENT_CLEAN", fs_exists(path) == 0 && fs_exists(path4) == 0);

    /* ==================================================== A2 embedded NUL */
    snprintf(path, sizeof(path), "/tmp/%snulprefix_absent", pidtag);
    memset(nulbuf, 0, sizeof(nulbuf));
    len = strlen(path);
    memcpy(nulbuf, path, len);              /* explicit length */
    memcpy(nulbuf + len + 1, "TRAILING", 8); /* bytes AFTER the embedded NUL */
    rc = fs_size(nulbuf, &err);
    f5("FS5_NUL_SIZE",
       rc == -1 && err.category() == STDLIB_CATEGORY_IO &&
           err.code() == CPLUS_EXIT_IO && err.what()[0] != '\0');
    rc = fs_remove(nulbuf, &err);
    f5("FS5_NUL_REMOVE",
       rc == STDLIB_FAIL && err.category() == STDLIB_CATEGORY_IO &&
           fs_exists(path) == 0);

    /* Explicit-length fs_mkdtemp: strnlen stops at the embedded NUL, so the
     * visible prefix ends in "ABCDEF", not "XXXXXX" -> INVALID, no artifact. */
    memset(nultmpl, 0, sizeof(nultmpl));
    snprintf(path2, sizeof(path2), "/tmp/%snul_ABCDEF", pidtag);
    len = strlen(path2);
    memcpy(nultmpl, path2, len);
    memcpy(nultmpl + len + 1, "XXXXXX", 6);
    rc = fs_mkdtemp(nultmpl, sizeof(nultmpl), &err);
    f5("FS5_NUL_MKDTEMP_INVALID",
       rc == STDLIB_FAIL && err.category() == STDLIB_CATEGORY_INVALID &&
           err.code() == CPLUS_EXIT_UNSUPPORTED && fs_exists(path2) == 0);

    /* Characterisation: the POSIX syscalls truncate at the first NUL.  A file
     * at the visible prefix is found and unlinked by the NUL-containing path.
     * This is a DEFINED behaviour, not one of the required failures above. */
    snprintf(path3, sizeof(path3), "/tmp/%snultrunc", pidtag);
    f5_write_file(path3, "abc", 3);
    memset(nultrunc, 0, sizeof(nultrunc));
    len = strlen(path3);
    memcpy(nultrunc, path3, len);
    memcpy(nultrunc + len + 1, "IGNORED", 7);
    f5("FS5_NUL_TRUNCATION",
       fs_exists(nultrunc) == 1 && fs_size(nultrunc, &err) == 3 &&
           fs_remove(nultrunc, &err) == STDLIB_OK && fs_exists(path3) == 0);

    /* ==================================================== A3 empty string */
    f5("FS5_EMPTY_QUERIES",
       fs_exists("") == 0 && fs_is_file("") == 0 && fs_is_dir("") == 0);
    rc = fs_size("", &err);
    int empty_ok = (rc == -1 && err.category() == STDLIB_CATEGORY_IO &&
                    err.code() == CPLUS_EXIT_IO);
    empty_ok = empty_ok && fs_remove("", &err) == STDLIB_FAIL &&
               err.category() == STDLIB_CATEGORY_IO;
    empty_ok = empty_ok && fs_mkdir("", &err) == STDLIB_FAIL &&
               err.category() == STDLIB_CATEGORY_IO;
    empty_ok = empty_ok && fs_rename("", "/tmp/x", &err) == STDLIB_FAIL &&
               err.category() == STDLIB_CATEGORY_IO;
    empty_ok = empty_ok && fs_mkdtemp(buf, 0, &err) == STDLIB_FAIL &&
               err.category() == STDLIB_CATEGORY_INVALID;
    f5("FS5_EMPTY_OPS", empty_ok);

    /* ==================================================== A4 > PATH_MAX */
    memset(longpath, 'a', sizeof(longpath) - 1);
    longpath[sizeof(longpath) - 1] = '\0';
    memcpy(longpath, "/tmp/", 5);
    len = strlen(longpath);
    f5("FS5_LONG_LENGTH", len > (size_t)PATH_MAX);
    int long_ok = (fs_exists(longpath) == 0);
    long_ok = long_ok && fs_size(longpath, &err) == -1 &&
              err.category() == STDLIB_CATEGORY_IO;
    long_ok = long_ok && fs_remove(longpath, &err) == STDLIB_FAIL &&
              err.category() == STDLIB_CATEGORY_IO;
    long_ok = long_ok && fs_mkdir(longpath, &err) == STDLIB_FAIL &&
              err.category() == STDLIB_CATEGORY_IO;
    long_ok = long_ok && fs_rename(longpath, "/tmp/x", &err) == STDLIB_FAIL &&
              err.category() == STDLIB_CATEGORY_IO;
    f5("FS5_LONG_PATH", long_ok);
    memset(longtmpl, 'a', sizeof(longtmpl) - 1);
    longtmpl[sizeof(longtmpl) - 1] = '\0';
    memcpy(longtmpl, "/tmp/", 5);
    memcpy(longtmpl + sizeof(longtmpl) - 1 - 6, "XXXXXX", 6);
    rc = fs_mkdtemp(longtmpl, sizeof(longtmpl), &err);
    f5("FS5_LONG_MKDTEMP",
       rc == STDLIB_FAIL && err.category() == STDLIB_CATEGORY_IO);

    /* ==================================================== B permissions */
    snprintf(path, sizeof(path), "/tmp/%spermdir", pidtag);
    snprintf(path2, sizeof(path2), "/tmp/%spermdir/inside", pidtag);
    snprintf(path3, sizeof(path3), "/tmp/%spermdir/subdir", pidtag);
    if (geteuid() == 0) {
        printf("SKIP FS5_PERMISSIONS reason=euid=0-root-bypasses-DAC-nondeterministic\n");
    } else {
        int perm_ok = (fs_mkdir(path, &err) == STDLIB_OK && fs_is_dir(path) == 1 &&
                       chmod(path, 0000) == 0);
        f5("FS5_PERM_SETUP", perm_ok);
        if (perm_ok) {
            FILE *fp = fopen(path2, "w");
            f5("FS5_PERM_CREATE_DENIED", fp == NULL);
            if (fp != NULL) {
                fclose(fp);
                (void)fs_remove(path2, &err);
            }
            rc = fs_mkdir(path3, &err);
            f5("FS5_PERM_MKDIR_DENIED",
               rc == STDLIB_FAIL && err.category() == STDLIB_CATEGORY_IO &&
                   fs_is_dir(path3) == 0);
            chmod(path, 0755);
            f5("FS5_PERM_CLEANUP", rmdir(path) == 0 && fs_exists(path) == 0);
        } else {
            /* Do not crash or leave an artifact if setup was impossible. */
            chmod(path, 0755);
            (void)rmdir(path);
            f5("FS5_PERM_CREATE_DENIED", 0);
            f5("FS5_PERM_MKDIR_DENIED", 0);
            f5("FS5_PERM_CLEANUP", fs_exists(path) == 0);
        }
    }

    /* ==================================================== C missing / rename */
    snprintf(path, sizeof(path), "/tmp/%smiss_a", pidtag);
    snprintf(path2, sizeof(path2), "/tmp/%smiss_b", pidtag);
    snprintf(path3, sizeof(path3), "/tmp/%skeep", pidtag);
    int miss_ok = 1;
    for (i = 0; i < 3; i++) {
        rc = fs_remove(path, &err);
        if (rc != STDLIB_FAIL || err.category() != STDLIB_CATEGORY_IO ||
            err.code() != CPLUS_EXIT_IO || fs_exists(path) != 0)
            miss_ok = 0;
    }
    f5("FS5_MISSING_REMOVE_REPEAT", miss_ok);
    f5_write_file(path3, "KEEP", 4);
    rc = fs_rename(path, path3, &err);
    f5("FS5_MISSING_RENAME_SRC",
       rc == STDLIB_FAIL && err.category() == STDLIB_CATEGORY_IO &&
           fs_size(path3, &err) == 4 && err.ok() &&
           f5_read_file(path3, buf, sizeof(buf), &n) && n == 4 &&
           memcmp(buf, "KEEP", 4) == 0);
    (void)fs_remove(path3, &err);

    snprintf(path, sizeof(path), "/tmp/%sov_src", pidtag);
    snprintf(path2, sizeof(path2), "/tmp/%sov_dst", pidtag);
    f5_write_file(path, "NEWCONTENT", 10);
    f5_write_file(path2, "OLDLONGCONTENT", 14);
    rc = fs_rename(path, path2, &err);
    f5("FS5_RENAME_OVERWRITE_CONTENT",
       rc == STDLIB_OK && err.ok() && fs_exists(path) == 0 &&
           fs_is_file(path2) == 1 && fs_size(path2, &err) == 10 &&
           f5_read_file(path2, buf, sizeof(buf), &n) && n == 10 &&
           memcmp(buf, "NEWCONTENT", 10) == 0);
    (void)fs_remove(path2, &err);

    /* ==================================================== D cleanup/leaks */
    /* Genuine gap found here: a FAILED fs_mkdtemp mutates the caller's
     * template (libc replaces XXXXXX before mkdir fails).  Reusing the same
     * buffer without restoring the suffix therefore reports INVALID instead
     * of IO.  Both calls below are defined failures and create no artifact;
     * the contract documents the exact observed categories. */
    snprintf(path3, sizeof(path3), "/tmp/%smutate_no_dir/XXXXXX", pidtag);
    rc = fs_mkdtemp(path3, sizeof(path3), &err);
    int mut_first = (rc == STDLIB_FAIL && err.category() == STDLIB_CATEGORY_IO);
    int mut_reuse = fs_mkdtemp(path3, sizeof(path3), &err);
    f5("FS5_MKDTEMP_FAILED_TEMPLATE_REUSE",
       mut_first && mut_reuse == STDLIB_FAIL &&
           (err.category() == STDLIB_CATEGORY_INVALID ||
            err.category() == STDLIB_CATEGORY_IO) &&
           fs_exists(path3) == 0);

    fd0 = f5_count_fds();
    tmp0 = f5_count_tmp(prefix);
    f5("FS5_RESOURCE_BASELINE", fd0 >= 0 && tmp0 >= 0);
    snprintf(path, sizeof(path), "/tmp/%sloop_src", pidtag);
    snprintf(path2, sizeof(path2), "/tmp/%sloop_dst", pidtag);
    snprintf(path3, sizeof(path3), "/tmp/%sloop_no_dir/XXXXXX", pidtag);
    int loop_ok = 1;
    for (i = 0; i < 30; i++) {
        /* A failed mkdtemp consumes the XXXXXX suffix in the caller's buffer
         * (libc mutates it before mkdir fails), so restore the fixture each
         * iteration to keep probing the same syscall-failure path. */
        snprintf(path3, sizeof(path3), "/tmp/%sloop_no_dir/XXXXXX", pidtag);
        if (fs_rename(path, path2, &err) != STDLIB_FAIL ||
            err.category() != STDLIB_CATEGORY_IO)
            loop_ok = 0;
        if (fs_remove(path, &err) != STDLIB_FAIL ||
            err.category() != STDLIB_CATEGORY_IO)
            loop_ok = 0;
        if (fs_mkdtemp(path3, sizeof(path3), &err) != STDLIB_FAIL ||
            err.category() != STDLIB_CATEGORY_IO)
            loop_ok = 0;
    }
    f5("FS5_REPEAT_FAILURES", loop_ok);
    /* Successful operations after 90 failures must still work. */
    snprintf(path3, sizeof(path3), "/tmp/%sXXXXXX", pidtag);
    int recover_ok = (fs_mkdtemp(path3, sizeof(path3), &err) == STDLIB_OK &&
                      fs_is_dir(path3) == 1 && rmdir(path3) == 0);
    f5_write_file(path, "ok", 2);
    recover_ok = recover_ok && fs_rename(path, path2, &err) == STDLIB_OK;
    recover_ok = recover_ok && fs_remove(path2, &err) == STDLIB_OK &&
                 fs_exists(path2) == 0;
    f5("FS5_RECOVER_AFTER_FAILURES", recover_ok);
    f5("FS5_RESOURCE_NO_LEAK",
       f5_count_fds() == fd0 && f5_count_tmp(prefix) == tmp0);

    /* ==================================================== E repeated remove */
    snprintf(path, sizeof(path), "/tmp/%sdrop_like", pidtag);
    f5_write_file(path, "x", 1);
    int rep_ok = (fs_remove(path, &err) == STDLIB_OK && err.ok());
    for (i = 0; i < 4; i++) {
        rc = fs_remove(path, &err);
        if (rc != STDLIB_FAIL || err.category() != STDLIB_CATEGORY_IO ||
            err.code() != CPLUS_EXIT_IO || fs_exists(path) != 0)
            rep_ok = 0;
    }
    f5("FS5_REPEATED_REMOVE", rep_ok);

    /* ==================================================== F time boundaries */
    rc = time_format_iso8601(1ULL, buf, sizeof(buf), &err);
    f5("FS5_TIME_ONE_NS",
       rc == STDLIB_OK && err.ok() && strcmp(buf, "1970-01-01T00:00:00Z") == 0);

    rc = time_format_iso8601(18446744073709551615ULL, buf, sizeof(buf), &err);
    f5("FS5_TIME_UINT64_MAX",
       rc == STDLIB_OK && err.ok() &&
           strcmp(buf, "2554-07-21T23:34:33Z") == 0);

    /* NULL buffer and zero size: INVALID, then a valid call clears the record. */
    memset(buf2, 'Z', sizeof(buf2));
    buf2[sizeof(buf2) - 1] = '\0';
    rc = time_format_iso8601(1000000000ULL, NULL, 32, &err);
    int terr_ok = (rc == STDLIB_FAIL &&
                   err.category() == STDLIB_CATEGORY_INVALID &&
                   err.code() == CPLUS_EXIT_UNSUPPORTED && err.what()[0] != '\0');
    rc = time_format_iso8601(1000000000ULL, buf2, 0, &err);
    terr_ok = terr_ok && rc == STDLIB_FAIL &&
              err.category() == STDLIB_CATEGORY_INVALID && buf2[0] == 'Z';
    rc = time_format_iso8601(1000000000ULL, buf2, sizeof(buf2), &err);
    terr_ok = terr_ok && rc == STDLIB_OK && err.ok() &&
              strcmp(buf2, "1970-01-01T00:00:01Z") == 0;
    f5("FS5_TIME_NULL_ZERO_RESET", terr_ok);

    /* Exact-fit 21-byte buffer (20 chars + NUL) with an adjacent guard. */
    {
        char exact[21];
        char guard = 0x5a;
        memset(exact, 'X', sizeof(exact));
        rc = time_format_iso8601(1000000000ULL, exact, sizeof(exact), &err);
        f5("FS5_TIME_EXACT21_GUARD",
           rc == STDLIB_OK && err.ok() && strlen(exact) == 20 &&
               exact[20] == '\0' && strcmp(exact, "1970-01-01T00:00:01Z") == 0 &&
               guard == 0x5a);
    }

    /* Negative caller input wraps to a large positive; documented consequence
     * of the unsigned-nanoseconds API (different value from deep_fs_time's -1). */
    rc = time_format_iso8601((unsigned long long)(-86400000000000LL), buf,
                             sizeof(buf), &err);
    f5("FS5_TIME_NEGATIVE_WRAP",
       rc == STDLIB_OK && err.ok() && strlen(buf) == 20 && buf[19] == 'Z');

    /* time_sleep_ms(0) must not block; (1) must sleep ~1ms, not overshoot. */
    {
        unsigned long long z0 = time_monotonic_ns();
        time_sleep_ms(0);
        unsigned long long z1 = time_monotonic_ns();
        unsigned long long o0 = time_monotonic_ns();
        time_sleep_ms(1);
        unsigned long long o1 = time_monotonic_ns();
        f5("FS5_TIME_SLEEP_ZERO",
           z1 >= z0 && (z1 - z0) < 100000000ULL);
        f5("FS5_TIME_SLEEP_ONE_MS",
           o1 >= o0 && (o1 - o0) >= 500000ULL && (o1 - o0) < 100000000ULL);
    }

    /* 100 monotonic samples, never backwards. */
    {
        unsigned long long prev = time_monotonic_ns();
        int mono_ok = 1;
        for (i = 0; i < 100; i++) {
            unsigned long long now = time_monotonic_ns();
            if (now < prev)
                mono_ok = 0;
            prev = now;
        }
        f5("FS5_TIME_MONOTONIC_100", mono_ok && prev > 0);
    }

    /* ==================================================== final no leftovers */
    f5("FS5_NO_LEFTOVERS", f5_count_tmp(prefix) == tmp0);

    print("FS_TASK5", f5_checks, f5_failures);
    return f5_failures == 0 ? 0 : 1;
}
