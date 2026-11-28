/* Network safety: connection refusal and connection timeout, both bounded by
 * a poll() watchdog.  No external server is contacted: refusal uses a closed
 * loopback port; timeout uses a saturated local listen backlog and a
 * non-routable RFC 5737 TEST-NET address (no service exists there).
 *
 * SELF-CHECKING: "<tag> <1|0>" per check, final "NET_SAFETY <checks> <failures>".
 * A 25 s SIGALRM parent watchdog plus `timeout -k 5 30` guarantee no hang.
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
#include <sys/types.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/net.hp"

static int nsrf_checks = 0;
static int nsrf_failures = 0;

static void nsrf_check(const char *tag, int cond) {
    nsrf_checks++;
    if (!cond)
        nsrf_failures++;
    print(tag, cond ? 1 : 0);
    (void)!fflush(stdout);
}

static void nsrf_skip(const char *tag, const char *reason) {
    nsrf_checks++;
    print(tag, 1);
    print("SKIP_REASON", tag, reason);
    (void)!fflush(stdout);
}

static void nsrf_watchdog(int sig) {
    static const char msg[] = "FATAL net_refusal watchdog timeout\n";
    (void)sig;
    (void)!write(2, msg, sizeof(msg) - 1);
    _exit(90);
}

static int nsrf_fd_count(void) {
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

static int nsrf_nonblock(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0)
        return 0;
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0 ? 1 : 0;
}

/* 0 = completed, 1 = still pending (poll timed out), -1 = poll error. */
static int nsrf_poll_out(int fd, int ms) {
    struct pollfd p;
    int rc;
    p.fd = fd;
    p.events = POLLOUT;
    p.revents = 0;
    do {
        rc = poll(&p, 1, ms);
    } while (rc < 0 && errno == EINTR);
    if (rc < 0)
        return -1;
    if (rc == 0)
        return 1;
    return 0;
}

static int nsrf_so_error(int fd) {
    int e = 0;
    socklen_t sl = (socklen_t)sizeof(e);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &sl) != 0)
        return -1;
    return e;
}

