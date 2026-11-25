/* F-095 -- deterministic fuzz configuration: fs create/remove lifecycle.
 *
 * Standalone bounded fuzzer for the fs.hp create/rename/remove lifecycle.
 * A fixed-seed LCG drives create, single/ repeated remove, rename and mkdir
 * operations over PID-unique paths inside a private sandbox.  An independent
 * shadow bitmap is compared with fs_exists()/fs_is_file() after every case.
 * The run ends by deleting every modeled artifact and proving (a) no file with
 * our PID prefix remains in /tmp and (b) the /dev/fd count is unchanged.
 *
 * Fixed seed, bounded CASES/paths, SIGALRM watchdog, external timeout 30s.
 * Digest folds results/categories/shadow state only -- never strerror() text
 * or the random mkdtemp suffix.
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

#define CASES 600
#define NPATHS 180
#define FNV_PRIME 1099511628211ULL

static unsigned long long rng_state = 0xF0951234ULL;
static unsigned long long digest = 0xcbf29ce484222325ULL;
static int failures = 0;
static char prefix[128];
static char sandbox[512];
static unsigned char modeled[NPATHS];

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
    static const char msg[] = "F095 watchdog timeout\n";
    (void)sig;
    (void)!write(2, msg, sizeof(msg) - 1);
    _exit(90);
}

static void on_ignore(int sig) {
    (void)sig;
}

static void path_of(char *out, size_t cap, int index) {
    snprintf(out, cap, "%s/p%d", sandbox, index);
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

static int count_fds(void) {
    DIR *d = opendir("/dev/fd");
    struct dirent *e;
    int n = 0;
    if (d == NULL)
        return -1;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] >= '0' && e->d_name[0] <= '9')
            n++;
    }
    closedir(d);
    return n;
}

/* Create a small regular file and close it before returning. */
static int create_file(const char *path) {
    FILE *out = fopen(path, "wb");
    size_t written;
    if (out == NULL)
        return 0;
    written = fwrite("f095", 1, 4, out);
    fclose(out);
    return written == 4;
}

