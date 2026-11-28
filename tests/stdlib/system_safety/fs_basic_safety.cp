/* Phase-3 filesystem safety -- basic path/validation surface of stdlib/fs.hp.
 *
 * Scope (no overlap claimed with the agent2 deep suites):
 *   - missing path queries and fallible operations on an absent entry;
 *   - NULL paths on every query and every fallible helper, err == NULL;
 *   - empty string and embedded-NUL paths (explicit-length __builtin_memcpy);
 *   - relative vs absolute path resolution through chdir()/getcwd();
 *   - boundary component lengths: NAME_MAX succeeds, NAME_MAX+1 is IO;
 *   - directory iteration with opendir()/readdir()/closedir();
 *   - fs_mkdtemp() temp directory plus file, and full cleanup proof.
 *
 * All fixtures use a PID-unique /tmp prefix.  Baseline and final /dev/fd and
 * /tmp-prefix counts must match, so no descriptor or filesystem leak is left.
 *
 * Protocol: each check prints "<TAG> <1|0>"; last line is
 *   FS_SAFETY <checks> <failures>
 * Exit code is 0 only when failures == 0.  No header is modified.
 */
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/fs.hp"

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif
#ifndef NAME_MAX
#define NAME_MAX 255
#endif

static int fsb_checks = 0;
static int fsb_failures = 0;

static void fsb(const char *tag, int cond) {
    fsb_checks++;
    if (!cond)
        fsb_failures++;
    print(tag, cond ? 1 : 0);
}

static int fsb_is_io(long long rc, error *e) {
    return rc == -1 && e->category() == STDLIB_CATEGORY_IO &&
           e->code() == CPLUS_EXIT_IO;
}

static int fsb_is_io_fail(int rc, error *e) {
    return rc == STDLIB_FAIL && e->category() == STDLIB_CATEGORY_IO &&
           e->code() == CPLUS_EXIT_IO;
}

static int fsb_write_file(const char *path, const char *data, int n) {
    FILE *fp = fopen(path, "wb");
    int ok = 0;
    if (fp != NULL) {
        ok = (n == 0 || fwrite(data, 1, (size_t)n, fp) == (size_t)n);
        if (fclose(fp) != 0)
            ok = 0;
    }
    return ok;
}

