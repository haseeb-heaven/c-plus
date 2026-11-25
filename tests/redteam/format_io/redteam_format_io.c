/*
 * REDTEAM format_io -- adversarial harness for the checked C+ format / I/O APIs.
 *
 * Attacker posture:
 *   - Fixed PRNG seed; every case is deterministic and replayable.
 *   - Every case executes in a forked child. stdout in every child is redirected
 *     to /dev/null so printf-family output never pollutes the log.
 *   - Each case reports raw facts (refused?, canary intact?, return code, errno)
 *     over a pipe. The parent classifies REFUSED / CRASH / CORRUPT /
 *     UNEXPECTED_OK / OK and appends one line per case to scenarios.log.
 *
 * Genuine finding = CRASH / CORRUPT / UNEXPECTED_OK where the API promises a
 * refusal. The two such classes exercised here are:
 *   F1: %n detector bypass via repeated positional specifiers (e.g. "%1$2$n")
 *       -> real write into an attacker-chosen int (CORRUPT) or SIGSEGV (CRASH).
 *   F2: destination-capacity check bypass for a struct member because
 *       __builtin_object_size(dest, 0) returns the enclosing struct size
 *       -> adjacent field overwritten (CORRUPT).
 *
 * No source files are modified by this harness. Artifacts live next to it.
 */
#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "safe_format.h"
#include "safe_io.h"
#include "safe_runtime.h"

#define NCASES 6000

/* ---------------- deterministic PRNG ---------------- */
static uint64_t g_state = 0x52454454464D4931ULL; /* "REDTFIO1" */
static uint64_t rnd(void) {
    g_state = g_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return g_state >> 17;
}
static size_t rnd_range(size_t lo, size_t hi) { /* inclusive */
    return lo + (size_t)(rnd() % (uint64_t)(hi - lo + 1));
}

/* ---------------- case catalogue ---------------- */
enum {
    K_PCTN_DIRECT = 0,
    K_PCTN_BYPASS,
    K_PCTN_BYPASS_CRASH,
    K_STRUCT_MEMBER,
    K_FLAT_ONEOVER,
    K_NONLITERAL,
    K_NULL_FMT,
    K_NULL_STREAM,
    K_HUGE_WIDTH,
    K_SCAN_UNBOUNDED,
    K_SCAN_SAFE,
    K_DOUBLE_CLOSE,
    K_FREAD_BOUNDS,
    K_FWRITE_BOUNDS,
    K_HUGE_COUNT,
    K_IO_NULL,
    K_FOPEN_TRAVERSAL,
    NKINDS
};

static const char *kind_name(int k) {
    static const char *n[NKINDS] = {
        "PCTN_DIRECT", "PCTN_BYPASS", "PCTN_BYPASS_CRASH", "STRUCT_MEMBER",
        "FLAT_ONEOVER", "NONLITERAL", "NULL_FMT", "NULL_STREAM",
        "HUGE_WIDTH", "SCAN_UNBOUNDED", "SCAN_SAFE", "DOUBLE_CLOSE",
        "FREAD_BOUNDS", "FWRITE_BOUNDS", "HUGE_COUNT", "IO_NULL",
        "FOPEN_TRAVERSAL"
    };
    return (k >= 0 && k < NKINDS) ? n[k] : "UNKNOWN";
}
static const char *kind_api(int k) {
    static const char *n[NKINDS] = {
        "printf_s", "printf_s", "printf_s", "snprintf_s",
        "snprintf_s", "printf_s", "printf_s", "fprintf_s",
        "snprintf_s", "sscanf_s", "sscanf_s", "fclose_s",
        "fread_s", "fwrite_s", "fread_s", "fwrite_s", "fopen_s"
    };
    return (k >= 0 && k < NKINDS) ? n[k] : "?";
}
/* Kinds for which the contract promises a refusal. */
static int kind_expects_refusal(int k) {
    switch (k) {
    case K_PCTN_DIRECT:
    case K_PCTN_BYPASS:         /* F1: promise -> real %n write must be refused */
    case K_PCTN_BYPASS_CRASH:   /* F1: promise -> wild vararg access must be refused */
    case K_STRUCT_MEMBER:       /* F2: promise -> capacity overflow must be refused */
    case K_FLAT_ONEOVER:
    case K_NONLITERAL:
    case K_NULL_FMT:
    case K_NULL_STREAM:
    case K_SCAN_UNBOUNDED:
    case K_DOUBLE_CLOSE:
    case K_IO_NULL:
        return 1;
    default:
        return 0;
    }
}

/* Raw facts reported by a child over its pipe. */
struct facts {
    int refused;
    int canary_ok;
    long rc;
    int err;
    int wrote;
};