int main(void) {
    char tmpl[256];
    char path[512];
    char dst[512];
    char dirtmpl[320];
    char hex[32];
    error ioerr = error();
    int baseline_tmp;
    int baseline_fd;
    int fd_supported;
    int tmp_supported;
    int dir_made;
    int rc;
    int i;
    int idx;
    int dstidx;
    int op;
    int k;
    int checkidx;
    int expect;
    int temp_dirs = 0;

    (void)!signal(SIGPIPE, on_ignore);
    (void)!signal(SIGALRM, on_alarm);
    (void)!alarm(25);

    snprintf(prefix, sizeof(prefix), "f100_f095_%d_", (int)getpid());
    baseline_tmp = count_prefix();
    baseline_fd = count_fds();
    tmp_supported = baseline_tmp >= 0;
    fd_supported = baseline_fd >= 0;
    if (!tmp_supported)
        print("SKIP_REASON", "f095_tmp_count", "opendir(/tmp) unavailable");
    if (!fd_supported)
        print("SKIP_REASON", "f095_fd_count", "opendir(/dev/fd) unavailable");

    snprintf(tmpl, sizeof(tmpl), "/tmp/%sXXXXXX", prefix);
    rc = fs_mkdtemp(tmpl, sizeof(tmpl), &ioerr);
    mix((unsigned long long)(rc == STDLIB_OK));
    if (rc != STDLIB_OK || !fs_is_dir(tmpl)) {
        print("SKIP_REASON", "f095_sandbox", "fs_mkdtemp unavailable on this host");
        (void)!alarm(0);
        snprintf(hex, sizeof(hex), "%016llx", digest);
        print("f095_digest", hex);
        print("f095:", CASES, "cases skipped");
        return 0;
    }
    snprintf(sandbox, sizeof(sandbox), "%s", tmpl);
    dir_made = 0;

    for (i = 0; i < CASES; i++) {
        idx = (int)(rnd() % (unsigned int)NPATHS);
        dstidx = (int)(rnd() % (unsigned int)NPATHS);
        op = (int)(rnd() % 6u);
        path_of(path, sizeof(path), idx);
        mix((unsigned long long)i);
        mix((unsigned long long)op);
        mix((unsigned long long)idx);
        ioerr.clear();

        switch (op) {
        case 0: /* create; a second create on an existing path is a no-op */
            if (!modeled[idx]) {
                if (!create_file(path) || fs_exists(path) != 1 ||
                    fs_is_file(path) != 1 || fs_size(path, &ioerr) != 4)
                    invariant("f095_create");
                modeled[idx] = 1;
            } else {
                if (create_file(path) != 1)
                    invariant("f095_recreate");
            }
            mix((unsigned long long)modeled[idx]);
            break;

        case 1: /* single remove, result predicted by the shadow model */
            rc = fs_remove(path, &ioerr);
            expect = modeled[idx] ? STDLIB_OK : STDLIB_FAIL;
            if (rc != expect)
                invariant("f095_remove_result");
            if (!modeled[idx] &&
                ioerr.category() != STDLIB_CATEGORY_IO)
                invariant("f095_remove_category");
            modeled[idx] = 0;
            if (fs_exists(path) != 0)
                invariant("f095_remove_effect");
            mix((unsigned long long)rc);
            mix((unsigned long long)ioerr.category());
            break;

        case 2: /* repeated remove: only the first can succeed */
            for (k = 0; k < 3; k++) {
                rc = fs_remove(path, &ioerr);
                expect = (k == 0 && modeled[idx]) ? STDLIB_OK : STDLIB_FAIL;
                if (rc != expect)
                    invariant("f095_repeat_remove");
                if (rc == STDLIB_FAIL && ioerr.category() != STDLIB_CATEGORY_IO)
                    invariant("f095_repeat_category");
                modeled[idx] = 0;
                mix((unsigned long long)rc);
                mix((unsigned long long)ioerr.category());
            }
            break;

        case 3: /* rename into a distinct slot, then remove the source */
            if (idx != dstidx) {
                path_of(dst, sizeof(dst), dstidx);
                rc = fs_rename(path, dst, &ioerr);
                if (modeled[idx]) {
                    if (rc != STDLIB_OK || !fs_exists(dst))
                        invariant("f095_rename");
                    modeled[dstidx] = 1;
                    modeled[idx] = 0;
                } else {
                    if (rc != STDLIB_FAIL ||
                        ioerr.category() != STDLIB_CATEGORY_IO)
                        invariant("f095_rename_missing");
                }
                mix((unsigned long long)rc);
                mix((unsigned long long)ioerr.category());
            }
            break;

        case 4: /* repeated mkdir: first OK, rest recoverable IO failures */
            snprintf(path, sizeof(path), "%s/d", sandbox);
            if (!dir_made) {
                if (fs_mkdir(path, &ioerr) != STDLIB_OK ||
                    !fs_is_dir(path))
                    invariant("f095_mkdir");
                dir_made = 1;
            } else {
                if (fs_mkdir(path, &ioerr) != STDLIB_FAIL ||
                    ioerr.category() != STDLIB_CATEGORY_IO)
                    invariant("f095_mkdir_repeat");
            }
            mix((unsigned long long)dir_made);
            mix((unsigned long long)ioerr.category());
            if ((rnd() % 8u) == 0u) {
                if (rmdir(path) != 0)
                    invariant("f095_rmdir");
                dir_made = 0;
            }
            break;

        default: /* bounded mkdtemp cycle with an explicit cleanup */
            snprintf(dirtmpl, sizeof(dirtmpl), "%s/t%d_XXXXXX", sandbox, i);
            rc = fs_mkdtemp(dirtmpl, sizeof(dirtmpl), &ioerr);
            if (rc != STDLIB_OK || !fs_is_dir(dirtmpl))
                invariant("f095_mkdtemp");
            if (rmdir(dirtmpl) != 0)
                invariant("f095_mkdtemp_rmdir");
            temp_dirs++;
            mix((unsigned long long)rc);
            break;
        }

        /* Independent shadow comparison on a bounded neighbourhood. */
        for (k = 0; k < 6; k++) {
            checkidx = (idx + k * 29) % NPATHS;
            path_of(path, sizeof(path), checkidx);
            if (fs_exists(path) != (modeled[checkidx] ? 1 : 0))
                invariant("f095_shadow_exists");
            if (modeled[checkidx] && fs_is_file(path) != 1)
                invariant("f095_shadow_isfile");
            mix((unsigned long long)modeled[checkidx]);
        }
    }

    /* Explicit cleanup of every modeled artifact and the sandbox itself. */
    for (i = 0; i < NPATHS; i++) {
        if (modeled[i]) {
            path_of(path, sizeof(path), i);
            if (fs_remove(path, &ioerr) != STDLIB_OK ||
                fs_exists(path) != 0)
                invariant("f095_final_remove");
            modeled[i] = 0;
        }
    }
    snprintf(path, sizeof(path), "%s/d", sandbox);
    if (dir_made && rmdir(path) != 0)
        invariant("f095_final_rmdir_d");
    if (rmdir(sandbox) != 0)
        invariant("f095_final_rmdir_sandbox");
    if (fs_exists(sandbox) != 0)
        invariant("f095_final_sandbox_gone");

    if (tmp_supported) {
        int after_tmp = count_prefix();
        if (after_tmp != baseline_tmp)
            invariant("f095_tmp_cleanup_proof");
        mix((unsigned long long)(after_tmp - baseline_tmp));
    } else {
        mix((unsigned long long)-1);
    }
    if (fd_supported) {
        int after_fd = count_fds();
        if (after_fd != baseline_fd)
            invariant("f095_fd_cleanup_proof");
        mix((unsigned long long)(after_fd - baseline_fd));
    } else {
        mix((unsigned long long)-1);
    }
    mix((unsigned long long)temp_dirs);

    (void)!alarm(0);
    mix((unsigned long long)failures);
    snprintf(hex, sizeof(hex), "%016llx", digest);
    print("f095_digest", hex);
    if (failures == 0)
        print("f095:", CASES, "cases ok");
    else
        print("f095: FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
