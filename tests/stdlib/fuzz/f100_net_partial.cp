/* F-098 -- deterministic fuzz configuration: net partial reads / EOF.
 *
 * A forked child writes a patterned payload in bounded chunks over an AF_UNIX
 * socketpair with a 0.5 ms inter-chunk delay; the parent wraps its end with
 * connection_borrow() and reads with a seeded small capacity so that
 * connection::read() must return short counts before STDLIB_EOF.  A second
 * round drives connection::write() with a 64 KiB payload to exercise its
 * short-write retry loop while the child drains the peer.  No external
 * service, no fixed ports, no unbounded waits: poll() with a 2 s cap on every
 * read, a SIGALRM watchdog and an external `timeout 30s`.
 *
 * Digest folds payload/capacity parameters, byte totals, result codes and
 * error categories.  The *number* of read() calls is deliberately excluded:
 * it depends on how the kernel chunks arriving data, not on library behavior.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/net.hp"

#define ROUNDS 12
#define BIG_WRITE 65536
#define FNV_PRIME 1099511628211ULL

static unsigned long long rng_state = 0xF0981234ULL;
static unsigned long long digest = 0xcbf29ce484222325ULL;
static int failures = 0;

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
    static const char msg[] = "F098 watchdog timeout\n";
    (void)sig;
    (void)!write(2, msg, sizeof(msg) - 1);
    _exit(90);
}

static void on_ignore(int sig) {
    (void)sig;
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

static unsigned char pattern(int seed, int index) {
    return (unsigned char)(seed * 13 + index * 29 + 5);
}

/* Forked writer -> borrowed connection reader with short reads. */
static int partial_round(int round, int len, int cap, int chunk) {
    int sp[2];
    int params[3];
    int c2p[2];
    pid_t kid;
    int status = 0;
    int got = 0;
    int ok = 1;
    int n;
    int k;
    int written = 0;
    char buf[256];
    char ack = 0;
    struct pollfd pfd;
    error eof_err = error();

    if (cap > (int)sizeof(buf))
        cap = (int)sizeof(buf);
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0 ||
        pipe(c2p) != 0)
        return -1;
    kid = fork();
    if (kid < 0) {
        close(sp[0]); close(sp[1]);
        close(c2p[0]); close(c2p[1]);
        return -1;
    }
    if (kid == 0) {
        int child_len = 0;
        int child_chunk = 1;
        close(sp[0]);
        close(c2p[1]);
        if (read(c2p[0], params, sizeof(params)) != (ssize_t)sizeof(params))
            _exit(61);
        child_len = params[0];
        child_chunk = params[1];
        while (written < child_len) {
            int take = child_chunk;
            if (take > child_len - written)
                take = child_len - written;
            for (k = 0; k < take; k++)
                buf[k] = (char)pattern(round, written + k);
            if (write(sp[1], buf, (size_t)take) != (ssize_t)take)
                _exit(62);
            written += take;
            if (usleep(500) != 0 && errno != 0)
                _exit(63);
        }
        close(sp[1]);
        close(c2p[0]);
        _exit(0);
    }

    /* Parent: parameters go through a pipe, never through ambient state. */
    close(sp[1]);
    close(c2p[0]);
    params[0] = len;
    params[1] = chunk;
    params[2] = cap;
    if (write(c2p[1], params, sizeof(params)) != (ssize_t)sizeof(params))
        ok = 0;
    {
        connection peer = connection_borrow(sp[0]);
        while (got < len) {
            pfd.fd = peer.descriptor();
            pfd.events = POLLIN;
            pfd.revents = 0;
            if (poll(&pfd, 1, 2000) != 1 || !(pfd.revents & POLLIN)) {
                ok = 0;
                break;
            }
            n = peer.read(buf, (size_t)cap, &eof_err);
            if (n < 1) {
                /* Only EOF is legal before the whole payload is consumed. */
                if (n != STDLIB_EOF || !eof_err.ok())
                    ok = 0;
                break;
            }
            if (n > cap) {
                ok = 0;
                break;
            }
            for (k = 0; k < n; k++) {
                if ((unsigned char)buf[k] != pattern(round, got + k))
                    ok = 0;
            }
            got += n;
        }
        /* After the child closes, the next read must be a clean EOF. */
        pfd.fd = peer.descriptor();
        pfd.events = POLLIN;
        pfd.revents = 0;
        if (poll(&pfd, 1, 2000) == 1 &&
            (pfd.revents & (POLLIN | POLLHUP))) {
            eof_err.clear();
            n = peer.read(buf, (size_t)cap, &eof_err);
            if (n != STDLIB_EOF || !eof_err.ok())
                ok = 0;
        } else {
            ok = 0;
        }
        mix((unsigned long long)got);
        peer.drop(); /* borrowed end must stay open until the explicit close */
    }
    close(sp[0]);
    close(c2p[1]);
    (void)ack;
    waitpid(kid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        ok = 0;
    if (got != len)
        ok = 0;
    return ok;
}

