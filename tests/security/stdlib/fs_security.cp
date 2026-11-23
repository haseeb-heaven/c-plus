/* Task 6 / Phase 4 -- filesystem & path SECURITY characterization of stdlib/fs.hp.
 *
 * This is a deterministic CHARACTERIZATION / AUDIT suite, not a claim that
 * fs.hp "validates" or "sanitizes" paths.  fs.hp is a thin stat(2)/unlink(2)/
 * rename(2)/mkdir(2)/mkdtemp(3) wrapper with NO path canonicalization, NO
 * confinement root, NO lstat/O_NOFOLLOW/openat primitives and NO atomic
 * check-then-use.  Everything below records that reality so callers know what
 * they must enforce themselves.  See FS_SECURITY_NOTES.md for the threat model.
 *
 * Coverage (all reachable on this POSIX host):
 *   1  path traversal via '..' components -- resolves through the OS; fs.hp
 *      does NOT confine a path to any root (no string check exists).
 *   2  path traversal via a symlinked directory -- a path with no '..' at all
 *      still escapes through a link; a string-only '..' filter would miss it.
 *   3  symlink to file / symlink to dir / dangling symlink -- stat() follows,
 *      unlink() removes the link and leaves the target intact.
 *   4  TOCTOU -- deterministic check-then-swap demonstration: the result of a
 *      stat-based query is stale the instant the entry is replaced.  We do NOT
 *      claim any string check prevents this; fs.hp offers no atomic primitive.
 *   5  temporary path creation -- fs_mkdtemp() exclusive mkdtemp(3) creation,
 *      caller-supplied PID-unique prefix, template validation, uniqueness.
 *   6  unsafe replacement -- fs_rename() is rename(2): it silently overwrites
 *      an existing destination (no no-clobber).  Documented, not "fixed".
 *   7  permission handling -- DAC denial for a non-root euid; an explicit,
 *      non-passing SKIP is printed when euid == 0 (root bypasses DAC).
 *   8  directory traversal via existing APIs -- opendir/readdir/closedir;
 *      fs.hp has no iteration API, callers must use libc dirent.
 *   9  malformed paths -- NULL, empty, embedded NUL (syscall truncation),
 *      PATH_MAX+ and NAME_MAX+1 lengths, malformed mkdtemp templates.
 *  10  cleanup proof -- PID-unique /tmp prefix; baseline/final /dev/fd and
 *      /tmp-prefix counts must match (no descriptor or filesystem leak).
 *
 * Protocol: every executed check prints "<TAG> <1|0>"; the last line is
 *   FS_SECURITY <checks> <failures>
 * Exit code is 0 only when failures == 0.  Root-only SKIPs print
 *   SKIP_REASON <TAG> reason=...
 * and are NOT counted as passes or failures.  No header is modified.
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
/* Test-only hook: compile with -- -DFSQ_FORCE_ROOT=1 to exercise the root
 * SKIP branch deterministically without changing euid.  Default is 0. */
#ifndef FSQ_FORCE_ROOT
#define FSQ_FORCE_ROOT 0
#endif

static int fsq_checks = 0;
static int fsq_failures = 0;

static void fsq(const char *tag, int cond) {
    fsq_checks++;
    if (!cond)
        fsq_failures++;
    print(tag, cond ? 1 : 0);
}

/* error-record predicates: size helper returns -1, op helper returns FAIL. */
static int fsq_io_size(long long rc, error *e) {
    return rc == -1 && e->category() == STDLIB_CATEGORY_IO &&
           e->code() == CPLUS_EXIT_IO;
}
static int fsq_io_fail(int rc, error *e) {
    return rc == STDLIB_FAIL && e->category() == STDLIB_CATEGORY_IO &&
           e->code() == CPLUS_EXIT_IO;
}
static int fsq_invalid_size(long long rc, error *e) {
    return rc == -1 && e->category() == STDLIB_CATEGORY_INVALID;
}
static int fsq_invalid_fail(int rc, error *e) {
    return rc == STDLIB_FAIL && e->category() == STDLIB_CATEGORY_INVALID;
}