static const char *g_trav_path; /* set by parent */

/* ---------------- child-side attack cases ---------------- */
static void child_run(int kind, uint64_t r1, uint64_t r2, struct facts *f) {
    int rc = -999;
    int canary_ok = 1;
    int refused = 0;
    int wrote = 0;
    char fmt[96];

    f->refused = 0;
    f->canary_ok = 1;
    f->rc = -999;
    f->err = 0;
    f->wrote = 0;

    switch (kind) {
    case K_PCTN_DIRECT: {
        /* Forbidden %n in ordinary spellings must be refused. */
        static const char *fmts[] = {
            "%n", "%hhn", "%lln", "%zn", "%1$n", "AB%n", "%*n", "%5.3n"
        };
        volatile int sink = 0x24681357;
        const char *p = fmts[r1 % 8];
        errno = 0;
        rc = cplus_printf_s(p, (int *)&sink, (int *)&sink, (int *)&sink);
        refused = (rc == -1 && errno == EINVAL);
        canary_ok = (sink == 0x24681357);
        wrote = !canary_ok;
        break;
    }
    case K_PCTN_BYPASS: {
        /* FINDING F1: repeated positional specifiers defeat the %n scanner.
         * The format is assembled at runtime (attacker-controlled, non-literal). */
        static const char *fmts[] = {
            "%1$2$n", "%2$1$n", "%1$2$hn", "ABCD%1$2$n",
            "%1$2$3$n", "%1$-2$n", "%01$02$n", "%1$2$lln"
        };
        volatile int sink = 0x24681357;
        const char *p = fmts[r1 % 8];
        errno = 0;
        rc = cplus_printf_s(p, (int *)&sink, (int *)&sink, (int *)&sink,
                            (int *)&sink, (int *)&sink, (int *)&sink,
                            (int *)&sink, (int *)&sink);
        refused = (rc == -1 && errno == EINVAL);
        canary_ok = (sink == 0x24681357);
        wrote = !canary_ok;
        break;
    }
    case K_PCTN_BYPASS_CRASH: {
        /* FINDING F1 (crash form): higher positional index than supplied
         * arguments makes libc read a wild vararg after the check has passed. */
        errno = 0;
        volatile int sink = 0x24681357;
        rc = cplus_printf_s("%11$22$n", (int *)&sink, (int *)&sink,
                            (int *)&sink, (int *)&sink);
        (void)rc; /* a SIGSEGV here is the observation */
        canary_ok = (sink == 0x24681357);
        break;
    }
    case K_STRUCT_MEMBER: {
        /* FINDING F2: object-size(type 0) of a struct member is the whole
         * struct, so dmax == sizeof(struct) passes and overflows the member. */
        struct S { char a[8]; char can[8]; } s;
        memset(&s, 0x5A, sizeof s);
        const char *payload = "0123456789ABCDE"; /* 15 chars + NUL = 16 bytes */
        errno = 0;
        rc = snprintf_s(s.a, 16, "%s", payload);
        canary_ok = 1;
        for (size_t i = 0; i < sizeof s.can; ++i)
            if ((unsigned char)s.can[i] != 0x5A) canary_ok = 0;
        wrote = !canary_ok;
        break;
    }
    case K_FLAT_ONEOVER: {
        /* Control: a flat array's one-byte-too-small dmax IS refused. */
        char b[16];
        char guard[16];
        memset(b, 0x5A, sizeof b);
        memset(guard, 0x5A, sizeof guard);
        errno = 0;
        rc = snprintf_s(b, 17, "%s", "0123456789ABCDE");
        refused = (rc == -1 && errno == EINVAL);
        canary_ok = (b[0] == 0x5A);
        for (size_t i = 0; i < sizeof guard; ++i)
            if ((unsigned char)guard[i] != 0x5A) canary_ok = 0;
        break;
    }
    case K_NONLITERAL: {
        char fmtbuf[32];
        snprintf(fmtbuf, sizeof fmtbuf, "hello %s", "%d");
        errno = 0;
        rc = cplus_printf_s_checked(0, fmtbuf, 1);
        refused = (rc == -1 && errno == EINVAL);
        break;
    }
    case K_NULL_FMT: {
        errno = 0;
        rc = cplus_printf_s(NULL, 1);
        refused = (rc == -1 && errno == EINVAL);
        break;
    }
    case K_NULL_STREAM: {
        errno = 0;
        rc = cplus_fprintf_s(NULL, "%s", "x");
        refused = (rc == -1 && errno == EINVAL);
        break;
    }
    case K_HUGE_WIDTH: {
        size_t w = 1 + (size_t)(r1 % 100000);
        char b[32], guard[16];
        memset(b, 0x5A, sizeof b);
        memset(guard, 0x5A, sizeof guard);
        snprintf(fmt, sizeof fmt, "%%%zus", w);
        errno = 0;
        rc = cplus_snprintf_s(b, sizeof b, fmt, "x");
        canary_ok = (guard[0] == 0x5A);
        /* b must be NUL-terminated within its real capacity. */
        {
            int terminated = 0;
            for (size_t i = 0; i < sizeof b; ++i) if (b[i] == '\0') { terminated = 1; break; }
            if (!terminated) canary_ok = 0;
        }
        break;
    }
    case K_SCAN_UNBOUNDED: {
        static const char *fmts[] = { "%s", "%[a-z]", "%%%s" };
        char dest[8];
        memset(dest, 0x5A, sizeof dest);
        errno = 0;
        rc = cplus_sscanf_s("abcdefghijklmnop", fmts[r1 % 3], dest, sizeof dest);
        refused = (rc == -1 && errno == EINVAL);
        canary_ok = (dest[0] == 0x5A);
        break;
    }
    case K_SCAN_SAFE: {
        static const char *fmts[] = { "%7s", "%d", "%7[a-z]" };
        char dest[8];
        int v = 0;
        memset(dest, 0x5A, sizeof dest);
        errno = 0;
        rc = (r1 & 1) ? cplus_sscanf_s("123 456", fmts[r1 % 3], &v)
                      : cplus_sscanf_s("abcdefghij", fmts[r1 % 3], dest, sizeof dest);
        canary_ok = 1;
        break;
    }
    case K_DOUBLE_CLOSE: {
        FILE *f = tmpfile();
        if (!f) { canary_ok = 1; break; }
        errno = 0;
        (void)cplus_fclose_s(f);
        errno = 0;
        rc = cplus_fclose_s(f);
        refused = (rc == EOF && errno == EINVAL);
        break;
    }
    case K_FREAD_BOUNDS: {
        FILE *f = tmpfile();
        unsigned char buf[48];
        memset(buf, 0x5A, sizeof buf);
        if (!f) break;
        rewind(f);
        errno = 0;
        rc = (int)cplus_fread_s(buf, 8, 1, (size_t)(r1 % 100 + 1), f);
        canary_ok = 1;
        for (size_t i = 8; i < sizeof buf; ++i)
            if (buf[i] != 0x5A) canary_ok = 0;
        (void)cplus_fclose_s(f);
        break;
    }
    case K_FWRITE_BOUNDS: {
        FILE *f = tmpfile();
        unsigned char buf[48];
        memset(buf, 0x41, sizeof buf);
        if (!f) break;
        errno = 0;
        rc = (int)cplus_fwrite_s(buf, 8, 1, (size_t)(r1 % 100 + 1), f);
        canary_ok = 1; /* bmax clamp must not read past 8 bytes of source */
        (void)cplus_fclose_s(f);
        break;
    }
    case K_HUGE_COUNT: {
        FILE *f = tmpfile();
        unsigned char buf[16];
        memset(buf, 0x5A, sizeof buf);
        if (!f) break;
        errno = 0;
        rc = (int)cplus_fread_s(buf, sizeof buf, 1, (size_t)-1, f);
        canary_ok = 1;
        (void)cplus_fclose_s(f);
        break;
    }
    case K_IO_NULL: {
        FILE *f = tmpfile();
        unsigned char buf[16];
        memset(buf, 0x5A, sizeof buf);
        errno = 0;
        if (r1 & 1) rc = (int)cplus_fread_s(buf, sizeof buf, 1, 4, NULL);
        else        rc = (int)cplus_fwrite_s(NULL, sizeof buf, 1, 4, f);
        refused = (rc == 0);
        if (f) (void)cplus_fclose_s(f);
        break;
    }
    case K_FOPEN_TRAVERSAL: {
        /* No confinement is promised by fopen_s; this is coverage only. */
        FILE *f = cplus_fopen_s(g_trav_path, "rb");
        if (f) { rc = 0; (void)cplus_fclose_s(f); }
        else   rc = 1;
        canary_ok = 1;
        break;
    }
    default:
        break;
    }

    f->refused = refused;
    f->canary_ok = canary_ok;
    f->rc = (long)rc;
    f->err = errno;
    f->wrote = wrote;
}

