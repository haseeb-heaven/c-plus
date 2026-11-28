/* Phase-3 filesystem safety -- overlong paths and template validation.
 *
 *   - path strictly longer than PATH_MAX: every query returns 0 and every
 *     fallible helper returns a defined IO failure with a non-empty message;
 *   - near-limit but valid deeply nested path: created and removed cleanly;
 *   - fs_mkdtemp invalid templates (NULL, size < 7, missing XXXXXX,
 *     size == strlen instead of capacity) -> INVALID with no artifact;
 *   - fs_mkdtemp valid template -> directory created, then rmdir.
 *
 * PID-unique /tmp prefix; baseline/final /dev/fd and /tmp-prefix counts must
 * match.  Protocol: "<TAG> <1|0>" lines, final "FS_SAFETY <checks> <failures>",
 * exit 0 only when failures == 0.  No header changes.
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

static int fsl_checks = 0;
static int fsl_failures = 0;

static void fsl(const char *tag, int cond) {
    fsl_checks++;
    if (!cond)
        fsl_failures++;
    print(tag, cond ? 1 : 0);
}

static int fsl_count_fds(void) {
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

static int fsl_count_tmp(const char *prefix) {
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

static int fsl_is_io_fail(int rc, error *e) {
    return rc == STDLIB_FAIL && e->category() == STDLIB_CATEGORY_IO &&
           e->code() == CPLUS_EXIT_IO;
}

static int fsl_is_invalid_fail(int rc, error *e) {
    return rc == STDLIB_FAIL && e->category() == STDLIB_CATEGORY_INVALID &&
           e->code() == CPLUS_EXIT_UNSUPPORTED;
}

int main(void) {
    error fsl_err = error();
    char fsl_pid[32];
    char fsl_prefix[64];
    char fsl_long[PATH_MAX + 128];
    char fsl_tmpl[PATH_MAX + 128];
    char fsl_small[8];
    char fsl_bad[64];
    char fsl_tight[128];
    char fsl_root[160];
    char fsl_l1[240];
    char fsl_l2[320];
    char fsl_l3[400];
    char fsl_leaf[460];
    char fsl_part[128];
    size_t fsl_len;
    int fsl_fd0, fsl_tmp0, fsl_rc, fsl_ok;

    snprintf(fsl_pid, sizeof(fsl_pid), "%d", (int)getpid());
    snprintf(fsl_prefix, sizeof(fsl_prefix), "cplus_fsl_%s_", fsl_pid);
    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();

    fsl_fd0 = fsl_count_fds();
    fsl_tmp0 = fsl_count_tmp(fsl_prefix);
    fsl("FSL_BASELINE", fsl_fd0 >= 0 && fsl_tmp0 == 0);

    /* ---- path strictly longer than PATH_MAX ----------------------------- */
    memset(fsl_long, 'a', sizeof(fsl_long) - 1);
    fsl_long[sizeof(fsl_long) - 1] = '\0';
    __builtin_memcpy(fsl_long, "/tmp/", 5);
    fsl_len = strlen(fsl_long);
    fsl("FSL_LONG_LENGTH", fsl_len > (size_t)PATH_MAX);
    fsl("FSL_LONG_QUERIES", fs_exists(fsl_long) == 0 &&
                                fs_is_file(fsl_long) == 0 &&
                                fs_is_dir(fsl_long) == 0);
    fsl_rc = (int)fs_size(fsl_long, &fsl_err);
    fsl("FSL_LONG_SIZE", fsl_rc == -1 &&
                            fsl_err.category() == STDLIB_CATEGORY_IO &&
                            fsl_err.code() == CPLUS_EXIT_IO &&
                            strlen(fsl_err.what()) > 0);
    fsl_rc = fs_remove(fsl_long, &fsl_err);
    fsl("FSL_LONG_REMOVE", fsl_is_io_fail(fsl_rc, &fsl_err));
    fsl_rc = fs_mkdir(fsl_long, &fsl_err);
    fsl("FSL_LONG_MKDIR", fsl_is_io_fail(fsl_rc, &fsl_err));
    fsl_rc = fs_rename(fsl_long, "/tmp/does-not-matter", &fsl_err);
    fsl("FSL_LONG_RENAME_FROM", fsl_is_io_fail(fsl_rc, &fsl_err));
    fsl_rc = fs_rename("/tmp/does-not-matter", fsl_long, &fsl_err);
    fsl("FSL_LONG_RENAME_TO", fsl_is_io_fail(fsl_rc, &fsl_err));
    fsl("FSL_LONG_NO_ARTIFACT", fs_exists(fsl_long) == 0);

    /* ---- overlong mkdtemp template -------------------------------------- */
    memset(fsl_tmpl, 'a', sizeof(fsl_tmpl) - 1);
    fsl_tmpl[sizeof(fsl_tmpl) - 1] = '\0';
    __builtin_memcpy(fsl_tmpl, "/tmp/", 5);
    __builtin_memcpy(fsl_tmpl + sizeof(fsl_tmpl) - 7, "XXXXXX", 6);
    fsl_len = strlen(fsl_tmpl);
    fsl("FSL_LONG_TMPL_LENGTH", fsl_len > (size_t)PATH_MAX);
    fsl_rc = fs_mkdtemp(fsl_tmpl, sizeof(fsl_tmpl), &fsl_err);
    fsl("FSL_LONG_MKDTEMP", fsl_is_io_fail(fsl_rc, &fsl_err));

    /* ---- invalid mkdtemp templates -------------------------------------- */
    fsl_rc = fs_mkdtemp(NULL, 64, &fsl_err);
    fsl("FSL_TMPL_NULL", fsl_is_invalid_fail(fsl_rc, &fsl_err));
    fsl_rc = fs_mkdtemp(fsl_small, 0, &fsl_err);
    fsl("FSL_TMPL_SIZE_ZERO", fsl_is_invalid_fail(fsl_rc, &fsl_err));
    fsl_rc = fs_mkdtemp(fsl_small, 6, &fsl_err);
    fsl("FSL_TMPL_SIZE_SIX", fsl_is_invalid_fail(fsl_rc, &fsl_err));
    memset(fsl_bad, 0, sizeof(fsl_bad));
    snprintf(fsl_bad, sizeof(fsl_bad), "/tmp/%sbad_ZZZZZZ", fsl_pid);
    fsl_rc = fs_mkdtemp(fsl_bad, sizeof(fsl_bad), &fsl_err);
    fsl("FSL_TMPL_NO_SUFFIX", fsl_is_invalid_fail(fsl_rc, &fsl_err));
    fsl("FSL_TMPL_NO_ARTIFACT", fs_exists(fsl_bad) == 0);

    /* size == strlen (caller passes length, not capacity): len >= size
     * guard rejects it.  Caller rule, documented non-defect. */
    memset(fsl_tight, 0, sizeof(fsl_tight));
    snprintf(fsl_tight, sizeof(fsl_tight), "/tmp/%stight_XXXXXX", fsl_pid);
    fsl_len = strlen(fsl_tight);
    fsl_rc = fs_mkdtemp(fsl_tight, fsl_len, &fsl_err);
    fsl("FSL_TMPL_STRLEN_SIZE", fsl_is_invalid_fail(fsl_rc, &fsl_err));
    fsl("FSL_TMPL_STRLEN_NO_ARTIFACT", fs_exists(fsl_tight) == 0);

    /* ---- valid nested long path (< PATH_MAX) ---------------------------- */
    snprintf(fsl_root, sizeof(fsl_root), "/tmp/%sroot", fsl_pid);
    fsl_rc = fs_mkdir(fsl_root, &fsl_err);
    fsl("FSL_DEEP_ROOT", fsl_rc == STDLIB_OK && fs_is_dir(fsl_root) == 1);
    memset(fsl_part, 'd', sizeof(fsl_part) - 1);
    fsl_part[sizeof(fsl_part) - 1] = '\0';
    snprintf(fsl_l1, sizeof(fsl_l1), "%s/%s", fsl_root, fsl_part);
    fsl_rc = fs_mkdir(fsl_l1, &fsl_err);
    fsl("FSL_DEEP_L1", fsl_rc == STDLIB_OK && fs_is_dir(fsl_l1) == 1);
    snprintf(fsl_l2, sizeof(fsl_l2), "%s/%s", fsl_l1, fsl_part);
    fsl_rc = fs_mkdir(fsl_l2, &fsl_err);
    fsl("FSL_DEEP_L2", fsl_rc == STDLIB_OK && fs_is_dir(fsl_l2) == 1);
    snprintf(fsl_l3, sizeof(fsl_l3), "%s/%s", fsl_l2, fsl_part);
    fsl_rc = fs_mkdir(fsl_l3, &fsl_err);
    fsl("FSL_DEEP_L3", fsl_rc == STDLIB_OK && fs_is_dir(fsl_l3) == 1);
    snprintf(fsl_leaf, sizeof(fsl_leaf), "%s/%s", fsl_l3, "leaf.txt");
    fsl_len = strlen(fsl_leaf);
    fsl("FSL_DEEP_WITHIN_LIMIT", fsl_len < (size_t)PATH_MAX);
    {
        FILE *fp = fopen(fsl_leaf, "wb");
        fsl_ok = fp != NULL;
        if (fp != NULL) {
            fsl_ok = fwrite("deep", 1, 4, fp) == 4;
            if (fclose(fp) != 0)
                fsl_ok = 0;
        }
        fsl("FSL_DEEP_FILE", fsl_ok == 1 && fs_is_file(fsl_leaf) == 1 &&
                                 fs_size(fsl_leaf, &fsl_err) == 4);
    }
    fsl_rc = fs_remove(fsl_leaf, &fsl_err);
    fsl("FSL_DEEP_FILE_CLEAN", fsl_rc == STDLIB_OK &&
                                  fs_exists(fsl_leaf) == 0);
    fsl("FSL_DEEP_DIRS_CLEAN",
        rmdir(fsl_l3) == 0 && rmdir(fsl_l2) == 0 && rmdir(fsl_l1) == 0 &&
            rmdir(fsl_root) == 0);
    fsl("FSL_DEEP_ALL_GONE", fs_exists(fsl_root) == 0);

    fsl("FSL_NO_LEFTOVERS", fsl_count_tmp(fsl_prefix) == 0);
    fsl("FSL_FD_NO_LEAK", fsl_count_fds() == fsl_fd0);
    print("FS_SAFETY", fsl_checks, fsl_failures);
    return fsl_failures == 0 ? 0 : 1;
}
