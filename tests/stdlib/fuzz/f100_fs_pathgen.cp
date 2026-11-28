/* F-094 -- deterministic fuzz configuration: fs path generation.
 *
 * Standalone (own main) grammar-driven fuzzer for stdlib/fs.hp.  It generates
 * a fixed number of paths from a fixed-seed LCG: PID-unique valid paths inside
 * a private mkdtemp sandbox, the empty string, NULL, an embedded-NUL buffer, a
 * PATH_MAX+128 over-long path, a template without the XXXXXX suffix and a
 * too-small template buffer.  Every generated path is passed only to bounded,
 * non-blocking fs queries / recoverable operations; an invariant mismatch is
 * a library finding, not a contract violation by the caller.
 *
 * Termination: bounded CASES loop, bounded path lengths, an in-process
 * SIGALRM watchdog (_exit(90) on expiry) and an external `timeout 30s`.
 * Output: "f094_digest <hex>" then "f094: <N> cases ok" on success, stderr
 * empty.  The FNV-1a digest folds operation kinds, result codes and error
 * categories -- never strerror() text or the random mkdtemp suffix, which are
 * host-dependent.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <limits.h>
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

#define CASES 500
#define FNV_PRIME 1099511628211ULL
#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

static unsigned long long rng_state = 0xF0941234ULL;
static unsigned long long digest = 0xcbf29ce484222325ULL;
static int failures = 0;
static char prefix[128];
static char sandbox[512];

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
    static const char msg[] = "F094 watchdog timeout\n";
    (void)sig;
    (void)!write(2, msg, sizeof(msg) - 1);
    _exit(90);
}

static void on_ignore(int sig) {
    (void)sig;
}

/* Count /tmp entries that start with our PID-unique prefix (cleanup proof). */
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

