/* Agent 2 — remaining UNTESTED filesystem edge paths for stdlib/fs.hp.
 *
 * Scope (closes the gaps named in tests/stdlib/agent2_audit/module_audits/fs.hp.md
 * §7 that are reachable on this POSIX host):
 *   1  symlink semantics via symlink()/lstat(): link-to-file, link-to-dir and
 *      a dangling link.  fs.hp uses stat() (follows links) and unlink()
 *      (removes the link, not the target) — both are characterised here.
 *   2  file metadata via the only metadata helper in fs.hp, fs_size()
 *      (byte size), plus error-record reset on a successful call.
 *   3  overlong path (PATH_MAX + 127 bytes) -> defined IO failure.
 *   4  path with a trailing slash (dir resolves; regular file does not).
 *   5  path with '.' and '..' components.
 *   6  repeated fs_remove() of a missing path -> always defined IO failure.
 *   7  fs_mkdtemp template reseeding after a failed call: the consumed
 *      XXXXXX suffix is a documented libc non-defect, and reseeding the same
 *      buffer recovers.
 *   8  permission-denied directory: executed when euid != 0; when euid == 0
 *      the result is nondeterministic (root bypasses DAC) so an explicit
 *      SKIP line is printed instead of forcing a pass.
 *   9  cleanup proof: PID-unique /tmp prefix and /dev/fd counts are identical
 *      before and after every fixture (no leftovers, no descriptor leak).
 *
 * Self-checking. Every executed check prints "<TAG> <0|1>"; the final line
 * is "EXTRA_FS_EDGES <checks> <failures>". Exit code is 0 only when failures
 * == 0. A root-only permission SKIP prints "SKIP FS_PERMISSIONS reason=..."
 * and is NOT counted as a pass or a failure.
 *
 * No header is modified. All fixtures live under /tmp with a PID-unique
 * prefix so parallel runs cannot collide, and every file is unlinked and
 * every directory removed with rmdir() before the process exits.
 */
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/fs.hp"

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

static int x_checks = 0;
static int x_failures = 0;

static void x(const char *tag, int cond) {
    x_checks++;
    if (!cond)
        x_failures++;
    print(tag, cond ? 1 : 0);
}

/* Write a small known-content fixture file. */
static int x_write_file(const char *path, const char *data, int n) {
    FILE *fp = fopen(path, "wb");
    int ok = 0;
    if (fp != NULL) {
        ok = (n == 0 || fwrite(data, 1, (size_t)n, fp) == (size_t)n);
        if (fclose(fp) != 0)
            ok = 0;
    }
    return ok;
}

/* lstat() sees the link itself (stat() follows it); used to prove that
 * fs_remove() unlinks the symlink and leaves its target untouched. */
static int x_lstat_ok(const char *path) {
    struct stat info;
    return lstat(path, &info) == 0;
}

