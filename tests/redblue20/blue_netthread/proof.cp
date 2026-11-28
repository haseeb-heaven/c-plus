/* blue_netthread — BLUE-TEAM agent 14/20 network/thread safety proof.
 *
 * METHOD
 * ------
 * 2048 deterministic cases from a fixed splitmix64 seed (no pid/clock/network
 * input). Every case runs in its own forked child: the child carries an
 * alarm(5) watchdog and _Exit()s with the case result; the parent reaps the
 * child and records PASS only on a clean exit-0.  The parent carries an
 * alarm(55) watchdog and run.sh wraps the whole run in `timeout 60`, so no
 * case (and no deadlock) can exceed the bound.
 *
 * Coverage (12 families, all must pass on the current tree):
 *   net_invalid_port  - out-of-range ports are refused (no fd allocated)
 *   net_owned         - owned listener/connection handles, accept, drop
 *   net_borrowed      - borrowed handles never close the caller's fd
 *   net_fd_leak       - create/drop cycles do not grow the fd table
 *   http_invalid_url  - malformed URLs are refused with a clear error
 *   http_invalid_hdr  - NULL headers refused; set_headers owns a copy
 *   sync_mtx_destroy  - idempotent destroy: 2nd call refused with EINVAL,
 *                       EBUSY rollback leaves the object retryable
 *   sync_cnd_destroy  - idempotent condvar destroy
 *   sync_join         - double-join refusal + NULL-equivalent handle refusal
 *   sync_join_raii    - RAII thread double-join refusal
 *   sync_generation   - stale/recycled identities: rolled-back slot is
 *                       reusable, consumed identity stays refused
 *   sync_bounded      - side table is bounded (ENOMEM) and checks identity
 *                       before capacity, never dereferencing stale objects
 *   sync_nodeadlock   - mutex/condvar/timedwait/thread coordination finishes
 *
 * OUTPUT: "BLUE20 netthread <cases> <passed> <failed>" + per-family counts.
 * Exit 0 iff failed == 0.  This is executable evidence for the enumerated
 * cases against the current headers/libc, not a formal proof.
 */
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/net.hp"
#include "stdlib/thread.hp"
#include "stdlib/http.hp"
#include "libc/install-include/cplus_threads.h"
#include "libc/install-include/cplus_sync_checked.h"

#define BLUE_CASES 2048u
#define BLUE_SEED 0x4251E4D5A7C0FFEEULL
#define BLUE_FAMILIES 13

static unsigned long long g_cases = 0;
static unsigned long long g_passed = 0;
static unsigned long long g_failed = 0;
static unsigned long long g_family[BLUE_FAMILIES];