/* Write a small known-content fixture file. */
static int fsq_write_file(const char *path, const char *data, int n) {
    FILE *fp = fopen(path, "wb");
    int ok = 0;
    if (fp != NULL) {
        ok = (n == 0 || fwrite(data, 1, (size_t)n, fp) == (size_t)n);
        if (fclose(fp) != 0)
            ok = 0;
    }
    return ok;
}

/* Return the first byte of a file, or -1 when unreadable. */
static int fsq_first_byte(const char *path) {
    FILE *fp = fopen(path, "rb");
    unsigned char b[1];
    int first = -1;
    if (fp != NULL) {
        if (fread(b, 1, 1, fp) == 1)
            first = (int)b[0];
        fclose(fp);
    }
    return first;
}

/* lstat() sees the link itself; stat() (used by fs.hp) follows it. */
static int fsq_lstat_ok(const char *path) {
    struct stat info;
    return lstat(path, &info) == 0;
}

static int fsq_count_fds(void) {
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

static int fsq_count_tmp(const char *prefix) {
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
    error fsq_err = error();
    char fsq_pid[32], fsq_prefix[64];
    char fsq_sandbox[160], fsq_work[220], fsq_safe[220], fsq_secret[240];
    char fsq_escape[280], fsq_elink[240], fsq_elink_target[220];
    char fsq_elink_path[280];
    char fsq_a[200], fsq_b[200], fsq_lfile[200], fsq_tdir[200];
    char fsq_ldir[200], fsq_dang_target[200], fsq_ldang[200];
    char fsq_ta[200], fsq_tb[200], fsq_tlink[200];
    char fsq_tmpl[200], fsq_tmpl2[200], fsq_tmpl_bad[200], fsq_tmpl_short[64];
    char fsq_rsrc[200], fsq_rdst[200], fsq_rdir[220], fsq_rdst2[220];
    char fsq_pdir[200], fsq_pfile[240];
    char fsq_nulsrc[200], fsq_nulpath[220], fsq_name[300];
    char fsq_bound[420], fsq_toolong[420];
    char fsq_longpath[PATH_MAX + 128];
    size_t fsq_len;
    int fsq_fd0, fsq_tmp0, fsq_rc, fsq_n;
    int fsq_iter_open, fsq_dot, fsq_dotdot, fsq_secret_seen;
    DIR *fsq_iter;
    struct dirent *fsq_entry;

    snprintf(fsq_pid, sizeof(fsq_pid), "%d", (int)getpid());
    snprintf(fsq_prefix, sizeof(fsq_prefix), "cplus_fsq_%s_", fsq_pid);
    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();

    fsq_fd0 = fsq_count_fds();
    fsq_tmp0 = fsq_count_tmp(fsq_prefix);
    fsq("FSQ_BASELINE", fsq_fd0 >= 0 && fsq_tmp0 == 0);

    /* ============================ 1. '..' path traversal ================ */
    snprintf(fsq_sandbox, sizeof(fsq_sandbox), "/tmp/%ssandbox", fsq_pid);
    snprintf(fsq_work, sizeof(fsq_work), "%s/work", fsq_sandbox);
    snprintf(fsq_safe, sizeof(fsq_safe), "%s/safe", fsq_sandbox);
    snprintf(fsq_secret, sizeof(fsq_secret), "%s/secret.txt", fsq_safe);
    snprintf(fsq_escape, sizeof(fsq_escape), "%s/work/../safe/secret.txt",
             fsq_sandbox);
    fsq("FSQ_TRAV_SETUP",
        fs_mkdir(fsq_sandbox, &fsq_err) == STDLIB_OK &&
            fs_mkdir(fsq_work, &fsq_err) == STDLIB_OK &&
            fs_mkdir(fsq_safe, &fsq_err) == STDLIB_OK &&
            fsq_write_file(fsq_secret, "SECRET", 6) == 1);
    fsq("FSQ_TRAV_DOTDOT_EXISTS", fs_exists(fsq_escape) == 1 &&
                                    fs_is_file(fsq_escape) == 1);
    fsq("FSQ_TRAV_DOTDOT_SIZE",
        fs_size(fsq_escape, &fsq_err) == 6 && fsq_err.ok());
    /* The confined "work" dir itself has no secret directly. */
    snprintf(fsq_a, sizeof(fsq_a), "%s/secret.txt", fsq_work);
    fsq("FSQ_TRAV_NO_CONFINEMENT", fs_exists(fsq_a) == 0 &&
                                      fs_exists(fsq_escape) == 1);

    /* ============ 2. symlinked-directory traversal (no '..' text) ======== */
    snprintf(fsq_elink, sizeof(fsq_elink), "%s/work/escape_link", fsq_sandbox);
    snprintf(fsq_elink_target, sizeof(fsq_elink_target), "%s", fsq_safe);
    snprintf(fsq_elink_path, sizeof(fsq_elink_path), "%s/secret.txt",
             fsq_elink);
    fsq("FSQ_TRAV_SYMLINK_SETUP",
        symlink(fsq_elink_target, fsq_elink) == 0 && fsq_lstat_ok(fsq_elink));
    fsq("FSQ_TRAV_SYMLINK_DIR", fs_is_dir(fsq_elink) == 1);
    fsq("FSQ_TRAV_SYMLINK_RESOLVE", fs_is_file(fsq_elink_path) == 1 &&
                                       fs_size(fsq_elink_path, &fsq_err) == 6);
    fsq("FSQ_TRAV_SYMLINK_CLEAN",
        fs_remove(fsq_elink, &fsq_err) == STDLIB_OK &&
            fsq_lstat_ok(fsq_elink) == 0 && fs_is_file(fsq_secret) == 1);

    /* ==================== 3a. symlink -> regular file ==================== */
    snprintf(fsq_a, sizeof(fsq_a), "/tmp/%starget_file", fsq_pid);
    snprintf(fsq_lfile, sizeof(fsq_lfile), "/tmp/%slink_file", fsq_pid);
    fsq("FSQ_SYMFILE_FIXTURE", fsq_write_file(fsq_a, "HELLOFS", 7) == 1);
    fsq("FSQ_SYMFILE_SETUP",
        symlink(fsq_a, fsq_lfile) == 0 && fsq_lstat_ok(fsq_lfile));
    fsq("FSQ_SYMFILE_FOLLOW", fs_exists(fsq_lfile) == 1 &&
                                 fs_is_file(fsq_lfile) == 1 &&
                                 fs_is_dir(fsq_lfile) == 0);
    fsq("FSQ_SYMFILE_SIZE",
        fs_size(fsq_lfile, &fsq_err) == 7 && fsq_err.ok());
    fsq("FSQ_SYMFILE_UNLINK_LINK",
        fs_remove(fsq_lfile, &fsq_err) == STDLIB_OK &&
            fsq_lstat_ok(fsq_lfile) == 0 && fs_is_file(fsq_a) == 1);
    fsq("FSQ_SYMFILE_TARGET_INTACT",
        fs_size(fsq_a, &fsq_err) == 7);
    fsq("FSQ_SYMFILE_CLEAN",
        fs_remove(fsq_a, &fsq_err) == STDLIB_OK && fs_exists(fsq_a) == 0);

    /* ==================== 3b. symlink -> directory ====================== */
    snprintf(fsq_tdir, sizeof(fsq_tdir), "/tmp/%starget_dir", fsq_pid);
    snprintf(fsq_ldir, sizeof(fsq_ldir), "/tmp/%slink_dir", fsq_pid);
    fsq("FSQ_SYMDIR_SETUP",
        fs_mkdir(fsq_tdir, &fsq_err) == STDLIB_OK &&
            symlink(fsq_tdir, fsq_ldir) == 0 && fsq_lstat_ok(fsq_ldir));
    fsq("FSQ_SYMDIR_FOLLOW", fs_is_dir(fsq_ldir) == 1 &&
                                fs_is_file(fsq_ldir) == 0);
    fsq("FSQ_SYMDIR_UNLINK_LINK",
        fs_remove(fsq_ldir, &fsq_err) == STDLIB_OK &&
            fsq_lstat_ok(fsq_ldir) == 0 && fs_is_dir(fsq_tdir) == 1);
    fsq("FSQ_SYMDIR_CLEAN", rmdir(fsq_tdir) == 0 && fs_exists(fsq_tdir) == 0);

    /* ==================== 3c. dangling symlink ========================== */
    snprintf(fsq_dang_target, sizeof(fsq_dang_target),
             "/tmp/%sNO_SUCH_TARGET", fsq_pid);
    snprintf(fsq_ldang, sizeof(fsq_ldang), "/tmp/%slink_dangling", fsq_pid);
    fsq("FSQ_DANGLING_SETUP",
        symlink(fsq_dang_target, fsq_ldang) == 0 &&
            fsq_lstat_ok(fsq_ldang) && fs_exists(fsq_dang_target) == 0);
    fsq("FSQ_DANGLING_QUERIES", fs_exists(fsq_ldang) == 0 &&
                                   fs_is_file(fsq_ldang) == 0 &&
                                   fs_is_dir(fsq_ldang) == 0);
    fsq_rc = fs_size(fsq_ldang, &fsq_err);
    fsq("FSQ_DANGLING_SIZE", fsq_io_size(fsq_rc, &fsq_err));
    fsq("FSQ_DANGLING_REMOVE",
        fs_remove(fsq_ldang, &fsq_err) == STDLIB_OK &&
            fsq_lstat_ok(fsq_ldang) == 0);

    /* ==================== 4. TOCTOU (documented, not fixed) ============= */
    snprintf(fsq_ta, sizeof(fsq_ta), "/tmp/%stoc_a", fsq_pid);
    snprintf(fsq_tb, sizeof(fsq_tb), "/tmp/%stoc_b", fsq_pid);
    snprintf(fsq_tlink, sizeof(fsq_tlink), "/tmp/%stoc_link", fsq_pid);
    fsq("FSQ_TOCTOU_SETUP", fsq_write_file(fsq_ta, "AAA", 3) == 1 &&
                                fsq_write_file(fsq_tb, "BBBBB", 5) == 1 &&
                                symlink(fsq_ta, fsq_tlink) == 0);
    fsq_rc = fs_size(fsq_tlink, &fsq_err);
    fsq("FSQ_TOCTOU_CHECK", fsq_rc == 3);
    /* Attacker swaps the symlink between the check and the next use. */
    fsq("FSQ_TOCTOU_SWAP",
        unlink(fsq_tlink) == 0 && symlink(fsq_tb, fsq_tlink) == 0);
    fsq_rc = fs_size(fsq_tlink, &fsq_err);
    fsq("FSQ_TOCTOU_STALE",
        fsq_rc == 5); /* check said 3; the object is now 5 -- no atomicity */
    fsq("FSQ_TOCTOU_CLEAN",
        fs_remove(fsq_tlink, &fsq_err) == STDLIB_OK &&
            fs_remove(fsq_ta, &fsq_err) == STDLIB_OK &&
            fs_remove(fsq_tb, &fsq_err) == STDLIB_OK);

    /* ================ 5. temporary path creation (PID-unique) =========== */
    snprintf(fsq_tmpl, sizeof(fsq_tmpl), "/tmp/%stmp_XXXXXX", fsq_pid);
    snprintf(fsq_tmpl2, sizeof(fsq_tmpl2), "/tmp/%stmp_XXXXXX", fsq_pid);
    fsq_rc = fs_mkdtemp(fsq_tmpl, sizeof(fsq_tmpl), &fsq_err);
    fsq("FSQ_TMP_CREATE", fsq_rc == STDLIB_OK && fsq_err.ok() &&
                             fs_is_dir(fsq_tmpl) == 1 &&
                             strstr(fsq_tmpl, "XXXXXX") == NULL);
    fsq_rc = fs_mkdtemp(fsq_tmpl2, sizeof(fsq_tmpl2), &fsq_err);
    fsq("FSQ_TMP_UNIQUE", fsq_rc == STDLIB_OK &&
                             strcmp(fsq_tmpl, fsq_tmpl2) != 0 &&
                             fs_is_dir(fsq_tmpl2) == 1);
    /* Template must end in XXXXXX (string validation) and parent must exist. */
    snprintf(fsq_tmpl_bad, sizeof(fsq_tmpl_bad), "/tmp/%sno_suffix", fsq_pid);
    fsq("FSQ_TMP_TEMPLATE_BAD",
        fs_mkdtemp(fsq_tmpl_bad, sizeof(fsq_tmpl_bad), &fsq_err) ==
                STDLIB_FAIL &&
            fsq_err.category() == STDLIB_CATEGORY_INVALID);
    snprintf(fsq_tmpl_bad, sizeof(fsq_tmpl_bad),
             "/tmp/%sno_parent/XXXXXX", fsq_pid);
    fsq("FSQ_TMP_PARENT_MISSING",
        fsq_io_fail(fs_mkdtemp(fsq_tmpl_bad, sizeof(fsq_tmpl_bad), &fsq_err),
                    &fsq_err));
    fsq_tmpl_short[0] = '\0';
    fsq("FSQ_TMP_TEMPLATE_SHORT",
        fs_mkdtemp(fsq_tmpl_short, sizeof(fsq_tmpl_short), &fsq_err) ==
                STDLIB_FAIL &&
            fsq_err.category() == STDLIB_CATEGORY_INVALID);
    fsq("FSQ_TMP_CLEAN",
        rmdir(fsq_tmpl) == 0 && rmdir(fsq_tmpl2) == 0 &&
            fs_exists(fsq_tmpl) == 0 && fs_exists(fsq_tmpl2) == 0);

    /* ================ 6. unsafe replacement (rename over) =============== */
    snprintf(fsq_rdir, sizeof(fsq_rdir), "/tmp/%srenamedir", fsq_pid);
    snprintf(fsq_rsrc, sizeof(fsq_rsrc), "%s/src.txt", fsq_rdir);
    snprintf(fsq_rdst, sizeof(fsq_rdst), "%s/dst.txt", fsq_rdir);
    snprintf(fsq_rdst2, sizeof(fsq_rdst2), "%s/dst2.txt", fsq_rdir);
    fsq("FSQ_RENAME_SETUP",
        fs_mkdir(fsq_rdir, &fsq_err) == STDLIB_OK &&
            fsq_write_file(fsq_rsrc, "NEW", 3) == 1 &&
            fsq_write_file(fsq_rdst, "OLD", 3) == 1);
    fsq("FSQ_RENAME_PRE", fsq_first_byte(fsq_rdst) == 'O');
    fsq("FSQ_RENAME_OVERWRITE",
        fs_rename(fsq_rsrc, fsq_rdst, &fsq_err) == STDLIB_OK && fsq_err.ok());
    fsq("FSQ_RENAME_CLOBBERED", fsq_first_byte(fsq_rdst) == 'N' &&
                                    fs_size(fsq_rdst, &fsq_err) == 3 &&
                                    fs_exists(fsq_rsrc) == 0);
    /* rename to a brand-new name is a plain move (no destination existed). */
    fsq("FSQ_RENAME_MOVE_FRESH",
        fsq_write_file(fsq_rsrc, "MOVE", 4) == 1 &&
            fs_rename(fsq_rsrc, fsq_rdst2, &fsq_err) == STDLIB_OK &&
            fsq_first_byte(fsq_rdst2) == 'M' && fs_exists(fsq_rsrc) == 0);
    /* rename of a file onto an existing directory must fail (IO category). */
    fsq("FSQ_RENAME_ONTO_DIR",
        fsq_write_file(fsq_rsrc, "X", 1) == 1 &&
            fsq_io_fail(fs_rename(fsq_rsrc, fsq_rdir, &fsq_err), &fsq_err) &&
            fs_is_file(fsq_rsrc) == 1);
    fsq("FSQ_RENAME_CLEAN",
        fs_remove(fsq_rdst2, &fsq_err) == STDLIB_OK &&
            fs_remove(fsq_rsrc, &fsq_err) == STDLIB_OK &&
            fs_remove(fsq_rdst, &fsq_err) == STDLIB_OK &&
            rmdir(fsq_rdir) == 0 && fs_exists(fsq_rdir) == 0);

    /* ==================== 7. permission handling ======================== */
    snprintf(fsq_pdir, sizeof(fsq_pdir), "/tmp/%spermdir", fsq_pid);
    snprintf(fsq_pfile, sizeof(fsq_pfile), "%s/inside", fsq_pdir);
    if (geteuid() == 0 || FSQ_FORCE_ROOT) {
        printf("SKIP_REASON FSQ_PERMISSIONS reason=euid=0-root-bypasses-DAC-nondeterministic\n");
    } else {
        int fsq_perm_ok = (fs_mkdir(fsq_pdir, &fsq_err) == STDLIB_OK &&
                           fsq_write_file(fsq_pfile, "LOCKED", 6) == 1 &&
                           chmod(fsq_pdir, 0000) == 0);
        fsq("FSQ_PERM_SETUP", fsq_perm_ok);
        if (fsq_perm_ok) {
            FILE *fp = fopen(fsq_pfile, "rb");
            fsq("FSQ_PERM_STAT_DENIED", fs_is_file(fsq_pfile) == 0);
            fsq("FSQ_PERM_OPEN_DENIED", fp == NULL);
            if (fp != NULL)
                fclose(fp);
            chmod(fsq_pdir, 0755);
            fsq("FSQ_PERM_CLEAN", fs_remove(fsq_pfile, &fsq_err) == STDLIB_OK &&
                                      rmdir(fsq_pdir) == 0 &&
                                      fs_exists(fsq_pdir) == 0);
        } else {
            chmod(fsq_pdir, 0755);
            (void)rmdir(fsq_pdir);
            (void)fs_remove(fsq_pfile, &fsq_err);
            fsq("FSQ_PERM_STAT_DENIED", 0);
            fsq("FSQ_PERM_OPEN_DENIED", 0);
            fsq("FSQ_PERM_CLEAN", fs_exists(fsq_pdir) == 0);
        }
    }

    /* ============ 8. directory traversal via existing APIs ============== */
    fsq_iter = opendir(fsq_sandbox);
    fsq_iter_open = fsq_iter != NULL;
    fsq_n = 0;
    fsq_dot = 0;
    fsq_dotdot = 0;
    fsq_secret_seen = 0;
    if (fsq_iter_open) {
        while ((fsq_entry = readdir(fsq_iter)) != NULL) {
            if (strcmp(fsq_entry->d_name, ".") == 0)
                fsq_dot++;
            else if (strcmp(fsq_entry->d_name, "..") == 0)
                fsq_dotdot++;
            else
                fsq_n++;
            if (strcmp(fsq_entry->d_name, "safe") == 0)
                fsq_secret_seen++;
        }
        closedir(fsq_iter);
    }
    fsq("FSQ_ITER_OPEN", fsq_iter_open);
    fsq("FSQ_ITER_DOT_ENTRIES", fsq_dot == 1 && fsq_dotdot == 1);
    fsq("FSQ_ITER_ENTRIES", fsq_n >= 2 && fsq_secret_seen == 1);
    fsq("FSQ_ITER_CLASSIFY", fs_is_dir(fsq_safe) == 1 &&
                                fs_is_dir(fsq_work) == 1 &&
                                fs_exists(fsq_secret) == 1);

    /* ==================== 9. malformed paths =========================== */
    fsq("FSQ_NULL_QUERIES", fs_exists(NULL) == 0 && fs_is_file(NULL) == 0 &&
                                fs_is_dir(NULL) == 0);
    fsq("FSQ_NULL_SIZE", fsq_invalid_size(fs_size(NULL, &fsq_err), &fsq_err));
    fsq("FSQ_NULL_OPS",
        fsq_invalid_fail(fs_remove(NULL, &fsq_err), &fsq_err) &&
            fsq_invalid_fail(fs_mkdir(NULL, &fsq_err), &fsq_err) &&
            fsq_invalid_fail(fs_rename(NULL, fsq_secret, &fsq_err), &fsq_err) &&
            fsq_invalid_fail(fs_rename(fsq_secret, NULL, &fsq_err), &fsq_err));
    fsq("FSQ_EMPTY_QUERIES", fs_exists("") == 0 && fs_is_file("") == 0 &&
                                 fs_is_dir("") == 0);
    fsq("FSQ_EMPTY_OPS", fsq_io_size(fs_size("", &fsq_err), &fsq_err) &&
                             fsq_io_fail(fs_remove("", &fsq_err), &fsq_err) &&
                             fsq_io_fail(fs_mkdir("", &fsq_err), &fsq_err));

    /* embedded NUL: syscalls truncate at the first NUL byte. */
    snprintf(fsq_nulsrc, sizeof(fsq_nulsrc), "%s/nultrunc.txt", fsq_sandbox);
    memset(fsq_nulpath, 0, sizeof(fsq_nulpath));
    fsq_len = strlen(fsq_nulsrc);
    __builtin_memcpy(fsq_nulpath, fsq_nulsrc, fsq_len);
    __builtin_memcpy(fsq_nulpath + fsq_len + 1, "IGNORED", 7);
    fsq("FSQ_NUL_TRUNC_INVISIBLE",
        fs_exists(fsq_nulpath) == 0 &&
            fsq_io_size(fs_size(fsq_nulpath, &fsq_err), &fsq_err));
    fsq("FSQ_NUL_FIXTURE", fsq_write_file(fsq_nulsrc, "xyz", 3) == 1);
    fsq("FSQ_NUL_TRUNC_VISIBLE",
        fs_exists(fsq_nulpath) == 1 &&
            fs_size(fsq_nulpath, &fsq_err) == 3);
    fsq("FSQ_NUL_CLEAN", fs_remove(fsq_nulpath, &fsq_err) == STDLIB_OK &&
                             fs_exists(fsq_nulsrc) == 0);

    /* overlong path (PATH_MAX+) -> defined IO failure, never an overflow. */
    memset(fsq_longpath, 'a', sizeof(fsq_longpath) - 1);
    fsq_longpath[sizeof(fsq_longpath) - 1] = '\0';
    __builtin_memcpy(fsq_longpath, "/tmp/", 5);
    fsq("FSQ_LONG_LEN", strlen(fsq_longpath) > (size_t)PATH_MAX);
    fsq("FSQ_LONG_QUERIES", fs_exists(fsq_longpath) == 0 &&
                                fs_is_file(fsq_longpath) == 0);
    fsq("FSQ_LONG_IO", fsq_io_size(fs_size(fsq_longpath, &fsq_err), &fsq_err) &&
                          fsq_io_fail(fs_remove(fsq_longpath, &fsq_err),
                                      &fsq_err) &&
                          fsq_io_fail(fs_mkdir(fsq_longpath, &fsq_err),
                                      &fsq_err));

    /* NAME_MAX component succeeds; NAME_MAX+1 is a defined IO failure. */
    memset(fsq_name, 'x', NAME_MAX);
    fsq_name[NAME_MAX] = '\0';
    snprintf(fsq_bound, sizeof(fsq_bound), "%s/", fsq_sandbox);
    fsq_len = strlen(fsq_bound);
    __builtin_memcpy(fsq_bound + fsq_len, fsq_name, (size_t)NAME_MAX + 1);
    fsq("FSQ_NAME_MAX_CREATE", fsq_write_file(fsq_bound, "b", 1) == 1 &&
                                   fs_is_file(fsq_bound) == 1);
    fsq("FSQ_NAME_MAX_CLEAN",
        fs_remove(fsq_bound, &fsq_err) == STDLIB_OK && fs_exists(fsq_bound) == 0);
    memset(fsq_name, 'y', NAME_MAX + 1);
    fsq_name[NAME_MAX + 1] = '\0';
    snprintf(fsq_toolong, sizeof(fsq_toolong), "%s/", fsq_sandbox);
    fsq_len = strlen(fsq_toolong);
    __builtin_memcpy(fsq_toolong + fsq_len, fsq_name, (size_t)NAME_MAX + 2);
    fsq("FSQ_NAME_MAX_PLUS_ONE",
        fsq_io_size(fs_size(fsq_toolong, &fsq_err), &fsq_err) &&
            fsq_io_fail(fs_mkdir(fsq_toolong, &fsq_err), &fsq_err));

    /* ==================== 10. cleanup proof ============================ */
    fsq("FSQ_TRAV_CLEAN",
        fs_remove(fsq_secret, &fsq_err) == STDLIB_OK &&
            rmdir(fsq_work) == 0 && rmdir(fsq_safe) == 0 &&
            rmdir(fsq_sandbox) == 0 && fs_exists(fsq_sandbox) == 0);
    fsq("FSQ_NO_LEFTOVERS", fsq_count_tmp(fsq_prefix) == fsq_tmp0);
    fsq("FSQ_FD_NO_LEAK", fsq_count_fds() == fsq_fd0);

    print("FS_SECURITY", fsq_checks, fsq_failures);
    return fsq_failures == 0 ? 0 : 1;
}
