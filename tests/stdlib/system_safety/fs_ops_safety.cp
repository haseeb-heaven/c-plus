/* Phase-3 filesystem safety -- rename/delete failures, iteration, races.
 *
 *   - rename over an existing file replaces its bytes atomically; rename with
 *     a missing source or a missing destination parent leaves the target
 *     intact and reports IO;
 *   - repeated fs_remove of a missing path stays a defined IO failure;
 *   - directory iteration via opendir()/readdir() sees exactly the fixtures;
 *   - fs_mkdtemp() temp file cleanup is complete;
 *   - bounded file-creation race: four pthreads create distinct PID-unique
 *     files concurrently; the main thread joins all four and verifies them.
 *
 * All paths are PID-unique under /tmp.  Baseline/final /dev/fd and /tmp-prefix
 * counts must match.  Protocol: "<TAG> <1|0>" lines, final
 * "FS_SAFETY <checks> <failures>", exit 0 only when failures == 0.
 * No header changes.
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
#include "stdlib/thread.hp"

static int fso_checks = 0;
static int fso_failures = 0;

static void fso(const char *tag, int cond) {
    fso_checks++;
    if (!cond)
        fso_failures++;
    print(tag, cond ? 1 : 0);
}

static int fso_write_file(const char *path, const char *data, int n) {
    FILE *fp = fopen(path, "wb");
    int ok = 0;
    if (fp != NULL) {
        ok = (n == 0 || fwrite(data, 1, (size_t)n, fp) == (size_t)n);
        if (fclose(fp) != 0)
            ok = 0;
    }
    return ok;
}

static int fso_read_first(const char *path, char *out, int cap) {
    FILE *fp = fopen(path, "rb");
    int n = 0;
    if (fp != NULL) {
        n = (int)fread(out, 1, (size_t)(cap - 1), fp);
        out[n] = '\0';
        fclose(fp);
    }
    return n;
}

static int fso_count_fds(void) {
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

static int fso_count_tmp(const char *prefix) {
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

/* Static context: the opaque thread argument has process lifetime, so the
 * borrow heuristic cannot mistake it for a stack temporary. */
typedef struct {
    char dir[160];
    int index;
    int ok;
} fso_slot;

static fso_slot fso_slots[4];

static void *fso_worker(void *arg) {
    fso_slot *slot = (fso_slot *)arg;
    fso_slot *local_slot = slot;
    char fpath[220];
    FILE *fp;
    int local_ok = 0;
    snprintf(fpath, sizeof(fpath), "%s/race_%d.txt", local_slot->dir,
             local_slot->index);
    fp = fopen(fpath, "wb");
    if (fp != NULL) {
        local_ok = fwrite("race", 1, 4, fp) == 4;
        if (fclose(fp) != 0)
            local_ok = 0;
    }
    local_slot->ok = local_ok;
    return NULL;
}