/* Forked drainer -> borrowed connection writer, exercising short writes. */
static int big_write_round(void) {
    int sp[2];
    int c2p[2];
    pid_t kid;
    int status = 0;
    int ok = 1;
    int k;
    long total = 0;
    char ack = 0;
    error werr = error();
    char *data = (char *)malloc((size_t)BIG_WRITE);

    if (data == NULL)
        return -1;
    for (k = 0; k < BIG_WRITE; k++)
        data[k] = (char)pattern(19, k);
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0 || pipe(c2p) != 0) {
        free(data);
        return -1;
    }
    kid = fork();
    if (kid < 0) {
        close(sp[0]); close(sp[1]);
        close(c2p[0]); close(c2p[1]);
        free(data);
        return -1;
    }
    if (kid == 0) {
        char drain[512];
        ssize_t n;
        while (total < BIG_WRITE) {
            n = read(sp[1], drain, sizeof(drain));
            if (n <= 0)
                _exit(71);
            total += (long)n;
        }
        ack = 1;
        if (write(c2p[1], &ack, 1) != 1)
            _exit(72);
        close(sp[0]); close(sp[1]);
        close(c2p[0]); close(c2p[1]);
        _exit(0);
    }
    close(sp[1]);
    close(c2p[1]);
    {
        connection peer = connection_borrow(sp[0]);
        int rc = peer.write(data, (size_t)BIG_WRITE, &werr);
        if (rc != STDLIB_OK || !werr.ok())
            ok = 0;
        mix((unsigned long long)rc);
        mix((unsigned long long)werr.category());
        peer.drop();
    }
    close(sp[0]);
    if (read(c2p[0], &ack, 1) != 1 || ack != 1)
        ok = 0;
    close(c2p[0]);
    waitpid(kid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        ok = 0;
    free(data);
    return ok;
}

int main(void) {
    char hex[32];
    char buf[16];
    error bad = error();
    int fd_before;
    int fd_after;
    int fd_supported;
    int i;
    int len;
    int cap;
    int chunk;
    int r;

    (void)!signal(SIGPIPE, on_ignore);
    (void)!signal(SIGALRM, on_alarm);
    (void)!alarm(25);

    fd_before = count_fds();
    fd_supported = fd_before >= 0;
    if (!fd_supported)
        print("SKIP_REASON", "f098_fd_count", "/dev/fd count unavailable");

    /* Documented recoverable argument failures; no fatal path is induced. */
    {
        connection invalid = connection_borrow(-1);
        connection zero;
        int ok = 1;
        ok = ok && invalid.read(buf, sizeof(buf), &bad) == STDLIB_FAIL &&
                   bad.category() == STDLIB_CATEGORY_INVALID;
        mix((unsigned long long)bad.category());
        ok = ok && invalid.write("x", 1, &bad) == STDLIB_FAIL &&
                   bad.category() == STDLIB_CATEGORY_INVALID;
        mix((unsigned long long)bad.category());
        ok = ok && invalid.read(buf, 0, &bad) == STDLIB_FAIL;
        ok = ok && invalid.read(NULL, sizeof(buf), &bad) == STDLIB_FAIL;
        ok = ok && invalid.write(NULL, 1, &bad) == STDLIB_FAIL;
        ok = ok && !zero.is_valid() && zero.descriptor() == 0;
        ok = ok && zero.read(buf, 0, &bad) == STDLIB_FAIL;
        ok = ok && zero.read(NULL, sizeof(buf), &bad) == STDLIB_FAIL;
        ok = ok && zero.write(NULL, 1, &bad) == STDLIB_FAIL;
        invalid.drop(); invalid.drop();
        zero.drop();
        mix((unsigned long long)ok);
        if (!ok)
            invariant("f098_invalid_args");
    }

    for (i = 0; i < ROUNDS; i++) {
        len = 1 + (int)(rnd() % 400u);
        cap = 1 + (int)(rnd() % 32u);
        chunk = 1 + (int)(rnd() % 24u);
        r = partial_round(i, len, cap, chunk);
        if (r < 0) {
            print("SKIP_REASON", "f098_partial",
                  "socketpair/fork unavailable on this host");
            mix(0ULL);
        } else {
            mix((unsigned long long)i);
            mix((unsigned long long)len);
            mix((unsigned long long)cap);
            mix((unsigned long long)chunk);
            mix((unsigned long long)r);
            if (!r)
                invariant("f098_partial_round");
        }
    }

    r = big_write_round();
    if (r < 0) {
        print("SKIP_REASON", "f098_big_write",
              "socketpair/fork/malloc unavailable on this host");
        mix(0ULL);
    } else {
        mix((unsigned long long)r);
        if (!r)
            invariant("f098_big_write");
    }

    fd_after = count_fds();
    if (fd_supported) {
        if (fd_after != fd_before)
            invariant("f098_fd_leak");
        mix((unsigned long long)(fd_after - fd_before));
    } else {
        mix((unsigned long long)-1);
    }

    (void)!alarm(0);
    mix((unsigned long long)failures);
    snprintf(hex, sizeof(hex), "%016llx", digest);
    print("f098_digest", hex);
    if (failures == 0)
        print("f098:", ROUNDS, "rounds ok");
    else
        print("f098: FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