static unsigned long long blue_rnd(unsigned long long *s) {
    unsigned long long z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static void blue_on_alarm(int sig) {
    (void)sig;
    _Exit(99);
}

/* --------------------------------------------------------------- helpers */
static int count_fds(void) {
    DIR *d = opendir("/dev/fd");
    struct dirent *e;
    int n = 0;
    if (d == (DIR *)0)
        return -1;
    while ((e = readdir(d)) != (struct dirent *)0)
        ++n;
    closedir(d);
    return n;
}

static int nt_ret(void *arg) { return (int)(uintptr_t)arg; }
static void *nt_retp(void *arg) { return arg; }

/* Globals keep thread arguments out of stack frames (fork-per-case means a
 * single case owns the process, so globals are race-free here). */
static cplus_mtx_t g_dl_m;
static cplus_cnd_t g_dl_c;
static int g_dl_flag;

static int nt_dl_worker(void *arg) {
    (void)arg;
    cplus_mtx_lock(&g_dl_m);
    g_dl_flag = 1;
    cplus_cnd_signal(&g_dl_c);
    cplus_mtx_unlock(&g_dl_m);
    return 0;
}

/* --------------------------------------------------------- NET: ports ----- */
static int c_net_invalid_port(unsigned long long p) {
    int port;
    listener l;
    switch (p % 6u) {
    case 0u: port = -1; break;
    case 1u: port = -65536; break;
    case 2u: port = 65536; break;
    case 3u: port = 70000; break;
    case 4u: port = -123456789; break;
    default: port = 1000000; break;
    }
    l = listener(port);
    if (l.is_valid())
        return 1;                 /* refused port must not create a handle */
    if (l.descriptor() != -1)
        return 2;
    if (l.bound_port() != 0)
        return 3;
    l.drop();                     /* idempotent on an inert handle */
    if (l.is_valid())
        return 4;
    return 0;
}

/* ------------------------------------------------- NET: owned handles ---- */
static int c_net_owned(unsigned long long p) {
    error e = error();
    int port, cli;
    struct sockaddr_in addr;
    connection cn;
    char msg[7];
    listener l = listener(0);
    if (!l.is_valid())
        return 1;
    port = l.bound_port();
    if (port <= 0 || l.descriptor() < 3)
        return 2;
    cli = socket(AF_INET, SOCK_STREAM, 0);
    if (cli < 0)
        return 3;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(0x7F000001u);
    addr.sin_port = htons((unsigned short)port);
    if (connect(cli, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(cli);
        return 4;
    }
    cn = l.accept();
    if (!cn.is_valid() || cn.descriptor() < 3) {
        close(cli);
        return 5;
    }
    msg[0] = 'b'; msg[1] = 'l'; msg[2] = 'u';
    msg[3] = 'e'; msg[4] = '2'; msg[5] = '0'; msg[6] = '\n';
    if (cn.write(msg, sizeof(msg), &e) != STDLIB_OK) {
        cn.drop();
        close(cli);
        return 6;
    }
    cn.drop();
    if (cn.is_valid()) {
        close(cli);
        return 7;
    }
    close(cli);
    l.drop();
    if (l.is_valid())
        return 8;
    (void)p;
    return 0;
}

/* ----------------------------------------------- NET: borrowed handles --- */
static int c_net_borrowed(unsigned long long p) {
    int stream_fd = socket(AF_INET, SOCK_STREAM, 0);
    int zero_fd = (int)(p % 3u);          /* 0, 1 or 2: process-owned fds */
    listener lb;
    connection cb;
    listener lz;
    connection cz;
    if (stream_fd < 3)
        return 1;
    lb = listener_borrow(stream_fd);
    if (!lb.is_valid() || lb.descriptor() != stream_fd)
        return 2;
    lb.drop();
    if (fcntl(stream_fd, F_GETFD) < 0)
        return 3;                         /* borrowed drop must NOT close */
    cb = connection_borrow(stream_fd);
    if (!cb.is_valid() || cb.descriptor() != stream_fd)
        return 4;
    cb.drop();
    if (fcntl(stream_fd, F_GETFD) < 0)
        return 5;
    close(stream_fd);
    lz = listener_borrow(zero_fd);
    if (lz.is_valid())
        return 6;
    lz.drop();
    cz = connection_borrow(-1);
    if (cz.is_valid())
        return 7;
    cz.drop();
    return 0;
}

/* --------------------------------------------------- NET: no fd leaks ---- */
static int c_net_fd_leak(unsigned long long p) {
    int cycles = 8 + (int)(p & 15u);
    int before = count_fds();
    int after, i;
    if (before < 0)
        return 1;
    for (i = 0; i < cycles; ++i) {
        listener l = listener(0);
        if (!l.is_valid())
            return 2;
        l.drop();
        if (l.is_valid())
            return 3;
    }
    after = count_fds();
    if (after != before)
        return 4;
    return 0;
}

/* ------------------------------------------------- HTTP: invalid URL ---- */
static int c_http_invalid_url(unsigned long long p) {
    error e = error();
    http_client hc = http_client(&e);
    char buf[32];
    const char *url;
    switch (p % 7u) {
    case 0u: url = ""; break;
    case 1u: url = "not-a-url"; break;
    case 2u: url = "://bad"; break;
    case 3u: url = "http://"; break;
    case 4u: url = "ht tp://bad"; break;
    case 5u: url = "%%%"; break;
    default: url = "\x01\x02\x03"; break;
    }
    if (!hc.valid())
        return 1;
    if (hc.set_timeout(1, &e) != STDLIB_OK)
        return 2;
    if (hc.get(url, buf, sizeof(buf), &e) != STDLIB_FAIL)
        return 3;                         /* malformed URL must be refused */
    if (e.category() == STDLIB_CATEGORY_OK)
        return 4;
    hc.drop();
    if (hc.valid())
        return 5;
    return 0;
}

/* ------------------------------------------------ HTTP: invalid header --- */
static int c_http_invalid_hdr(unsigned long long p) {
    error e = error();
    error e2 = error();
    http_headers hh = http_headers();
    http_client hc = http_client(&e2);
    http_headers *np = (http_headers *)0;
    const char *lines[3];
    int want = 1 + (int)(p % 3u);
    int i;
    if (!hc.valid())
        return 1;
    if (hh.add((const char *)0, &e) != STDLIB_FAIL)
        return 2;                         /* NULL header line refused */
    if (e.code() != (int)CPLUS_EXIT_UNSUPPORTED)
        return 3;
    lines[0] = "X-Blue20: one";
    lines[1] = "X-Blue20: two";
    lines[2] = "X-Blue20: three";
    for (i = 0; i < want; ++i) {
        if (hh.add(lines[i], &e) != STDLIB_OK)
            return 4;
    }
    if (!hh.valid())
        return 5;
    if (hc.set_headers(&hh, &e) != STDLIB_OK)
        return 6;
    if (hc.header_lines() != want)
        return 7;
    hh.drop();
    if (hc.header_lines() != want)
        return 8;                         /* set_headers owns a copy */
    if (hc.set_headers(np, &e) != STDLIB_FAIL)
        return 9;                         /* NULL headers refused */
    hc.drop();
    return 0;
}

/* --------------------------------------- SYNC: idempotent mutex destroy -- */
static int c_sync_mtx_destroy(unsigned long long p) {
    cplus_mtx_t m;
    cplus_mtx_t *np = (cplus_mtx_t *)0;
    if (cplus_mtx_init(&m, CPLUS_MTX_PLAIN) != CPLUS_THRD_SUCCESS)
        return 1;
    if ((p & 1u) == 0u) {
        /* EBUSY failure must roll the claim back so a retry is possible. */
        if (cplus_mtx_lock(&m) != CPLUS_THRD_SUCCESS)
            return 2;
        if (cplus_mtx_destroy_checked(&m) != EBUSY)
            return 3;
        if (cplus_mtx_unlock(&m) != CPLUS_THRD_SUCCESS)
            return 4;
    }
    if (cplus_mtx_destroy_checked(&m) != 0)
        return 5;
    if (cplus_mtx_destroy_checked(&m) != EINVAL)
        return 6;                         /* second destroy refused */
    if (cplus_mtx_destroy_checked(np) != EINVAL)
        return 7;
    return 0;
}

/* --------------------------------------- SYNC: idempotent cond destroy --- */
static int c_sync_cnd_destroy(unsigned long long p) {
    cplus_cnd_t c;
    cplus_cnd_t *np = (cplus_cnd_t *)0;
    (void)p;
    if (cplus_cnd_init(&c) != CPLUS_THRD_SUCCESS)
        return 1;
    if (cplus_cnd_signal(&c) != CPLUS_THRD_SUCCESS)
        return 2;                         /* signal with no waiter is benign */
    if (cplus_cnd_destroy_checked(&c) != 0)
        return 3;
    if (cplus_cnd_destroy_checked(&c) != EINVAL)
        return 4;                         /* second destroy refused */
    if (cplus_cnd_destroy_checked(np) != EINVAL)
        return 5;
    return 0;
}

/* ----------------------------------------- SYNC: double-join refusal ----- */
static int c_sync_join(unsigned long long p) {
    cplus_thrd_t t;
    cplus_thrd_t z;
    int res = 0;
    int want = (int)(p & 0xFFu);
    memset(&z, 0, sizeof(z));
    if (cplus_thrd_join_checked(z, &res) != EINVAL)
        return 1;                         /* NULL-equivalent handle refused */
    if (cplus_thrd_create(&t, nt_ret, (void *)(uintptr_t)want) != CPLUS_THRD_SUCCESS)
        return 2;
    if (cplus_thrd_join_checked(t, &res) != 0)
        return 3;
    if (res != want)
        return 4;
    if (cplus_thrd_join_checked(t, &res) != EINVAL)
        return 5;                         /* second join refused */
    return 0;
}

/* ----------------------------------- SYNC: RAII double-join refusal ------ */
static int c_sync_join_raii(unsigned long long p) {
    error e = error();
    thread th = thread();
    void *out = (void *)0;
    (void)p;
    if (th.is_joinable())
        return 1;
    if (th.join(&out, &e) != STDLIB_FAIL)
        return 2;                         /* join of non-joinable refused */
    if (th.start(nt_retp, (void *)(uintptr_t)0x5Au, &e) != STDLIB_OK)
        return 3;
    if (th.join(&out, &e) != STDLIB_OK)
        return 4;
    if (out != (void *)(uintptr_t)0x5Au)
        return 5;
    if (th.join(&out, &e) != STDLIB_FAIL)
        return 6;                         /* RAII double join refused */
    if (e.category() != STDLIB_CATEGORY_LIFECYCLE)
        return 7;
    th.drop();
    return 0;
}

/* ------------------------------ SYNC: stale generation / slot recycling - */
static int c_sync_generation(unsigned long long p) {
    cplus_mtx_t a;
    cplus_mtx_t b;
    (void)p;
    if (cplus_mtx_init(&a, CPLUS_MTX_PLAIN) != CPLUS_THRD_SUCCESS)
        return 1;
    /* Claim a, then force a rollback (EBUSY); the recycled slot must take a
     * fresh identity/generation and a must become claimable again. */
    if (cplus_mtx_lock(&a) != CPLUS_THRD_SUCCESS)
        return 2;
    if (cplus_mtx_destroy_checked(&a) != EBUSY)
        return 3;
    if (cplus_mtx_unlock(&a) != CPLUS_THRD_SUCCESS)
        return 4;
    if (cplus_mtx_init(&b, CPLUS_MTX_PLAIN) != CPLUS_THRD_SUCCESS)
        return 5;
    if (cplus_mtx_destroy_checked(&b) != 0)
        return 6;                         /* recycled slot accepts b */
    if (cplus_mtx_destroy_checked(&a) != 0)
        return 7;                         /* rollback fully released a */
    if (cplus_mtx_destroy_checked(&a) != EINVAL)
        return 8;                         /* consumed a stays stale/refused */
    if (cplus_mtx_destroy_checked(&b) != EINVAL)
        return 9;                         /* consumed b stays stale/refused */
    return 0;
}

/* -------------------------------------- SYNC: bounded side table (ENOMEM) */
static int c_sync_bounded(unsigned long long p) {
    cplus_mtx_t *arr;
    cplus_mtx_t extra;
    unsigned int i;
    (void)p;
    /* Heap-backed so distinct addresses are guaranteed; the C+ rewriter does
     * not instrument pointer subscripts the way it does typedef'd arrays. */
    arr = (cplus_mtx_t *)stdlib_alloc(sizeof(cplus_mtx_t) *
                                      CPLUS_SYNC_CHECKED_CAPACITY);
    if (arr == (cplus_mtx_t *)0)
        return 6;
    for (i = 0; i < CPLUS_SYNC_CHECKED_CAPACITY; ++i) {
        if (cplus_mtx_init(&arr[i], CPLUS_MTX_PLAIN) != CPLUS_THRD_SUCCESS)
            return 1;
    }
    for (i = 0; i < CPLUS_SYNC_CHECKED_CAPACITY; ++i) {
        if (cplus_mtx_destroy_checked(&arr[i]) != 0)
            return 2;
    }
    if (cplus_mtx_init(&extra, CPLUS_MTX_PLAIN) != CPLUS_THRD_SUCCESS)
        return 3;
    if (cplus_mtx_destroy_checked(&extra) != ENOMEM)
        return 4;                         /* bounded refusal, no deref */
    if (cplus_mtx_destroy_checked(&arr[0]) != EINVAL)
        return 5;                         /* identity checked before capacity */
    stdlib_release(arr);
    return 0;
}

/* -------------------------------------------- SYNC: no deadlock ---------- */
static int c_sync_nodeadlock(unsigned long long p) {
    cplus_thrd_t t;
    int res = 0;
    int wr;
    struct timespec deadline;
    error e = error();
    thread rt = thread();
    (void)p;
    g_dl_flag = 0;
    if (cplus_mtx_init(&g_dl_m, CPLUS_MTX_PLAIN) != CPLUS_THRD_SUCCESS)
        return 1;
    if (cplus_cnd_init(&g_dl_c) != CPLUS_THRD_SUCCESS)
        return 2;
    if (cplus_thrd_create(&t, nt_dl_worker, (void *)0) != CPLUS_THRD_SUCCESS)
        return 3;
    if (cplus_mtx_lock(&g_dl_m) != CPLUS_THRD_SUCCESS)
        return 4;
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) {
        cplus_mtx_unlock(&g_dl_m);
        return 5;
    }
    deadline.tv_sec += 2;
    /* Predicate loop: the worker may signal before we reach the wait, and a
     * condvar does not remember a signal. Checking the flag while holding the
     * mutex (the worker sets it under the same mutex) makes this race-free. */
    while (g_dl_flag == 0) {
        wr = cplus_cnd_timedwait(&g_dl_c, &g_dl_m, &deadline);
        if (wr != CPLUS_THRD_SUCCESS)
            break;                        /* would be a deadlock/hang */
    }
    if (g_dl_flag != 1) {
        cplus_mtx_unlock(&g_dl_m);
        return 6;
    }
    cplus_mtx_unlock(&g_dl_m);
    if (cplus_thrd_join_checked(t, &res) != 0)
        return 8;
    if (rt.start(nt_retp, (void *)0, &e) != STDLIB_OK)
        return 9;
    rt.drop();                            /* drop() must join, never hang */
    if (cplus_mtx_destroy_checked(&g_dl_m) != 0)
        return 10;
    if (cplus_cnd_destroy_checked(&g_dl_c) != 0)
        return 11;
    return 0;
}

/* ------------------------------------------------------------- dispatcher */
static int dispatch_case(int idx, unsigned long long param) {
    switch (idx) {
    case 0: return c_net_invalid_port(param);
    case 1: return c_net_owned(param);
    case 2: return c_net_borrowed(param);
    case 3: return c_net_fd_leak(param);
    case 4: return c_http_invalid_url(param);
    case 5: return c_http_invalid_hdr(param);
    case 6: return c_sync_mtx_destroy(param);
    case 7: return c_sync_cnd_destroy(param);
    case 8: return c_sync_join(param);
    case 9: return c_sync_join_raii(param);
    case 10: return c_sync_generation(param);
    case 11: return c_sync_bounded(param);
    case 12: return c_sync_nodeadlock(param);
    default: return 1;
    }
}

static const char *family_name(int idx) {
    switch (idx) {
    case 0: return "net_invalid_port";
    case 1: return "net_owned";
    case 2: return "net_borrowed";
    case 3: return "net_fd_leak";
    case 4: return "http_invalid_url";
    case 5: return "http_invalid_hdr";
    case 6: return "sync_mtx_destroy";
    case 7: return "sync_cnd_destroy";
    case 8: return "sync_join";
    case 9: return "sync_join_raii";
    case 10: return "sync_generation";
    case 11: return "sync_bounded";
    case 12: return "sync_nodeadlock";
    default: return "unknown";
    }
}

static int run_child(int idx, unsigned long long param) {
    pid_t pid;
    int status = 0;
    int out = 1;
    pid = fork();
    if (pid == (pid_t)0) {
        signal(SIGALRM, blue_on_alarm);
        alarm(5);
        _Exit(dispatch_case(idx, param));
    }
    if (pid < (pid_t)0)
        return 1;                         /* fork failure fails the case */
    if (waitpid(pid, &status, 0) == pid) {
        if (WIFEXITED(status))
            out = WEXITSTATUS(status);
        else
            out = 1;                      /* signal: watchdog / crash */
    }
    return out;
}

int main(void) {
    unsigned long long seed = BLUE_SEED;
    unsigned long long i, idx, param;
    int rc;
    signal(SIGALRM, blue_on_alarm);
    alarm(55);
    for (i = 0; i < BLUE_CASES; ++i) {
        idx = blue_rnd(&seed) % (unsigned long long)BLUE_FAMILIES;
        param = blue_rnd(&seed);
        ++g_cases;
        ++g_family[idx];
        rc = run_child((int)idx, param);
        if (rc == 0)
            ++g_passed;
        else {
            ++g_failed;
            fprintf(stderr, "FAILCASE F=%s idx=%llu param=%llu rc=%d\n",
                    family_name((int)idx), idx, param, rc);
        }
    }
    alarm(0);
    printf("BLUE20 netthread %llu %llu %llu\n", g_cases, g_passed, g_failed);
    for (idx = 0; idx < BLUE_FAMILIES; ++idx)
        printf("F %s %llu\n", family_name((int)idx), g_family[idx]);
    return g_failed == 0 ? 0 : 1;
}
