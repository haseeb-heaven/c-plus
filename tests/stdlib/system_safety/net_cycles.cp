/* Network safety: repeated connect/accept/exchange/disconnect cycles and fd
 * balance.  Each cycle is a helper call so C+ RAII owned handles keep unique
 * names per stack frame (reusing a name across loop iterations loses zero-init
 * and destructors).
 *
 * All waits are bounded: non-blocking connect + poll(POLLOUT, 2 s),
 * SO_RCVTIMEO (3 s) on accepted sockets, poll(POLLIN, 2 s) on the raw peer,
 * a 25 s parent SIGALRM watchdog and an external `timeout -k 5 30`.
 * No external servers; every peer is a local loopback socket.
 *
 * SELF-CHECKING: "<tag> <1|0>" per check, final "NET_SAFETY <checks> <failures>".
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/net.hp"

#define NSC_ROUNDS 20

static int nsc_checks = 0;
static int nsc_failures = 0;

static void nsc_check(const char *tag, int cond) {
    nsc_checks++;
    if (!cond)
        nsc_failures++;
    print(tag, cond ? 1 : 0);
    (void)!fflush(stdout);
}

static void nsc_skip(const char *tag, const char *reason) {
    nsc_checks++;
    print(tag, 1);
    print("SKIP_REASON", tag, reason);
    (void)!fflush(stdout);
}

static void nsc_watchdog(int sig) {
    static const char msg[] = "FATAL net_cycles watchdog timeout\n";
    (void)sig;
    (void)!write(2, msg, sizeof(msg) - 1);
    _exit(90);
}

static int nsc_fd_count(void) {
    DIR *d = opendir("/dev/fd");
    struct dirent *e;
    int n = 0;
    if (d == (DIR *)0)
        return -1;
    while ((e = readdir(d)) != (struct dirent *)0) {
        const char *s = e->d_name;
        if (s[0] >= '0' && s[0] <= '9')
            n++;
    }
    closedir(d);
    return n;
}

static int nsc_nonblock(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0)
        return 0;
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0 ? 1 : 0;
}

static int nsc_poll_out(int fd, int ms) {
    struct pollfd p;
    int rc;
    p.fd = fd;
    p.events = POLLOUT;
    p.revents = 0;
    do {
        rc = poll(&p, 1, ms);
    } while (rc < 0 && errno == EINTR);
    return rc;
}

static int nsc_poll_in(int fd, int ms) {
    struct pollfd p;
    int rc;
    p.fd = fd;
    p.events = POLLIN;
    p.revents = 0;
    do {
        rc = poll(&p, 1, ms);
    } while (rc < 0 && errno == EINTR);
    return rc;
}

/* One full local TCP exchange.  Returns 1 on success, 0 on any failure. */
static int nsc_one_cycle(void) {
    error nsc_cycle_err = error();
    listener nsc_cycle_srv = listener(0);
    int nsc_cycle_port = nsc_cycle_srv.bound_port();
    int nsc_cycle_peer = -1;
    int ok = nsc_cycle_srv.is_valid() && nsc_cycle_port > 0;
    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    (void)!setsockopt(nsc_cycle_srv.descriptor(), SOL_SOCKET, SO_RCVTIMEO,
                      &tv, sizeof(tv));

    if (ok) {
        nsc_cycle_peer = socket(AF_INET, SOCK_STREAM, 0);
        if (nsc_cycle_peer >= 0 && nsc_nonblock(nsc_cycle_peer)) {
            struct sockaddr_in a;
            memset(&a, 0, sizeof(a));
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            a.sin_port = htons((unsigned short)nsc_cycle_port);
            (void)!connect(nsc_cycle_peer, (struct sockaddr *)&a, sizeof(a));
            ok = ok && nsc_poll_out(nsc_cycle_peer, 2000) > 0;
        } else {
            ok = 0;
        }
    }

    if (ok) {
        connection nsc_cycle_conn = nsc_cycle_srv.accept();
        ok = ok && nsc_cycle_conn.is_valid();
        if (ok) {
            char nsc_cycle_buf[8];
            memset(nsc_cycle_buf, 0, sizeof(nsc_cycle_buf));
            (void)!setsockopt(nsc_cycle_conn.descriptor(), SOL_SOCKET,
                              SO_RCVTIMEO, &tv, sizeof(tv));
            (void)!write(nsc_cycle_peer, "HI", 2);
            nsc_cycle_err.reset();
            ok = ok && nsc_cycle_conn.read(nsc_cycle_buf,
                                           (size_t)sizeof(nsc_cycle_buf),
                                           &nsc_cycle_err) == 2;
            ok = ok && nsc_cycle_err.ok() == 1 &&
                 __builtin_memcmp(nsc_cycle_buf, "HI", 2) == 0;
            nsc_cycle_err.reset();
            ok = ok && nsc_cycle_conn.write("Q", 1, &nsc_cycle_err) ==
                           STDLIB_OK &&
                 nsc_cycle_err.ok() == 1;
            {
                char nsc_cycle_reply = 0;
                if (nsc_poll_in(nsc_cycle_peer, 2000) > 0)
                    ok = ok && read(nsc_cycle_peer, &nsc_cycle_reply, 1) == 1 &&
                         nsc_cycle_reply == 'Q';
                else
                    ok = 0;
            }
        }
        nsc_cycle_conn.drop();
    }

    if (nsc_cycle_peer >= 0)
        close(nsc_cycle_peer);
    nsc_cycle_srv.drop();
    return ok ? 1 : 0;
}

int main(void) {
    int nsc_baseline = nsc_fd_count();
    int nsc_ok = 0;
    int nsc_i;

    (void)!signal(SIGALRM, nsc_watchdog);
    (void)!alarm(25);

    for (nsc_i = 0; nsc_i < NSC_ROUNDS; nsc_i++)
        if (nsc_one_cycle() == 1)
            nsc_ok++;

    nsc_check("net_repeated_cycles", nsc_ok == NSC_ROUNDS);
    if (nsc_baseline < 0 || nsc_fd_count() < 0)
        nsc_skip("net_cycles_no_fd_leak",
                 "opendir(/dev/fd) unavailable; descriptor balance not checked");
    else
        nsc_check("net_cycles_no_fd_leak", nsc_fd_count() == nsc_baseline);

    (void)!alarm(0);
    print("NET_SAFETY", nsc_checks, nsc_failures);
    return nsc_failures == 0 ? 0 : 1;
}