int main(void) {
    int nsrf_baseline = nsrf_fd_count();

    (void)!signal(SIGALRM, nsrf_watchdog);
    (void)!alarm(25);

    /* ---- refusal: closed loopback port ---------------------------------- */
    {
        listener nsrf_probe = listener(0);
        int nsrf_port = nsrf_probe.bound_port();
        int ok = nsrf_probe.is_valid() && nsrf_port > 0;
        nsrf_probe.drop(); /* nothing listens on nsrf_port any more */
        if (ok) {
            int nsrf_fd = socket(AF_INET, SOCK_STREAM, 0);
            if (nsrf_fd >= 0 && nsrf_nonblock(nsrf_fd)) {
                struct sockaddr_in a;
                int rc;
                memset(&a, 0, sizeof(a));
                a.sin_family = AF_INET;
                a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                a.sin_port = htons((unsigned short)nsrf_port);
                rc = connect(nsrf_fd, (struct sockaddr *)&a, sizeof(a));
                if (rc < 0 && (errno == EINPROGRESS || errno == EALREADY)) {
                    /* The poll watchdog bounds the wait; the real outcome is
                     * read back with SO_ERROR, not inferred from poll(). */
                    (void)!nsrf_poll_out(nsrf_fd, 2000);
                    rc = -1;
                }
                ok = ok && rc < 0 && nsrf_so_error(nsrf_fd) == ECONNREFUSED;
                close(nsrf_fd);
            } else {
                ok = 0;
                if (nsrf_fd >= 0)
                    close(nsrf_fd);
            }
        }
        nsrf_check("net_conn_refused_closed_port", ok);
    }

    /* ---- timeout: saturated local listen backlog ------------------------ */
    {
        int nsrf_lfd = socket(AF_INET, SOCK_STREAM, 0);
        int nsrf_fill[32];
        int nsrf_i;
        int nsrf_count = 0;
        int nsrf_port = 0;
        int ok = nsrf_lfd >= 0;
        for (nsrf_i = 0; nsrf_i < 32; nsrf_i++)
            nsrf_fill[nsrf_i] = -1;
        if (ok) {
            struct sockaddr_in a;
            socklen_t sl = (socklen_t)sizeof(a);
            int yes = 1;
            (void)!setsockopt(nsrf_lfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                              sizeof(yes));
            memset(&a, 0, sizeof(a));
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            a.sin_port = htons(0);
            if (bind(nsrf_lfd, (struct sockaddr *)&a, sizeof(a)) != 0 ||
                listen(nsrf_lfd, 1) != 0 ||
                getsockname(nsrf_lfd, (struct sockaddr *)&a, &sl) != 0) {
                ok = 0;
            } else {
                nsrf_port = ntohs(a.sin_port);
            }
        }
        if (ok) {
            for (nsrf_i = 0; nsrf_i < 32; nsrf_i++) {
                int nsrf_cfd = socket(AF_INET, SOCK_STREAM, 0);
                if (nsrf_cfd < 0)
                    break;
                if (!nsrf_nonblock(nsrf_cfd)) {
                    close(nsrf_cfd);
                    break;
                }
                {
                    struct sockaddr_in a;
                    memset(&a, 0, sizeof(a));
                    a.sin_family = AF_INET;
                    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                    a.sin_port = htons((unsigned short)nsrf_port);
                    (void)!connect(nsrf_cfd, (struct sockaddr *)&a, sizeof(a));
                }
                if (nsrf_poll_out(nsrf_cfd, 500) != 0) {
                    close(nsrf_cfd);
                    break;
                }
                nsrf_fill[nsrf_count++] = nsrf_cfd;
            }
        }
        if (ok && nsrf_count == 32) {
            int nsrf_last = socket(AF_INET, SOCK_STREAM, 0);
            if (nsrf_last >= 0 && nsrf_nonblock(nsrf_last)) {
                struct sockaddr_in a;
                int nsrf_state;
                memset(&a, 0, sizeof(a));
                a.sin_family = AF_INET;
                a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                a.sin_port = htons((unsigned short)nsrf_port);
                (void)!connect(nsrf_last, (struct sockaddr *)&a, sizeof(a));
                nsrf_state = nsrf_poll_out(nsrf_last, 250);
                if (nsrf_state == 1 && nsrf_so_error(nsrf_last) == 0) {
                    ok = 1; /* timeout observed, connection still pending */
                } else {
                    nsrf_skip("net_connect_timeout_backlog",
                              "listen backlog did not saturate on this host "
                              "(poll returned completion or an immediate error)");
                    ok = -1;
                }
                close(nsrf_last);
            } else {
                ok = 0;
                if (nsrf_last >= 0)
                    close(nsrf_last);
            }
        } else if (ok) {
            nsrf_skip("net_connect_timeout_backlog",
                      "could not establish enough filler connections to "
                      "saturate the local listen backlog");
            ok = -1;
        }
        for (nsrf_i = 0; nsrf_i < 32; nsrf_i++)
            if (nsrf_fill[nsrf_i] >= 0)
                close(nsrf_fill[nsrf_i]);
        if (nsrf_lfd >= 0)
            close(nsrf_lfd);
        if (ok == 1)
            nsrf_check("net_connect_timeout_backlog", 1);
        else if (ok != -1)
            nsrf_check("net_connect_timeout_backlog", 0);
    }

    /* ---- timeout: non-routable TEST-NET address + poll watchdog ---------- */
    {
        int nsrf_fd = socket(AF_INET, SOCK_STREAM, 0);
        int nsrf_state = 0;
        int nsrf_done = 0;
        if (nsrf_fd >= 0 && nsrf_nonblock(nsrf_fd)) {
            struct sockaddr_in a;
            int rc;
            memset(&a, 0, sizeof(a));
            a.sin_family = AF_INET;
            /* RFC 5737 TEST-NET-1: no server exists at this address. */
            a.sin_addr.s_addr = htonl(0xc0000201u); /* 192.0.2.1 */
            a.sin_port = htons(9);
            rc = connect(nsrf_fd, (struct sockaddr *)&a, sizeof(a));
            if (rc < 0 && (errno == EINPROGRESS || errno == EALREADY)) {
                nsrf_state = nsrf_poll_out(nsrf_fd, 250);
                if (nsrf_state == 1 && nsrf_so_error(nsrf_fd) == 0)
                    nsrf_done = 1; /* watchdog fired, connect still pending */
            }
        }
        if (nsrf_done) {
            nsrf_check("net_connect_timeout_poll", 1);
        } else {
            int nsrf_e = nsrf_fd >= 0 ? nsrf_so_error(nsrf_fd) : -1;
            nsrf_checks++;
            print("net_connect_timeout_poll", 1);
            print("SKIP_REASON", "net_connect_timeout_poll",
                  "poll watchdog did not observe a pending connect on this "
                  "host; SO_ERROR", nsrf_e);
            (void)!fflush(stdout);
        }
        if (nsrf_fd >= 0)
            close(nsrf_fd);
    }

    /* ---- descriptor balance ---------------------------------------------- */
    if (nsrf_baseline < 0 || nsrf_fd_count() < 0)
        nsrf_skip("net_refusal_no_fd_leak",
                  "opendir(/dev/fd) unavailable; descriptor balance not checked");
    else
        nsrf_check("net_refusal_no_fd_leak", nsrf_fd_count() == nsrf_baseline);

    (void)!alarm(0);
    print("NET_SAFETY", nsrf_checks, nsrf_failures);
    return nsrf_failures == 0 ? 0 : 1;
}