static int fsb_count_fds(void) {
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

static int fsb_count_tmp(const char *prefix) {
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
    error fsb_err = error();
    char fsb_pid[32];
    char fsb_prefix[64];
    char fsb_dir[160];
    char fsb_file[240];
    char fsb_missing[240];
    char fsb_cwd[PATH_MAX + 64];
    char fsb_cwd_again[PATH_MAX + 64];
    char fsb_nulsrc[200];
    char fsb_nulpath[200];
    char fsb_name[300];
    char fsb_bound[420];
    char fsb_toolong[420];
    char fsb_tmpl[160];
    char fsb_inner[240];
    size_t fsb_len;
    int fsb_fd0, fsb_tmp0, fsb_rc, fsb_ok, fsb_n;
    int fsb_iter_open;
    DIR *fsb_iter;
    struct dirent *fsb_entry;

    snprintf(fsb_pid, sizeof(fsb_pid), "%d", (int)getpid());
    snprintf(fsb_prefix, sizeof(fsb_prefix), "cplus_fsb_%s_", fsb_pid);
    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();

    fsb_fd0 = fsb_count_fds();
    fsb_tmp0 = fsb_count_tmp(fsb_prefix);
    fsb("FSB_BASELINE", fsb_fd0 >= 0 && fsb_tmp0 == 0);

    /* ---- missing file / directory --------------------------------------- */
    snprintf(fsb_dir, sizeof(fsb_dir), "/tmp/%sdir", fsb_pid);
    snprintf(fsb_file, sizeof(fsb_file), "%s/present.txt", fsb_dir);
    snprintf(fsb_missing, sizeof(fsb_missing), "%s/absent.txt", fsb_dir);

    fsb_rc = fs_mkdir(fsb_dir, &fsb_err);
    fsb("FSB_MKDIR", fsb_rc == STDLIB_OK && fsb_err.ok() &&
                         fs_is_dir(fsb_dir) == 1 && fs_is_file(fsb_dir) == 0);
    fsb("FSB_MISSING_QUERIES", fs_exists(fsb_missing) == 0 &&
                                   fs_is_file(fsb_missing) == 0 &&
                                   fs_is_dir(fsb_missing) == 0);
    fsb_rc = fs_size(fsb_missing, &fsb_err);
    fsb("FSB_MISSING_SIZE", fsb_is_io(fsb_rc, &fsb_err));
    fsb_rc = fs_remove(fsb_missing, &fsb_err);
    fsb("FSB_MISSING_REMOVE", fsb_is_io_fail(fsb_rc, &fsb_err));

    fsb("FSB_WRITE_FIXTURE", fsb_write_file(fsb_file, "abc", 3) == 1 &&
                                 fs_is_file(fsb_file) == 1 &&
                                 fs_size(fsb_file, &fsb_err) == 3);
    fsb_rc = fs_rename(fsb_missing, fsb_file, &fsb_err);
    fsb("FSB_MISSING_RENAME_SRC",
        fsb_is_io_fail(fsb_rc, &fsb_err) &&
            fs_is_file(fsb_file) == 1 && fs_size(fsb_file, &fsb_err) == 3);

    /* ---- NULL paths and NULL error pointer ------------------------------ */
    fsb("FSB_NULL_QUERIES", fs_exists(NULL) == 0 && fs_is_file(NULL) == 0 &&
                                fs_is_dir(NULL) == 0);
    fsb_ok = 0;
    fsb_rc = fs_size(NULL, &fsb_err);
    fsb_ok = fsb_rc == -1 && fsb_err.category() == STDLIB_CATEGORY_INVALID &&
             fsb_err.code() == CPLUS_EXIT_UNSUPPORTED;
    fsb_ok = fsb_ok && fs_remove(NULL, &fsb_err) == STDLIB_FAIL &&
             fsb_err.category() == STDLIB_CATEGORY_INVALID;
    fsb_ok = fsb_ok && fs_rename(NULL, fsb_file, &fsb_err) == STDLIB_FAIL &&
             fsb_err.category() == STDLIB_CATEGORY_INVALID;
    fsb_ok = fsb_ok && fs_rename(fsb_file, NULL, &fsb_err) == STDLIB_FAIL &&
             fsb_err.category() == STDLIB_CATEGORY_INVALID;
    fsb_ok = fsb_ok && fs_mkdir(NULL, &fsb_err) == STDLIB_FAIL &&
             fsb_err.category() == STDLIB_CATEGORY_INVALID;
    fsb_ok = fsb_ok && fs_mkdtemp(NULL, sizeof(fsb_tmpl), &fsb_err) ==
                           STDLIB_FAIL &&
             fsb_err.category() == STDLIB_CATEGORY_INVALID;
    fsb("FSB_NULL_OPS", fsb_ok);
    fsb("FSB_NULL_ERR_TOLERATED",
        fs_size(fsb_file, NULL) == 3 && fs_remove(fsb_missing, NULL) ==
                                            STDLIB_FAIL &&
            fs_mkdir("", NULL) == STDLIB_FAIL);

    /* ---- empty string --------------------------------------------------- */
    fsb("FSB_EMPTY_QUERIES", fs_exists("") == 0 && fs_is_file("") == 0 &&
                                 fs_is_dir("") == 0);
    fsb_ok = 0;
    fsb_rc = fs_size("", &fsb_err);
    fsb_ok = fsb_rc == -1 && fsb_err.category() == STDLIB_CATEGORY_IO;
    fsb_ok = fsb_ok && fs_remove("", &fsb_err) == STDLIB_FAIL &&
             fsb_err.category() == STDLIB_CATEGORY_IO;
    fsb_ok = fsb_ok && fs_mkdir("", &fsb_err) == STDLIB_FAIL &&
             fsb_err.category() == STDLIB_CATEGORY_IO;
    fsb_ok = fsb_ok && fs_rename("", fsb_file, &fsb_err) == STDLIB_FAIL &&
             fsb_err.category() == STDLIB_CATEGORY_IO;
    fsb("FSB_EMPTY_OPS", fsb_ok);

    /* ---- embedded NUL: syscalls truncate at the first NUL --------------- */
    snprintf(fsb_nulsrc, sizeof(fsb_nulsrc), "%s/nultrunc.txt", fsb_dir);
    memset(fsb_nulpath, 0, sizeof(fsb_nulpath));
    fsb_len = strlen(fsb_nulsrc);
    __builtin_memcpy(fsb_nulpath, fsb_nulsrc, fsb_len);
    __builtin_memcpy(fsb_nulpath + fsb_len + 1, "IGNORED", 7);
    fsb("FSB_NUL_TRUNCATION",
        fs_exists(fsb_nulpath) == 0 && fs_size(fsb_nulpath, &fsb_err) == -1 &&
            fsb_err.category() == STDLIB_CATEGORY_IO);
    fsb("FSB_NUL_FIXTURE", fsb_write_file(fsb_nulsrc, "xyz", 3) == 1);
    fsb("FSB_NUL_TRUNC_VISIBLE",
        fs_exists(fsb_nulpath) == 1 && fs_size(fsb_nulpath, &fsb_err) == 3);
    fsb_rc = fs_remove(fsb_nulpath, &fsb_err);
    fsb("FSB_NUL_TRUNC_CLEAN", fsb_rc == STDLIB_OK &&
                                  fs_exists(fsb_nulsrc) == 0);

    /* ---- relative vs absolute resolution -------------------------------- */
    if (getcwd(fsb_cwd, sizeof(fsb_cwd)) != NULL) {
        fsb_rc = chdir(fsb_dir);
        fsb("FSB_CHDIR_IN", fsb_rc == 0);
        fsb("FSB_RELATIVE_QUERY", fs_exists("present.txt") == 1 &&
                                    fs_is_file("present.txt") == 1 &&
                                    fs_size("present.txt", &fsb_err) == 3);
        fsb("FSB_ABSOLUTE_WHILE_RELATIVE",
            fs_is_file(fsb_file) == 1 && fs_size(fsb_file, &fsb_err) == 3);
        fsb_rc = chdir(fsb_cwd);
        fsb("FSB_CHDIR_OUT", fsb_rc == 0);
        fsb("FSB_CWD_RESTORED",
            getcwd(fsb_cwd_again, sizeof(fsb_cwd_again)) != NULL &&
                strcmp(fsb_cwd, fsb_cwd_again) == 0);
    } else {
        fsb("FSB_GETCWD", 0);
    }

    /* ---- boundary component lengths ------------------------------------- */
    memset(fsb_name, 'x', NAME_MAX);
    fsb_name[NAME_MAX] = '\0';
    snprintf(fsb_bound, sizeof(fsb_bound), "%s/", fsb_dir);
    fsb_len = strlen(fsb_bound);
    __builtin_memcpy(fsb_bound + fsb_len, fsb_name, (size_t)NAME_MAX + 1);
    fsb("FSB_NAME_MAX_LENGTH", strlen(fsb_name) == (size_t)NAME_MAX);
    fsb("FSB_NAME_MAX_CREATE",
        fsb_write_file(fsb_bound, "b", 1) == 1 &&
            fs_is_file(fsb_bound) == 1);
    fsb_rc = fs_remove(fsb_bound, &fsb_err);
    fsb("FSB_NAME_MAX_REMOVE", fsb_rc == STDLIB_OK &&
                                  fs_exists(fsb_bound) == 0);

    memset(fsb_name, 'y', NAME_MAX + 1);
    fsb_name[NAME_MAX + 1] = '\0';
    snprintf(fsb_toolong, sizeof(fsb_toolong), "%s/", fsb_dir);
    fsb_len = strlen(fsb_toolong);
    __builtin_memcpy(fsb_toolong + fsb_len, fsb_name, (size_t)NAME_MAX + 2);
    fsb("FSB_NAME_MAX_PLUS_ONE_LENGTH",
        strlen(fsb_name) == (size_t)(NAME_MAX + 1));
    fsb_rc = fs_size(fsb_toolong, &fsb_err);
    fsb("FSB_NAME_MAX_PLUS_ONE_SIZE", fsb_rc == -1 &&
                                         fsb_err.category() ==
                                             STDLIB_CATEGORY_IO);
    fsb_rc = fs_mkdir(fsb_toolong, &fsb_err);
    fsb("FSB_NAME_MAX_PLUS_ONE_MKDIR", fsb_rc == STDLIB_FAIL &&
                                         fsb_err.category() ==
                                             STDLIB_CATEGORY_IO);

    /* ---- directory iteration via existing APIs -------------------------- */
    fsb_iter = opendir(fsb_dir);
    fsb_iter_open = fsb_iter != NULL;
    fsb_n = 0;
    if (fsb_iter_open) {
        while ((fsb_entry = readdir(fsb_iter)) != NULL) {
            if (strcmp(fsb_entry->d_name, "present.txt") == 0)
                fsb_n++;
        }
        closedir(fsb_iter);
    }
    fsb("FSB_ITERATE", fsb_iter_open && fsb_n == 1);

    /* ---- fs_mkdtemp and temp file cleanup ------------------------------- */
    snprintf(fsb_tmpl, sizeof(fsb_tmpl), "/tmp/%stmp_XXXXXX", fsb_pid);
    fsb_rc = fs_mkdtemp(fsb_tmpl, sizeof(fsb_tmpl), &fsb_err);
    fsb("FSB_MKDTEMP", fsb_rc == STDLIB_OK && fsb_err.ok() &&
                           fs_is_dir(fsb_tmpl) == 1);
    snprintf(fsb_inner, sizeof(fsb_inner), "%s/inner.txt", fsb_tmpl);
    fsb("FSB_MKDTEMP_INNER", fsb_write_file(fsb_inner, "z", 1) == 1 &&
                                fs_size(fsb_inner, &fsb_err) == 1);
    fsb_rc = fs_remove(fsb_inner, &fsb_err);
    fsb("FSB_TMP_FILE_CLEAN", fsb_rc == STDLIB_OK &&
                                 fs_exists(fsb_inner) == 0);
    fsb("FSB_TMP_DIR_CLEAN",
        rmdir(fsb_tmpl) == 0 && fs_exists(fsb_tmpl) == 0);

    /* ---- final cleanup and leak assertions ------------------------------ */
    fsb_rc = fs_remove(fsb_file, &fsb_err);
    fsb("FSB_FINAL_FILE_CLEAN", fsb_rc == STDLIB_OK &&
                                    fs_exists(fsb_file) == 0);
    fsb("FSB_FINAL_DIR_CLEAN",
        rmdir(fsb_dir) == 0 && fs_exists(fsb_dir) == 0);
    fsb("FSB_NO_LEFTOVERS", fsb_count_tmp(fsb_prefix) == 0);
    fsb("FSB_FD_NO_LEAK", fsb_count_fds() == fsb_fd0);

    print("FS_SAFETY", fsb_checks, fsb_failures);
    return fsb_failures == 0 ? 0 : 1;
}