/* ---------------- parent ---------------- */
int main(int argc, char **argv) {
    const char *logpath = (argc > 1) ? argv[1] : "scenarios.log";
    const char *sumpath = (argc > 2) ? argv[2] : "summary.txt";
    char tmpbase[256];
    char travdir[320];
    FILE *log, *sum;

    if (!getenv("TMPDIR")) setenv("TMPDIR", "/tmp", 1);
    snprintf(tmpbase, sizeof tmpbase, "%s/rt_fmtio_XXXXXX", getenv("TMPDIR"));
    if (!mkdtemp(tmpbase)) { fprintf(stderr, "mkdtemp failed\n"); return 1; }
    snprintf(travdir, sizeof travdir, "%s/sub", tmpbase);
    mkdir(travdir, 0700);
    {
        char p[400], cmd[512];
        snprintf(p, sizeof p, "%s/payload.txt", tmpbase);
        FILE *q = fopen(p, "wb");
        if (q) { fputs("REDTEAM", q); fclose(q); }
        snprintf(cmd, sizeof cmd, "%s/sub/../payload.txt", tmpbase);
        g_trav_path = strdup(cmd);
    }

    log = fopen(logpath, "w");
    sum = fopen(sumpath, "w");
    if (!log || !sum) { fprintf(stderr, "cannot open outputs\n"); return 1; }

    /* Keep harness stdout out of the log: children additionally redirect to
     * /dev/null. The runner redirects this process's stdout to a sink file. */
    fflush(stdout);

    long n_refused = 0, n_crash = 0, n_corrupt = 0, n_unexpected = 0, n_ok = 0;

    for (int id = 1; id <= NCASES; ++id) {
        uint64_t r1 = rnd(), r2 = rnd();
        int kind = (int)((uint64_t)(id - 1) % (uint64_t)NKINDS);
        int pipefd[2];
        struct facts f;
        int status = 0;
        const char *cls;

        if (pipe(pipefd) != 0) { fprintf(stderr, "pipe failed\n"); return 1; }
        fflush(NULL);
        pid_t pid = fork();
        if (pid < 0) { fprintf(stderr, "fork failed\n"); return 1; }
        if (pid == 0) {
            close(pipefd[0]);
            /* Redirect stdout to a sink for every child. */
            if (!freopen("/dev/null", "w", stdout)) _exit(127);
            cplus_runtime_set_violation_trap(0); /* refusals return, not exit(70) */
            memset(&f, 0, sizeof f);
            child_run(kind, r1, r2, &f);
            {
                char line[160];
                int n = snprintf(line, sizeof line, "%d %d %ld %d %d\n",
                                 f.refused, f.canary_ok, f.rc, f.err, f.wrote);
                (void)!write(pipefd[1], line, (size_t)n);
            }
            close(pipefd[1]);
            _exit(0);
        }
        close(pipefd[1]);
        memset(&f, 0, sizeof f);
        {
            char line[160];
            ssize_t got = read(pipefd[0], line, sizeof line - 1);
            if (got > 0) {
                line[got] = '\0';
                if (sscanf(line, "%d %d %ld %d %d", &f.refused, &f.canary_ok,
                           &f.rc, &f.err, &f.wrote) != 5) {
                    f.refused = 0; f.canary_ok = 1;
                }
            } else {
                f.canary_ok = 1;
            }
        }
        close(pipefd[0]);
        while (waitpid(pid, &status, 0) < 0) { }

        if (WIFSIGNALED(status)) {
            cls = "CRASH";
            ++n_crash;
        } else if (!f.canary_ok) {
            cls = "CORRUPT";
            ++n_corrupt;
        } else if (kind_expects_refusal(kind) && !f.refused) {
            cls = "UNEXPECTED_OK";
            ++n_unexpected;
        } else if (f.refused) {
            cls = "REFUSED";
            ++n_refused;
        } else {
            cls = "OK";
            ++n_ok;
        }

        fprintf(log, "C %d %s %s %s canary=%d refused=%d rc=%ld err=%d r1=%016llx r2=%016llx\n",
                id, kind_api(kind), kind_name(kind), cls, f.canary_ok, f.refused,
                f.rc, f.err, (unsigned long long)r1, (unsigned long long)r2);
    }

    fprintf(log, "REDTEAM format_io %d %ld %ld %ld %ld\n",
            NCASES, n_refused, n_crash, n_corrupt, n_unexpected);
    fprintf(sum, "REDTEAM format_io %d %ld %ld %ld %ld\n",
            NCASES, n_refused, n_crash, n_corrupt, n_unexpected);
    fclose(log);
    fclose(sum);

    /* Clean up our own temp tree. */
    {
        char p[400];
        snprintf(p, sizeof p, "%s/sub", tmpbase); rmdir(p);
        snprintf(p, sizeof p, "%s/payload.txt", tmpbase); unlink(p);
        rmdir(tmpbase);
    }

    printf("REDTEAM format_io %d %ld %ld %ld %ld\n",
           NCASES, n_refused, n_crash, n_corrupt, n_unexpected);
    fflush(stdout);
    return 0;
}