int main(void) {
    char tmpl[256];
    char path[PATH_MAX + 160];
    char shortbuf[16];
    error probe = error();
    int baseline;
    int after;
    int i;
    int rc;
    int kind;
    unsigned int w0;
    unsigned int w1;
    char hex[32];

    (void)!signal(SIGPIPE, on_ignore);
    (void)!signal(SIGALRM, on_alarm);
    (void)!alarm(20);

    snprintf(prefix, sizeof(prefix), "f100_f094_%d_", (int)getpid());
    baseline = count_prefix();

    /* Private, PID-unique sandbox.  Only the success boolean is folded: the
     * mkdtemp suffix is intentionally random. */
    snprintf(tmpl, sizeof(tmpl), "/tmp/%sXXXXXX", prefix);
    rc = fs_mkdtemp(tmpl, sizeof(tmpl), &probe);
    mix((unsigned long long)(rc == STDLIB_OK));
    if (rc != STDLIB_OK || !fs_is_dir(tmpl)) {
        print("SKIP_REASON", "f094_sandbox", "fs_mkdtemp unavailable on this host");
        (void)!alarm(0);
        mix((unsigned long long)failures);
        snprintf(hex, sizeof(hex), "%016llx", digest);
        print("f094_digest", hex);
        print("f094:", CASES, "cases skipped");
        return 0;
    }
    snprintf(sandbox, sizeof(sandbox), "%s", tmpl);

    for (i = 0; i < CASES; i++) {
        w0 = rnd();
        w1 = rnd();
        kind = (int)(w0 % 7u);
        mix((unsigned long long)i);
        mix((unsigned long long)kind);
        probe.clear();

        switch (kind) {
        case 0: /* PID-unique valid path that was never created */
            snprintf(path, sizeof(path), "%s/f%u", sandbox, w1);
            if (fs_exists(path) != 0 || fs_is_file(path) != 0 ||
                fs_is_dir(path) != 0)
                invariant("f094_fresh_path");
            mix((unsigned long long)fs_exists(path));
            mix((unsigned long long)(fs_size(path, &probe) == -1));
            mix((unsigned long long)probe.category());
            if (!probe.failed_now())
                invariant("f094_fresh_size_err");
            break;

        case 1: /* empty string */
            if (fs_exists("") != 0 || fs_is_file("") != 0 ||
                fs_is_dir("") != 0)
                invariant("f094_empty_exists");
            if (fs_size("", &probe) != -1 || !probe.failed_now())
                invariant("f094_empty_size");
            mix((unsigned long long)probe.category());
            if (fs_remove("", &probe) != STDLIB_FAIL || probe.ok())
                invariant("f094_empty_remove");
            mix((unsigned long long)probe.category());
            break;

        case 2: /* NULL path: documented INVALID for fallible helpers */
            if (fs_exists(NULL) != 0 || fs_is_file(NULL) != 0 ||
                fs_is_dir(NULL) != 0)
                invariant("f094_null_exists");
            if (fs_size(NULL, &probe) != -1 ||
                probe.category() != STDLIB_CATEGORY_INVALID)
                invariant("f094_null_size");
            mix((unsigned long long)probe.category());
            if (fs_remove(NULL, &probe) != STDLIB_FAIL ||
                probe.category() != STDLIB_CATEGORY_INVALID)
                invariant("f094_null_remove");
            if (fs_rename(NULL, NULL, &probe) != STDLIB_FAIL ||
                probe.category() != STDLIB_CATEGORY_INVALID)
                invariant("f094_null_rename");
            if (fs_mkdir(NULL, &probe) != STDLIB_FAIL ||
                probe.category() != STDLIB_CATEGORY_INVALID)
                invariant("f094_null_mkdir");
            if (fs_mkdtemp(NULL, sizeof(tmpl), &probe) != STDLIB_FAIL ||
                probe.category() != STDLIB_CATEGORY_INVALID)
                invariant("f094_null_mkdtemp");
            mix((unsigned long long)probe.category());
            break;

        case 3: /* embedded NUL: stat sees only the prefix before the NUL */
            snprintf(path, sizeof(path), "%s/nul%u", sandbox, w1);
            {
                size_t used = strlen(path);
                if (used + 6 < sizeof(path)) {
                    memcpy(path + used, "\0tail", 5);
                    path[used + 5] = '\0';
                }
            }
            if (fs_size(path, &probe) != -1 || !probe.failed_now())
                invariant("f094_nul_size");
            mix((unsigned long long)probe.category());
            break;

        case 4: /* over-long path: PATH_MAX + 128 bytes */
            {
                size_t n = (size_t)(PATH_MAX + 128);
                size_t k;
                if (n >= sizeof(path))
                    n = sizeof(path) - 1;
                for (k = 0; k < n; k++)
                    path[k] = 'x';
                path[n] = '\0';
            }
            if (fs_size(path, &probe) != -1 || !probe.failed_now())
                invariant("f094_long_size");
            mix((unsigned long long)probe.category());
            mix((unsigned long long)strlen(path));
            break;

        case 5: /* template without the XXXXXX suffix: INVALID, no artifact */
            snprintf(path, sizeof(path), "%s/bad%u_ABCDEF", sandbox, w1);
            if (fs_mkdtemp(path, sizeof(path), &probe) != STDLIB_FAIL ||
                probe.category() != STDLIB_CATEGORY_INVALID)
                invariant("f094_bad_template");
            mix((unsigned long long)probe.category());
            break;

        default: /* too-small template buffers: size 0, 5 and 6 */
            snprintf(shortbuf, sizeof(shortbuf), "XXXXXX");
            if (fs_mkdtemp(shortbuf, 0, &probe) != STDLIB_FAIL ||
                probe.category() != STDLIB_CATEGORY_INVALID)
                invariant("f094_size0");
            snprintf(shortbuf, sizeof(shortbuf), "XXXXXX");
            if (fs_mkdtemp(shortbuf, 5, &probe) != STDLIB_FAIL ||
                probe.category() != STDLIB_CATEGORY_INVALID)
                invariant("f094_size5");
            snprintf(shortbuf, sizeof(shortbuf), "XXXXXX");
            if (fs_mkdtemp(shortbuf, 6, &probe) != STDLIB_FAIL ||
                probe.category() != STDLIB_CATEGORY_INVALID)
                invariant("f094_size6");
            mix((unsigned long long)probe.category());
            break;
        }
    }

    /* Cleanup proof: the sandbox is the only artifact with our prefix. */
    after = count_prefix();
    if (baseline >= 0 && after >= 0 && after != baseline + 1)
        invariant("f094_tmp_leak");
    mix((unsigned long long)(after - baseline));
    if (rmdir(sandbox) != 0)
        invariant("f094_rmdir");
    after = count_prefix();
    if (baseline >= 0 && after >= 0 && after != baseline)
        invariant("f094_cleanup_proof");
    mix((unsigned long long)(after - baseline));

    (void)!alarm(0);
    mix((unsigned long long)failures);
    snprintf(hex, sizeof(hex), "%016llx", digest);
    print("f094_digest", hex);
    if (failures == 0)
        print("f094:", CASES, "cases ok");
    else
        print("f094: FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
