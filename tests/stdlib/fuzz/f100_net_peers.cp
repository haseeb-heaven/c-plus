/* F-097 -- deterministic fuzz configuration: net forked peers and ports.
 *
 * All peers are created locally: an AF_INET/INADDR_LOOPBACK listener on an
 * ephemeral port connects to a forked child, and an AF_UNIX socketpair is
 * written by a forked child and read through connection_borrow().  No external
 * service, no DNS, no fixed port allocation.  Invalid ports (-1, INT_MIN,
 * 65536, 70000) must produce inert handles.  One connection per helper call
 * keeps C+ owned-local names unique and bounds the work; the parent uses poll()
 * with a 2-second cap before every blocking accept/read, plus a SIGALRM
 * watchdog and an external `timeout 30s`.
 *
 * The ephemeral port number is host-assigned and is never folded into the
 * digest -- only the boolean "in 1..65535" is.
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <netinet/in.h>
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

#define ROUNDS 8
#define FNV_PRIME 1099511628211ULL

static unsigned long long rng_state = 0xF0971234ULL;
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
    static const char msg[] = "F097 watchdog timeout\n";
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

static unsigned char payload(int index, int len) {
    return (unsigned char)(index * 37 + len * 11 + 7);
}

/* One loopback exchange with a forked child. Returns 1 on success. */
static int loopback_round(int round, int len) {
    listener srv = listener(0);
    int c2p[2];
    int p2c[2];
    pid_t kid;
    int port;
    int ok = 1;
    int status = 0;
    char ack = 0;
    struct pollfd pfd;
    char buf[256];

    if (!srv.is_valid() || srv.bound_port() <= 0)
        return 0;
    port = srv.bound_port();
    if (pipe(c2p) != 0 || pipe(p2c) != 0) {
        srv.drop();
        return 0;
    }
    kid = fork();
    if (kid < 0) {
        close(c2p[0]); close(c2p[1]); close(p2c[0]); close(p2c[1]);
        srv.drop();
        return 0;
    }
    if (kid == 0) {
        int child_port = 0;
        int fd;
        struct sockaddr_in addr;
        unsigned char want;
        int k;
        close(c2p[1]);
        close(p2c[0]);
        if (read(c2p[0], &child_port, sizeof(child_port)) !=
            (ssize_t)sizeof(child_port))
            _exit(41);
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
            _exit(42);
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons((unsigned short)child_port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
            _exit(43);
        want = payload(round, len);
        for (k = 0; k < len; k++)
            buf[k] = (char)want;
        if (write(fd, buf, (size_t)len) != (ssize_t)len)
            _exit(44);
        close(fd);
        ack = 1;
        if (write(p2c[1], &ack, 1) != 1)
            _exit(45);
        close(c2p[0]);
        close(p2c[1]);
        _exit(0);
    }

    /* Parent: announce the port, accept with a bounded poll, read exactly. */
    close(c2p[0]);
    close(p2c[1]);
    if (write(c2p[1], &port, sizeof(port)) != (ssize_t)sizeof(port))
        ok = 0;
    pfd.fd = srv.descriptor();
    pfd.events = POLLIN;
    pfd.revents = 0;
    if (poll(&pfd, 1, 2000) != 1 || !(pfd.revents & POLLIN)) {
        ok = 0;
    } else {
        connection peer = srv.accept();
        if (!peer.is_valid()) {
            ok = 0;
        } else {
            int got = 0;
            int n;
            unsigned char want = payload(round, len);
            while (got < len) {
                pfd.fd = peer.descriptor();
                pfd.events = POLLIN;
                pfd.revents = 0;
                if (poll(&pfd, 1, 2000) != 1 || !(pfd.revents & POLLIN)) {
                    ok = 0;
                    break;
                }
                n = peer.read(buf + got, (size_t)(len - got), NULL);
                if (n <= 0) {
                    ok = 0;
                    break;
                }
                got += n;
            }
            if (got != len)
                ok = 0;
            for (n = 0; n < len; n++) {
                if ((unsigned char)buf[n] != want)
                    ok = 0;
            }
            peer.drop();
        }
    }
    if (read(p2c[0], &ack, 1) != 1 || ack != 1)
        ok = 0;
    waitpid(kid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        ok = 0;
    close(c2p[1]);
    close(p2c[0]);
    srv.drop();

    return ok;
}

/* One AF_UNIX exchange with a forked child, through connection_borrow(). */
static int unix_round(int round, int len) {
    int sp[2];
    pid_t kid;
    int status = 0;
    int ok = 1;
    int k;
    char buf[256];
    unsigned char want = payload(round + 3, len);
    struct pollfd pfd;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0)
        return -1; /* capability absent -> caller records SKIP */
    kid = fork();
    if (kid < 0) {
        close(sp[0]);
        close(sp[1]);
        return -1;
    }
    if (kid == 0) {
        unsigned char fill = payload(round + 3, len);
        for (k = 0; k < len; k++)
            buf[k] = (char)fill;
        if (write(sp[1], buf, (size_t)len) != (ssize_t)len)
            _exit(51);
        close(sp[0]);
        close(sp[1]);
        _exit(0);
    }
    close(sp[1]);
    {
        connection borrowed = connection_borrow(sp[0]);
        int got = 0;
        while (got < len) {
            pfd.fd = borrowed.descriptor();
            pfd.events = POLLIN;
            pfd.revents = 0;
            if (poll(&pfd, 1, 2000) != 1 || !(pfd.revents & POLLIN)) {
                ok = 0;
                break;
            }
            {
                int n = borrowed.read(buf + got, (size_t)(len - got), NULL);
                if (n <= 0) {
                    ok = 0;
                    break;
                }
                got += n;
            }
        }
        if (got != len)
            ok = 0;
        for (k = 0; k < len; k++) {
            if ((unsigned char)buf[k] != want)
                ok = 0;
        }
        borrowed.drop(); /* borrowed: must not close sp[0] here */
    }
    close(sp[0]);
    waitpid(kid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        ok = 0;
    return ok;
}

int main(void) {
    char hex[32];
    int fd_before;
    int fd_after;
    int fd_supported;
    int i;
    int len;
    int r;

    (void)!signal(SIGPIPE, on_ignore);
    (void)!signal(SIGALRM, on_alarm);
    (void)!alarm(25);

    fd_before = count_fds();
    fd_supported = fd_before >= 0;
    if (!fd_supported)
        print("SKIP_REASON", "f097_fd_count", "/dev/fd count unavailable");

    /* Invalid ports must leave completely inert listeners. */
    {
        listener neg = listener(-1);
        listener low = listener(INT_MIN);
        listener big = listener(65536);
        listener huge = listener(70000);
        int ok = !neg.is_valid() && !low.is_valid() &&
                 !big.is_valid() && !huge.is_valid();
        ok = ok && neg.descriptor() == -1 && low.descriptor() == -1 &&
                   big.descriptor() == -1 && huge.descriptor() == -1;
        ok = ok && neg.bound_port() == 0 && low.bound_port() == 0 &&
                   big.bound_port() == 0 && huge.bound_port() == 0;
        neg.drop(); neg.drop();
        low.drop(); big.drop(); huge.drop();
        mix((unsigned long long)ok);
        if (!ok)
            invariant("f097_invalid_ports");
    }

    /* Zero-initialized handles and borrowed negative descriptors. */
    {
        listener zl;
        connection zc;
        listener bl = listener_borrow(-1);
        connection bc = connection_borrow(-1);
        int ok = !zl.is_valid() && !zc.is_valid() &&
                 !bl.is_valid() && !bc.is_valid();
        ok = ok && zl.bound_port() == 0 && bl.bound_port() == 0;
        zl.drop(); zc.drop(); bl.drop(); bc.drop();
        mix((unsigned long long)ok);
        if (!ok)
            invariant("f097_inert_handles");
    }

    for (i = 0; i < ROUNDS; i++) {
        len = 1 + (int)(rnd() % 200u);
        r = loopback_round(i, len);
        mix((unsigned long long)i);
        mix((unsigned long long)len);
        mix((unsigned long long)r);
        if (!r)
            invariant("f097_loopback_round");

        r = unix_round(i, len);
        if (r < 0) {
            print("SKIP_REASON", "f097_af_unix",
                  "socketpair/fork unavailable on this host");
            mix(0ULL);
        } else {
            mix((unsigned long long)r);
            if (!r)
                invariant("f097_unix_round");
        }
    }

    fd_after = count_fds();
    if (fd_supported) {
        if (fd_after != fd_before)
            invariant("f097_fd_leak");
        mix((unsigned long long)(fd_after - fd_before));
    } else {
        mix((unsigned long long)-1);
    }

    (void)!alarm(0);
    mix((unsigned long long)failures);
    snprintf(hex, sizeof(hex), "%016llx", digest);
    print("f097_digest", hex);
    if (failures == 0)
        print("f097:", ROUNDS, "rounds ok");
    else
        print("f097: FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