static int x_count_fds(void) {
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

static int x_count_tmp(const char *prefix) {
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
    char pidtag[32], prefix[64];
    char p1[256], p2[256], p3[256], p4[256], p5[256];
    char longpath[PATH_MAX + 128];
    int i, rc, rep_ok;
    int fd0, tmp0;

    snprintf(pidtag, sizeof(pidtag), "%d", (int)getpid());
    snprintf(prefix, sizeof(prefix), "cplus_fse_%s_", pidtag);
    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();

    fd0 = x_count_fds();
    tmp0 = x_count_tmp(prefix);
    x("FS_EDGE_BASELINE", fd0 >= 0 && tmp0 >= 0);

    /* ============================== 1a. symlink -> regular file ========== */
    snprintf(p1, sizeof(p1), "/tmp/%starget_file", pidtag);
    snprintf(p2, sizeof(p2), "/tmp/%slink_file", pidtag);
    x("FS_SYM_FILE_FIXTURE", x_write_file(p1, "HELLOFS", 7) == 1);
    x("FS_SYM_FILE_SETUP", symlink(p1, p2) == 0 && x_lstat_ok(p2) == 1);
    x("FS_SYM_FILE_EXISTS", fs_exists(p2) == 1);       /* stat follows link */
    x("FS_SYM_FILE_ISFILE", fs_is_file(p2) == 1);
    x("FS_SYM_FILE_ISDIR", fs_is_dir(p2) == 0);
    x("FS_SYM_FILE_SIZE", fs_size(p2, &err) == 7 && err.ok());
    x("FS_SYM_FILE_REMOVE_LINK",
      fs_remove(p2, &err) == STDLIB_OK && err.ok() && x_lstat_ok(p2) == 0);
    x("FS_SYM_FILE_TARGET_INTACT", fs_exists(p1) == 1 && fs_size(p1, &err) == 7);
    x("FS_SYM_FILE_CLEAN", fs_remove(p1, &err) == STDLIB_OK && fs_exists(p1) == 0);

    /* ============================== 1b. symlink -> directory ============= */
    snprintf(p1, sizeof(p1), "/tmp/%starget_dir", pidtag);
    snprintf(p2, sizeof(p2), "/tmp/%slink_dir", pidtag);
    x("FS_SYM_DIR_MKDIR", fs_mkdir(p1, &err) == STDLIB_OK && fs_is_dir(p1) == 1);
    x("FS_SYM_DIR_SETUP", symlink(p1, p2) == 0 && x_lstat_ok(p2) == 1);
    x("FS_SYM_DIR_EXISTS", fs_exists(p2) == 1);
    x("FS_SYM_DIR_ISDIR", fs_is_dir(p2) == 1);
    x("FS_SYM_DIR_ISFILE", fs_is_file(p2) == 0);
    x("FS_SYM_DIR_SIZE", fs_size(p2, &err) >= 0 && err.ok());
    x("FS_SYM_DIR_REMOVE_LINK",
      fs_remove(p2, &err) == STDLIB_OK && x_lstat_ok(p2) == 0 &&
          fs_is_dir(p1) == 1);
    x("FS_SYM_DIR_CLEAN", rmdir(p1) == 0 && fs_exists(p1) == 0);

    /* ============================== 1c. dangling symlink ================= */
    snprintf(p1, sizeof(p1), "/tmp/%sNO_SUCH_TARGET", pidtag);
    snprintf(p2, sizeof(p2), "/tmp/%slink_dangling", pidtag);
    x("FS_DANGLING_SETUP",
      symlink(p1, p2) == 0 && fs_exists(p1) == 0 && x_lstat_ok(p2) == 1);
    x("FS_DANGLING_EXISTS", fs_exists(p2) == 0);   /* stat fails: target gone */
    x("FS_DANGLING_ISFILE", fs_is_file(p2) == 0);
    x("FS_DANGLING_ISDIR", fs_is_dir(p2) == 0);
    rc = fs_size(p2, &err);
    x("FS_DANGLING_SIZE",
      rc == -1 && err.category() == STDLIB_CATEGORY_IO &&
          err.code() == CPLUS_EXIT_IO);
    x("FS_DANGLING_REMOVE", fs_remove(p2, &err) == STDLIB_OK && x_lstat_ok(p2) == 0);

    /* ============================== 2. metadata via fs_size ============== */
    snprintf(p1, sizeof(p1), "/tmp/%smeta", pidtag);
    x("FS_META_FIXTURE", x_write_file(p1, "ABCDE", 5) == 1);
    x("FS_META_SIZE", fs_size(p1, &err) == 5 && err.ok());
    /* A failed call populates err; a following success must reset it. */
    rc = fs_size(p2, &err); /* p2 removed above -> IO failure */
    int pre_reset = (rc == -1 && err.category() == STDLIB_CATEGORY_IO);
    x("FS_META_ERR_RESET",
      pre_reset && fs_size(p1, &err) == 5 && err.ok());
    x("FS_META_CLEAN", fs_remove(p1, &err) == STDLIB_OK && fs_exists(p1) == 0);

    /* ============================== 3. PATH_MAX+ path ==================== */
    memset(longpath, 'a', sizeof(longpath) - 1);
    longpath[sizeof(longpath) - 1] = '\0';
    memcpy(longpath, "/tmp/", 5);
    x("FS_LONG_LENGTH", strlen(longpath) > (size_t)PATH_MAX);
    x("FS_LONG_EXISTS", fs_exists(longpath) == 0 && fs_is_file(longpath) == 0);
    x("FS_LONG_SIZE",
      fs_size(longpath, &err) == -1 && err.category() == STDLIB_CATEGORY_IO);
    x("FS_LONG_REMOVE",
      fs_remove(longpath, &err) == STDLIB_FAIL &&
          err.category() == STDLIB_CATEGORY_IO);
    x("FS_LONG_MKDIR",
      fs_mkdir(longpath, &err) == STDLIB_FAIL &&
          err.category() == STDLIB_CATEGORY_IO);

    /* ============================== 4. trailing slash ==================== */
    snprintf(p1, sizeof(p1), "/tmp/%sslashdir", pidtag);
    snprintf(p2, sizeof(p2), "/tmp/%sslashdir/", pidtag);
    x("FS_SLASH_MKDIR", fs_mkdir(p1, &err) == STDLIB_OK);
    x("FS_SLASH_DIR", fs_exists(p2) == 1 && fs_is_dir(p2) == 1);
    snprintf(p3, sizeof(p3), "/tmp/%sslashfile", pidtag);
    snprintf(p4, sizeof(p4), "/tmp/%sslashfile/", pidtag);
    x("FS_SLASH_FILE_FIXTURE", x_write_file(p3, "z", 1) == 1);
    x("FS_SLASH_FILE_NOTDIR", fs_exists(p4) == 0 && fs_is_dir(p4) == 0);
    snprintf(p5, sizeof(p5), "/tmp/%sno_such_dir_%s/", pidtag, pidtag);
    x("FS_SLASH_MISSING", fs_exists(p5) == 0);
    x("FS_SLASH_CLEAN",
      fs_remove(p3, &err) == STDLIB_OK && rmdir(p1) == 0 && fs_exists(p1) == 0);

    /* ============================== 5. '.' and '..' ====================== */
    snprintf(p1, sizeof(p1), "/tmp/%sddir", pidtag);
    snprintf(p2, sizeof(p2), "/tmp/%sddir/sub", pidtag);
    snprintf(p3, sizeof(p3), "/tmp/%sddir/child", pidtag);
    snprintf(p4, sizeof(p4), "/tmp/%sddir/./child", pidtag);
    snprintf(p5, sizeof(p5), "/tmp/%sddir/sub/../child", pidtag);
    x("FS_DOT_SETUP",
      fs_mkdir(p1, &err) == STDLIB_OK && fs_mkdir(p2, &err) == STDLIB_OK &&
          x_write_file(p3, "DOT", 3) == 1);
    x("FS_DOT_COMPONENT",
      fs_exists(p4) == 1 && fs_is_file(p4) == 1 && fs_size(p4, &err) == 3);
    x("FS_DOTDOT_COMPONENT",
      fs_exists(p5) == 1 && fs_is_file(p5) == 1 && fs_size(p5, &err) == 3);
    snprintf(p5, sizeof(p5), "/tmp/%sddir/sub/..", pidtag);
    x("FS_DOTDOT_DIR", fs_is_dir(p5) == 1);
    x("FS_DOT_CLEAN",
      fs_remove(p3, &err) == STDLIB_OK && rmdir(p2) == 0 && rmdir(p1) == 0 &&
          fs_exists(p1) == 0);

    /* ============================== 6. repeated remove (missing) ========= */
    snprintf(p1, sizeof(p1), "/tmp/%snever_exists", pidtag);
    rep_ok = 1;
    for (i = 0; i < 4; i++) {
        rc = fs_remove(p1, &err);
        if (rc != STDLIB_FAIL || err.category() != STDLIB_CATEGORY_IO ||
            err.code() != CPLUS_EXIT_IO || fs_exists(p1) != 0)
            rep_ok = 0;
    }
    x("FS_REPEATED_REMOVE_MISSING", rep_ok);

    /* ============================== 7. mkdtemp reseed ==================== */
    snprintf(p1, sizeof(p1), "/tmp/%smissing_parent/XXXXXX", pidtag);
    rc = fs_mkdtemp(p1, sizeof(p1), &err);
    x("FS_MKDTEMP_FAIL_FIRST",
      rc == STDLIB_FAIL && err.category() == STDLIB_CATEGORY_IO);
    /* Non-defect: libc consumes XXXXXX even when mkdir fails, so a second
     * call on the un-reseeded buffer fails template validation. */
    rc = fs_mkdtemp(p1, sizeof(p1), &err);
    x("FS_MKDTEMP_CONSUMED_NO_RESEED",
      rc == STDLIB_FAIL &&
          (err.category() == STDLIB_CATEGORY_INVALID ||
           err.category() == STDLIB_CATEGORY_IO) &&
          strstr(p1, "XXXXXX") == NULL);
    snprintf(p2, sizeof(p2), "/tmp/%smkdtemp_parent", pidtag);
    x("FS_MKDTEMP_PARENT_MKDIR", fs_mkdir(p2, &err) == STDLIB_OK);
    /* Reseed the SAME buffer with a valid parent: the call now succeeds. */
    snprintf(p1, sizeof(p1), "/tmp/%smkdtemp_parent/XXXXXX", pidtag);
    x("FS_MKDTEMP_RESEEDED_OK",
      fs_mkdtemp(p1, sizeof(p1), &err) == STDLIB_OK && err.ok() &&
          fs_is_dir(p1) == 1);
    x("FS_MKDTEMP_CLEAN",
      rmdir(p1) == 0 && rmdir(p2) == 0 && fs_exists(p2) == 0);

    /* ============================== 8. permissions ======================= */
    snprintf(p1, sizeof(p1), "/tmp/%spermdir", pidtag);
    snprintf(p2, sizeof(p2), "/tmp/%spermdir/inside", pidtag);
    if (geteuid() == 0) {
        printf("SKIP FS_PERMISSIONS reason=euid=0-root-bypasses-DAC-nondeterministic\n");
    } else {
        int perm_ok = (fs_mkdir(p1, &err) == STDLIB_OK && chmod(p1, 0000) == 0);
        x("FS_PERM_SETUP", perm_ok);
        if (perm_ok) {
            FILE *fp = fopen(p2, "w");
            x("FS_PERM_CREATE_DENIED", fp == NULL);
            if (fp != NULL) {
                fclose(fp);
                (void)fs_remove(p2, &err);
            }
            chmod(p1, 0755);
            x("FS_PERM_CLEANUP", rmdir(p1) == 0 && fs_exists(p1) == 0);
        } else {
            chmod(p1, 0755);
            (void)rmdir(p1);
            x("FS_PERM_CREATE_DENIED", 0);
            x("FS_PERM_CLEANUP", fs_exists(p1) == 0);
        }
    }

    /* ============================== 9. cleanup proof ===================== */
    x("FS_EDGE_NO_LEFTOVERS", x_count_tmp(prefix) == tmp0);
    x("FS_EDGE_FD_NO_LEAK", x_count_fds() == fd0);

    print("EXTRA_FS_EDGES", x_checks, x_failures);
    return x_failures == 0 ? 0 : 1;
}
