/* Phase-3 filesystem safety -- permission-denied paths.
 *
 * A directory is created with fs_mkdir() then chmod(dir, 0000).  As a
 * non-root euid, file creation, nested fs_mkdir, fs_mkdtemp and fs_remove
 * inside it must all return the documented IO failure, and the original
 * directory must remain intact.  When geteuid() == 0 the result is
 * nondeterministic (root bypasses DAC), so an explicit
 *   SKIP FS_PERMISSIONS reason=euid=0-root-bypasses-DAC-nondeterministic
 * is printed instead of forcing a pass; the skip is not counted.
 *
 * Fixture is PID-unique, permissions are always restored before cleanup, and
 * baseline/final /dev/fd and /tmp-prefix counts must match.  Protocol:
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

static int fsp_checks = 0;
static int fsp_failures = 0;

static void fsp(const char *tag, int cond) {
    fsp_checks++;
    if (!cond)
        fsp_failures++;
    print(tag, cond ? 1 : 0);
}

static int fsp_count_fds(void) {
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

static int fsp_count_tmp(const char *prefix) {
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
    error fsp_err = error();
    char fsp_pid[32];
    char fsp_prefix[64];
    char fsp_dir[160];
    char fsp_child[220];
    char fsp_subdir[220];
    char fsp_tmpl[220];
    char fsp_victim[220];
    int fsp_fd0, fsp_tmp0, fsp_rc, fsp_ok;
    FILE *fsp_fp;

    snprintf(fsp_pid, sizeof(fsp_pid), "%d", (int)getpid());
    snprintf(fsp_prefix, sizeof(fsp_prefix), "cplus_fsp_%s_", fsp_pid);
    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();

    fsp_fd0 = fsp_count_fds();
    fsp_tmp0 = fsp_count_tmp(fsp_prefix);
    fsp("FSP_BASELINE", fsp_fd0 >= 0 && fsp_tmp0 == 0);

    snprintf(fsp_dir, sizeof(fsp_dir), "/tmp/%spermdir", fsp_pid);
    snprintf(fsp_child, sizeof(fsp_child), "%s/inside.txt", fsp_dir);
    snprintf(fsp_subdir, sizeof(fsp_subdir), "%s/subdir", fsp_dir);
    snprintf(fsp_tmpl, sizeof(fsp_tmpl), "%s/XXXXXX", fsp_dir);
    snprintf(fsp_victim, sizeof(fsp_victim), "%s/absent.txt", fsp_dir);

    if (geteuid() == 0) {
        printf("SKIP FS_PERMISSIONS reason=euid=0-root-bypasses-DAC-nondeterministic\n");
        printf("SKIP_REASON FS_PERMISSIONS reason=euid=0-root-bypasses-DAC-nondeterministic\n");
        fsp("FSP_ROOT_NO_FIXTURE", fsp_count_tmp(fsp_prefix) == 0);
    } else {
        fsp_ok = fs_mkdir(fsp_dir, &fsp_err) == STDLIB_OK &&
                 fs_is_dir(fsp_dir) == 1 && chmod(fsp_dir, 0000) == 0;
        fsp("FSP_SETUP", fsp_ok);
        if (fsp_ok) {
            /* The directory object itself is still queryable. */
            fsp("FSP_DIR_STILL_VISIBLE", fs_is_dir(fsp_dir) == 1);

            fsp_fp = fopen(fsp_child, "wb");
            fsp("FSP_FOPEN_DENIED", fsp_fp == NULL);
            if (fsp_fp != NULL) {
                fclose(fsp_fp);
                (void)fs_remove(fsp_child, &fsp_err);
            }

            fsp_rc = fs_mkdir(fsp_subdir, &fsp_err);
            fsp("FSP_MKDIR_DENIED",
                fsp_rc == STDLIB_FAIL &&
                    fsp_err.category() == STDLIB_CATEGORY_IO &&
                    fsp_err.code() == CPLUS_EXIT_IO &&
                    fs_is_dir(fsp_subdir) == 0);

            fsp_rc = fs_mkdtemp(fsp_tmpl, sizeof(fsp_tmpl), &fsp_err);
            fsp("FSP_MKDTEMP_DENIED",
                fsp_rc == STDLIB_FAIL &&
                    fsp_err.category() == STDLIB_CATEGORY_IO &&
                    fs_exists(fsp_subdir) == 0);

            fsp_rc = fs_remove(fsp_victim, &fsp_err);
            fsp("FSP_REMOVE_DENIED",
                fsp_rc == STDLIB_FAIL &&
                    fsp_err.category() == STDLIB_CATEGORY_IO);

            chmod(fsp_dir, 0755);
            fsp_fp = fopen(fsp_child, "wb");
            fsp("FSP_ACCESS_RESTORED", fsp_fp != NULL);
            if (fsp_fp != NULL) {
                fclose(fsp_fp);
                fsp_rc = fs_remove(fsp_child, &fsp_err);
                fsp("FSP_RESTORED_FILE_CLEAN",
                    fsp_rc == STDLIB_OK && fs_exists(fsp_child) == 0);
            } else {
                fsp("FSP_RESTORED_FILE_CLEAN", 0);
            }
            fsp("FSP_DIR_CLEAN",
                rmdir(fsp_dir) == 0 && fs_exists(fsp_dir) == 0);
        } else {
            chmod(fsp_dir, 0755);
            (void)rmdir(fsp_dir);
            fsp("FSP_DIR_STILL_VISIBLE", 0);
            fsp("FSP_FOPEN_DENIED", 0);
            fsp("FSP_MKDIR_DENIED", 0);
            fsp("FSP_MKDTEMP_DENIED", 0);
            fsp("FSP_REMOVE_DENIED", 0);
            fsp("FSP_ACCESS_RESTORED", 0);
            fsp("FSP_RESTORED_FILE_CLEAN", 0);
            fsp("FSP_DIR_CLEAN", fs_exists(fsp_dir) == 0);
        }
    }

    fsp("FSP_NO_LEFTOVERS", fsp_count_tmp(fsp_prefix) == 0);
    fsp("FSP_FD_NO_LEAK", fsp_count_fds() == fsp_fd0);
    print("FS_SAFETY", fsp_checks, fsp_failures);
    return fsp_failures == 0 ? 0 : 1;
}
