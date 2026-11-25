/* Phase-3 filesystem safety -- symlinks and path traversal components.
 *
 * fs.hp uses stat() (follows links) and unlink() (removes the link, not the
 * target).  This suite characterises both for links to a file, to a directory
 * and to a missing target (dangling), then checks that '.' and '..' path
 * components resolve exactly as the POSIX syscalls do -- no normalisation
 * is added or expected from fs.hp.
 *
 * PID-unique /tmp fixture; every link/file/directory is removed and baseline
 * vs final /dev/fd and /tmp-prefix counts must match.  Protocol:
 * "<TAG> <1|0>" lines, final "FS_SAFETY <checks> <failures>", exit 0 only
 * when failures == 0.  No header changes.
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

static int fss_checks = 0;
static int fss_failures = 0;

static void fss(const char *tag, int cond) {
    fss_checks++;
    if (!cond)
        fss_failures++;
    print(tag, cond ? 1 : 0);
}

static int fss_write_file(const char *path, const char *data, int n) {
    FILE *fp = fopen(path, "wb");
    int ok = 0;
    if (fp != NULL) {
        ok = (n == 0 || fwrite(data, 1, (size_t)n, fp) == (size_t)n);
        if (fclose(fp) != 0)
            ok = 0;
    }
    return ok;
}

static int fss_lstat_ok(const char *path) {
    struct stat info;
    return lstat(path, &info) == 0;
}

static int fss_count_fds(void) {
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

static int fss_count_tmp(const char *prefix) {
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
    error fss_err = error();
    char fss_pid[32];
    char fss_prefix[64];
    char fss_dir[160];
    char fss_sub[220];
    char fss_target[220];
    char fss_link[220];
    char fss_linkdir[220];
    char fss_dangle[220];
    char fss_missing[220];
    char fss_leaf[260];
    char fss_dot[300];
    char fss_dotdot[300];
    char fss_linkbuf[260];
    ssize_t fss_nread;
    int fss_fd0, fss_tmp0, fss_rc;

    snprintf(fss_pid, sizeof(fss_pid), "%d", (int)getpid());
    snprintf(fss_prefix, sizeof(fss_prefix), "cplus_fss_%s_", fss_pid);
    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();

    fss_fd0 = fss_count_fds();
    fss_tmp0 = fss_count_tmp(fss_prefix);
    fss("FSS_BASELINE", fss_fd0 >= 0 && fss_tmp0 == 0);

    snprintf(fss_dir, sizeof(fss_dir), "/tmp/%sroot", fss_pid);
    snprintf(fss_sub, sizeof(fss_sub), "%s/sub", fss_dir);
    fss_rc = fs_mkdir(fss_dir, &fss_err);
    fss("FSS_MKDIR_ROOT", fss_rc == STDLIB_OK && fs_is_dir(fss_dir) == 1);
    fss_rc = fs_mkdir(fss_sub, &fss_err);
    fss("FSS_MKDIR_SUB", fss_rc == STDLIB_OK && fs_is_dir(fss_sub) == 1);

    /* ---- symlink to a regular file -------------------------------------- */
    snprintf(fss_target, sizeof(fss_target), "%s/target.txt", fss_dir);
    snprintf(fss_link, sizeof(fss_link), "%s/link_file", fss_dir);
    fss("FSS_FILE_FIXTURE",
        fss_write_file(fss_target, "TARGET", 6) == 1 &&
            fs_size(fss_target, &fss_err) == 6);
    fss("FSS_FILE_LINK_SETUP", symlink(fss_target, fss_link) == 0 &&
                                    fss_lstat_ok(fss_link));
    fss("FSS_FILE_LINK_QUERIES", fs_exists(fss_link) == 1 &&
                                    fs_is_file(fss_link) == 1 &&
                                    fs_is_dir(fss_link) == 0 &&
                                    fs_size(fss_link, &fss_err) == 6);
    memset(fss_linkbuf, 0, sizeof(fss_linkbuf));
    fss_nread = readlink(fss_link, fss_linkbuf, sizeof(fss_linkbuf) - 1);
    fss("FSS_FILE_READLINK",
        fss_nread > 0 && strcmp(fss_linkbuf, fss_target) == 0);
    fss_rc = fs_remove(fss_link, &fss_err);
    fss("FSS_FILE_LINK_REMOVE", fss_rc == STDLIB_OK &&
                                   fss_lstat_ok(fss_link) == 0);
    fss("FSS_FILE_TARGET_INTACT", fs_is_file(fss_target) == 1 &&
                                     fs_size(fss_target, &fss_err) == 6);

    /* ---- symlink to a directory ----------------------------------------- */
    snprintf(fss_linkdir, sizeof(fss_linkdir), "%s/link_dir", fss_dir);
    fss("FSS_DIR_LINK_SETUP", symlink(fss_sub, fss_linkdir) == 0 &&
                                  fss_lstat_ok(fss_linkdir));
    fss("FSS_DIR_LINK_QUERIES", fs_exists(fss_linkdir) == 1 &&
                                   fs_is_dir(fss_linkdir) == 1 &&
                                   fs_is_file(fss_linkdir) == 0);
    fss("FSS_DIR_SIZE_DEFINED", fs_size(fss_linkdir, &fss_err) >= 0);
    fss_rc = fs_remove(fss_linkdir, &fss_err);
    fss("FSS_DIR_LINK_REMOVE", fss_rc == STDLIB_OK &&
                                  fss_lstat_ok(fss_linkdir) == 0);
    fss("FSS_DIR_TARGET_INTACT", fs_is_dir(fss_sub) == 1);

    /* ---- dangling symlink ------------------------------------------------ */
    snprintf(fss_missing, sizeof(fss_missing), "%s/no_such_target", fss_dir);
    snprintf(fss_dangle, sizeof(fss_dangle), "%s/dangling", fss_dir);
    fss("FSS_DANGLING_SETUP",
        fs_exists(fss_missing) == 0 &&
            symlink(fss_missing, fss_dangle) == 0 &&
            fss_lstat_ok(fss_dangle));
    fss("FSS_DANGLING_QUERIES", fs_exists(fss_dangle) == 0 &&
                                   fs_is_file(fss_dangle) == 0 &&
                                   fs_is_dir(fss_dangle) == 0);
    fss_rc = (int)fs_size(fss_dangle, &fss_err);
    fss("FSS_DANGLING_SIZE", fss_rc == -1 &&
                                fss_err.category() == STDLIB_CATEGORY_IO &&
                                fss_err.code() == CPLUS_EXIT_IO);
    fss_rc = fs_remove(fss_dangle, &fss_err);
    fss("FSS_DANGLING_REMOVE", fss_rc == STDLIB_OK &&
                                  fss_lstat_ok(fss_dangle) == 0);

    /* ---- traversal components: documented OS normalisation -------------- */
    snprintf(fss_leaf, sizeof(fss_leaf), "%s/trav.txt", fss_sub);
    fss("FSS_TRAV_FIXTURE", fss_write_file(fss_leaf, "TRAV", 4) == 1);
    snprintf(fss_dot, sizeof(fss_dot), "%s/./sub/trav.txt", fss_dir);
    snprintf(fss_dotdot, sizeof(fss_dotdot), "%s/sub/../sub/trav.txt",
             fss_dir);
    fss("FSS_DOT_COMPONENT", fs_is_file(fss_dot) == 1 &&
                                fs_size(fss_dot, &fss_err) == 4);
    fss("FSS_DOTDOT_COMPONENT", fs_is_file(fss_dotdot) == 1 &&
                                   fs_size(fss_dotdot, &fss_err) == 4);
    fss("FSS_DOTDOT_DIR", fs_is_dir(fss_dotdot) == 0 &&
                             fs_is_dir(fss_sub) == 1);
    fss("FSS_TRAV_SAME_FILE",
        fs_size(fss_dot, &fss_err) == fs_size(fss_leaf, &fss_err));
    fss_rc = fs_remove(fss_dot, &fss_err);
    fss("FSS_TRAV_REMOVE", fss_rc == STDLIB_OK &&
                              fs_exists(fss_leaf) == 0 &&
                              fs_exists(fss_dotdot) == 0);

    /* ---- fs_remove is unlink-only: a directory is not removable --------- */
    fss_rc = fs_remove(fss_sub, &fss_err);
    fss("FSS_REMOVE_DIR_FAILS", fss_rc == STDLIB_FAIL &&
                                   fss_err.category() == STDLIB_CATEGORY_IO &&
                                   fs_is_dir(fss_sub) == 1);

    /* ---- cleanup and leak proof ----------------------------------------- */
    fss_rc = fs_remove(fss_target, &fss_err);
    fss("FSS_TARGET_CLEAN", fss_rc == STDLIB_OK &&
                               fs_exists(fss_target) == 0);
    fss("FSS_DIRS_CLEAN", rmdir(fss_sub) == 0 && rmdir(fss_dir) == 0 &&
                             fs_exists(fss_dir) == 0);
    fss("FSS_NO_LEFTOVERS", fss_count_tmp(fss_prefix) == 0);
    fss("FSS_FD_NO_LEAK", fss_count_fds() == fss_fd0);
    print("FS_SAFETY", fss_checks, fss_failures);
    return fss_failures == 0 ? 0 : 1;
}