int main(void) {
    error fso_err = error();
    char fso_pid[32];
    char fso_prefix[64];
    char fso_dir[160];
    char fso_a[220];
    char fso_b[220];
    char fso_badparent[260];
    char fso_iter[240];
    char fso_tmpl[160];
    char fso_inner[220];
    char fso_racepath[220];
    char fso_buf[64];
    int fso_fd0, fso_tmp0, fso_rc, fso_ok, fso_n, fso_i;
    int fso_iter_open;
    DIR *fso_iterdir;
    struct dirent *fso_entry;
    void *fso_join_result;
    thread fso_thread0 = thread();
    thread fso_thread1 = thread();
    thread fso_thread2 = thread();
    thread fso_thread3 = thread();

    snprintf(fso_pid, sizeof(fso_pid), "%d", (int)getpid());
    snprintf(fso_prefix, sizeof(fso_prefix), "cplus_fso_%s_", fso_pid);
    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();

    fso_fd0 = fso_count_fds();
    fso_tmp0 = fso_count_tmp(fso_prefix);
    fso("FSO_BASELINE", fso_fd0 >= 0 && fso_tmp0 == 0);

    snprintf(fso_dir, sizeof(fso_dir), "/tmp/%sroot", fso_pid);
    snprintf(fso_a, sizeof(fso_a), "%s/a.txt", fso_dir);
    snprintf(fso_b, sizeof(fso_b), "%s/b.txt", fso_dir);
    fso_rc = fs_mkdir(fso_dir, &fso_err);
    fso("FSO_MKDIR", fso_rc == STDLIB_OK && fs_is_dir(fso_dir) == 1);
    fso("FSO_FIXTURES", fso_write_file(fso_a, "OLDLONGCONTENT", 14) == 1 &&
                           fso_write_file(fso_b, "NEW", 3) == 1);

    /* ---- rename over an existing file replaces content ------------------ */
    fso_rc = fs_rename(fso_a, fso_b, &fso_err);
    memset(fso_buf, 0, sizeof(fso_buf));
    fso_n = fso_read_first(fso_b, fso_buf, sizeof(fso_buf));
    fso("FSO_RENAME_OVERWRITE",
        fso_rc == STDLIB_OK && fso_err.ok() &&
            fs_exists(fso_a) == 0 && fs_is_file(fso_b) == 1 &&
            fso_n == 14 && strcmp(fso_buf, "OLDLONGCONTENT") == 0 &&
            fs_size(fso_b, &fso_err) == 14);

    /* ---- rename failures leave the target intact ------------------------ */
    snprintf(fso_badparent, sizeof(fso_badparent), "%s/no_such_dir/c.txt",
             fso_dir);
    fso_rc = fs_rename(fso_badparent, fso_b, &fso_err);
    fso("FSO_RENAME_MISSING_SRC",
        fso_rc == STDLIB_FAIL && fso_err.category() == STDLIB_CATEGORY_IO &&
            fs_is_file(fso_b) == 1 && fs_size(fso_b, &fso_err) == 14);
    fso_rc = fs_rename(fso_b, fso_badparent, &fso_err);
    fso("FSO_RENAME_MISSING_PARENT",
        fso_rc == STDLIB_FAIL && fso_err.category() == STDLIB_CATEGORY_IO &&
            fs_is_file(fso_b) == 1 && fs_size(fso_b, &fso_err) == 3 &&
            fs_exists(fso_badparent) == 0);

    /* ---- repeated delete of a missing path ------------------------------ */
    fso_ok = 1;
    for (fso_i = 0; fso_i < 8; fso_i++) {
        fso_rc = fs_remove(fso_badparent, &fso_err);
        fso_ok = fso_ok && fso_rc == STDLIB_FAIL &&
                 fso_err.category() == STDLIB_CATEGORY_IO &&
                 fso_err.code() == CPLUS_EXIT_IO;
    }
    fso("FSO_REPEATED_REMOVE_MISSING", fso_ok);

    /* ---- directory iteration via existing APIs -------------------------- */
    fso_ok = 1;
    for (fso_i = 0; fso_i < 3; fso_i++) {
        snprintf(fso_iter, sizeof(fso_iter), "%s/it_%d.txt", fso_dir, fso_i);
        fso_ok = fso_ok && fso_write_file(fso_iter, "i", 1) == 1;
    }
    fso("FSO_ITER_FIXTURES", fso_ok);
    fso_iterdir = opendir(fso_dir);
    fso_iter_open = fso_iterdir != NULL;
    fso_n = 0;
    if (fso_iter_open) {
        while ((fso_entry = readdir(fso_iterdir)) != NULL) {
            if (strncmp(fso_entry->d_name, "it_", 3) == 0)
                fso_n++;
        }
        closedir(fso_iterdir);
    }
    fso("FSO_ITER_COUNT", fso_iter_open && fso_n == 3);

    /* ---- temp file cleanup ---------------------------------------------- */
    snprintf(fso_tmpl, sizeof(fso_tmpl), "/tmp/%stmp_XXXXXX", fso_pid);
    fso_rc = fs_mkdtemp(fso_tmpl, sizeof(fso_tmpl), &fso_err);
    fso("FSO_MKDTEMP", fso_rc == STDLIB_OK && fs_is_dir(fso_tmpl) == 1);
    snprintf(fso_inner, sizeof(fso_inner), "%s/tempfile", fso_tmpl);
    fso("FSO_TMP_INNER", fso_write_file(fso_inner, "t", 1) == 1 &&
                            fs_size(fso_inner, &fso_err) == 1);
    fso_rc = fs_remove(fso_inner, &fso_err);
    fso("FSO_TMP_INNER_CLEAN", fso_rc == STDLIB_OK &&
                                  fs_exists(fso_inner) == 0);
    fso("FSO_TMP_DIR_CLEAN", rmdir(fso_tmpl) == 0 &&
                                fs_exists(fso_tmpl) == 0);

    /* ---- bounded file-creation race (4 threads) ------------------------- */
    for (fso_i = 0; fso_i < 4; fso_i++) {
        snprintf(fso_slots[fso_i].dir, sizeof(fso_slots[fso_i].dir), "%s",
                 fso_dir);
        fso_slots[fso_i].index = fso_i;
        fso_slots[fso_i].ok = 0;
    }
    fso_rc = fso_thread0.start(fso_worker, &fso_slots[0], &fso_err);
    fso("FSO_RACE_START0", fso_rc == STDLIB_OK && fso_err.ok());
    fso_rc = fso_thread1.start(fso_worker, &fso_slots[1], &fso_err);
    fso("FSO_RACE_START1", fso_rc == STDLIB_OK && fso_err.ok());
    fso_rc = fso_thread2.start(fso_worker, &fso_slots[2], &fso_err);
    fso("FSO_RACE_START2", fso_rc == STDLIB_OK && fso_err.ok());
    fso_rc = fso_thread3.start(fso_worker, &fso_slots[3], &fso_err);
    fso("FSO_RACE_START3", fso_rc == STDLIB_OK && fso_err.ok());

    fso_join_result = NULL;
    fso_rc = fso_thread0.join(&fso_join_result, &fso_err);
    fso("FSO_RACE_JOIN0", fso_rc == STDLIB_OK && fso_join_result == NULL);
    fso_join_result = NULL;
    fso_rc = fso_thread1.join(&fso_join_result, &fso_err);
    fso("FSO_RACE_JOIN1", fso_rc == STDLIB_OK && fso_join_result == NULL);
    fso_join_result = NULL;
    fso_rc = fso_thread2.join(&fso_join_result, &fso_err);
    fso("FSO_RACE_JOIN2", fso_rc == STDLIB_OK && fso_join_result == NULL);
    fso_join_result = NULL;
    fso_rc = fso_thread3.join(&fso_join_result, &fso_err);
    fso("FSO_RACE_JOIN3", fso_rc == STDLIB_OK && fso_join_result == NULL);

    fso_ok = 1;
    for (fso_i = 0; fso_i < 4; fso_i++) {
        snprintf(fso_racepath, sizeof(fso_racepath), "%s/race_%d.txt",
                 fso_dir, fso_i);
        fso_ok = fso_ok && fso_slots[fso_i].ok == 1 &&
                 fs_is_file(fso_racepath) == 1 &&
                 fs_size(fso_racepath, &fso_err) == 4;
    }
    fso("FSO_RACE_FILES", fso_ok);
    fso_thread0.drop();
    fso_thread1.drop();
    fso_thread2.drop();
    fso_thread3.drop();

    /* ---- cleanup and leak proof ----------------------------------------- */
    fso_ok = 1;
    for (fso_i = 0; fso_i < 3; fso_i++) {
        snprintf(fso_iter, sizeof(fso_iter), "%s/it_%d.txt", fso_dir, fso_i);
        fso_rc = fs_remove(fso_iter, &fso_err);
        fso_ok = fso_ok && fso_rc == STDLIB_OK && fs_exists(fso_iter) == 0;
    }
    for (fso_i = 0; fso_i < 4; fso_i++) {
        snprintf(fso_racepath, sizeof(fso_racepath), "%s/race_%d.txt",
                 fso_dir, fso_i);
        fso_rc = fs_remove(fso_racepath, &fso_err);
        fso_ok = fso_ok && fso_rc == STDLIB_OK &&
                 fs_exists(fso_racepath) == 0;
    }
    fso("FSO_FIXTURE_CLEAN", fso_ok);
    fso_rc = fs_remove(fso_b, &fso_err);
    fso("FSO_TARGET_CLEAN", fso_rc == STDLIB_OK && fs_exists(fso_b) == 0);
    fso("FSO_DIR_CLEAN", rmdir(fso_dir) == 0 && fs_exists(fso_dir) == 0);
    fso("FSO_NO_LEFTOVERS", fso_count_tmp(fso_prefix) == 0);
    fso("FSO_FD_NO_LEAK", fso_count_fds() == fso_fd0);
    print("FS_SAFETY", fso_checks, fso_failures);
    return fso_failures == 0 ? 0 : 1;
}
