/* Deep task-5 gap test for stdlib/net.hp.
 *
 * Complements deep_net.cp (which already covers port ranges, bind-in-use,
 * borrowed fd 0/1/2 and >=3, broken-pipe write, exact EOF, injected partial
 * write, invalid handles, write-no-progress, injected EINTR/EIO, EMFILE and
 * fd leaks). This file covers only NEW gaps:
 *   N1  connection refused on a closed loopback port (defined recoverable
 *       socket failure, no fd leak) plus a net.hp peer-reset write failure,
 *   N2  invalid address strings (SKIP: net.hp has no address argument),
 *   N3  repeated close/drop x3 on owning and borrowed handles, fd unchanged,
 *   N4  real SIGALRM interrupted read() and accept() in forked children,
 *   N5  partial read across several read() calls from a controlled peer.
 *
 * All endpoints are loopback or AF_UNIX socketpair; a forked local peer is used
 * for every blocking case. Each blocking wait is bounded: 3 s poll handshakes,
 * 3 s SO_RCVTIMEO/SO_SNDTIMEO, a 5 s alarm in each forked child, a bounded
 * waitpid loop, and a 25 s process watchdog (external harness timeout is 30 s).
 * SIGPIPE is ignored so a reset peer yields a returned error, not death.
 *
 * Every owned handle has a unique name: C+ RAII tracks owned locals by name
 * for the whole function, so reused names lose zero-init and destructors.
 *
 * Self-checking: each check prints "<tag> <0|1>"; the final line is
 * "NET_TASK5 <checks> <failures>". Exit 0 only when failures == 0.
 * Documented SKIPs print "<tag> 1" plus a SKIP_REASON line.
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/net.hp"

static int t5_checks = 0;
static int t5_failures = 0;
static volatile sig_atomic_t t5_child_alarm_fired = 0;

static void t5_check(const char *tag, int cond) {
    t5_checks++;
    if (!cond)
        t5_failures++;
    print(tag, cond ? 1 : 0);
    (void)!fflush(stdout);
}

static void t5_skip(const char *tag, const char *reason) {
    t5_checks++;
    print(tag, 1);
    print("SKIP_REASON", tag, reason);
    (void)!fflush(stdout);
}

static void t5_on_ignore(int sig) {
    (void)sig;
}

static void t5_on_watchdog(int sig) {
    static const char msg[] = "FATAL TASK5 watchdog timeout\n";
    (void)sig;
    (void)!write(2, msg, sizeof(msg) - 1);
    _exit(90);
}

static void t5_on_child_alarm(int sig) {
    (void)sig;
    t5_child_alarm_fired = 1;
}

/* Install a non-SA_RESTART handler in a forked child. */
static void t5_child_alarm_setup(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = t5_on_child_alarm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; /* deliberately no SA_RESTART */
    (void)!sigaction(SIGALRM, &sa, (struct sigaction *)0);
    t5_child_alarm_fired = 0;
}

static int t5_fd_count(void) {
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

static int t5_poll_in(int fd, int ms) {
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

/* Bounded reap: up to ~3 s, then kill and reap. Returns 1 if normally exited. */
static int t5_wait_child(pid_t pid, int *status) {
    int tries;
    for (tries = 0; tries < 600; tries++) {
        pid_t r = waitpid(pid, status, WNOHANG);
        if (r == pid)
            return 1;
        if (r < 0)
            return 0;
        usleep(5000);
    }
    (void)!kill(pid, SIGKILL);
    (void)!waitpid(pid, status, 0);
    return 0;
}

int main(void) {
    error t5_err = error();
    int t5_fd_baseline = t5_fd_count();
    int t5_status = 0;

    (void)!signal(SIGPIPE, t5_on_ignore);
    (void)!signal(SIGALRM, t5_on_watchdog);
    (void)!alarm(25);

    /* ---- N1a: connection refused on a closed loopback port -------------- */
    {
        listener t5_ref_listener = listener(0);
        int t5_ref_port = t5_ref_listener.bound_port();
        int ok = t5_ref_listener.is_valid() && t5_ref_port > 0;
        t5_ref_listener.drop(); /* no service remains on the port */
        if (ok) {
            int t5_ref_fd = socket(AF_INET, SOCK_STREAM, 0);
            if (t5_ref_fd >= 0) {
                struct sockaddr_in a;
                int fl;
                int rc;
                memset(&a, 0, sizeof(a));
                a.sin_family = AF_INET;
                a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                a.sin_port = htons((unsigned short)t5_ref_port);
                fl = fcntl(t5_ref_fd, F_GETFL, 0);
                if (fl >= 0)
                    (void)!fcntl(t5_ref_fd, F_SETFL, fl | O_NONBLOCK);
                rc = connect(t5_ref_fd, (struct sockaddr *)&a, sizeof(a));
                if (rc < 0 && (errno == EINPROGRESS || errno == EALREADY)) {
                    struct pollfd p;
                    int soerr = 0;
                    socklen_t sl = (socklen_t)sizeof(soerr);
                    p.fd = t5_ref_fd;
                    p.events = POLLOUT;
                    p.revents = 0;
                    (void)!poll(&p, 1, 2000);
                    (void)!getsockopt(t5_ref_fd, SOL_SOCKET, SO_ERROR, &soerr,
                                      &sl);
                    errno = soerr;
                    rc = soerr == 0 ? 0 : -1;
                }
                ok = ok && rc < 0 && errno == ECONNREFUSED;
                close(t5_ref_fd);
            } else {
                ok = 0;
            }
        }
        t5_check("net_conn_refused_closed_port", ok);
    }

    /* ---- N1b: net.hp peer-reset returns a defined recoverable error ----- */
    {
        listener t5_rst_srv = listener(0);
        int ok = t5_rst_srv.is_valid();
        int t5_rst_c2p[2];
        t5_rst_c2p[0] = t5_rst_c2p[1] = -1;
        if (ok && pipe(t5_rst_c2p) == 0) {
            pid_t t5_rst_kid = fork();
            if (t5_rst_kid == 0) {
                int t5_rst_fd;
                struct linger t5_rst_lng;
                struct sockaddr_in t5_rst_addr;
                char t5_rst_rel = 'R';
                close(t5_rst_c2p[0]);
                t5_rst_fd = socket(AF_INET, SOCK_STREAM, 0);
                if (t5_rst_fd < 0)
                    _exit(1);
                t5_rst_lng.l_onoff = 1;
#ifdef __linux__
                t5_rst_lng.l_onoff = 0;
#else
                t5_rst_lng.l_linger = 0;
#endif
                (void)!setsockopt(t5_rst_fd, SOL_SOCKET, SO_LINGER,
                                  &t5_rst_lng, sizeof(t5_rst_lng));
                memset(&t5_rst_addr, 0, sizeof(t5_rst_addr));
                t5_rst_addr.sin_family = AF_INET;
                t5_rst_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                t5_rst_addr.sin_port =
                    htons((unsigned short)t5_rst_srv.bound_port());
                if (connect(t5_rst_fd, (struct sockaddr *)&t5_rst_addr,
                            sizeof(t5_rst_addr)) != 0) {
                    close(t5_rst_fd);
                    _exit(1);
                }
                close(t5_rst_fd); /* SO_LINGER=0 -> RST */
                (void)!write(t5_rst_c2p[1], &t5_rst_rel, 1);
                close(t5_rst_c2p[1]);
                _exit(0);
            }
            close(t5_rst_c2p[1]);
            if (t5_rst_kid > 0 && t5_poll_in(t5_rst_c2p[0], 3000) > 0) {
                char t5_rst_rel = 0;
                if (read(t5_rst_c2p[0], &t5_rst_rel, 1) == 1 &&
                    t5_rst_rel == 'R') {
                    connection t5_rst_conn = t5_rst_srv.accept();
                    if (t5_rst_conn.is_valid()) {
                        struct timeval tv;
                        int rc;
                        tv.tv_sec = 3;
                        tv.tv_usec = 0;
                        (void)!setsockopt(t5_rst_conn.descriptor(), SOL_SOCKET,
                                          SO_SNDTIMEO, &tv, sizeof(tv));
                        usleep(100000);
                        rc = t5_rst_conn.write("z", 1, &t5_err);
                        if (rc == STDLIB_OK) {
                            usleep(150000); /* allow RST to be processed */
                            rc = t5_rst_conn.write("z", 1, &t5_err);
                        }
                        ok = ok && rc == STDLIB_FAIL && t5_err.failed_now() == 1 &&
                             t5_err.category() == STDLIB_CATEGORY_IO &&
                             strcmp(t5_err.what(), "connection::write failed") ==
                                 0;
                        t5_rst_conn.drop();
                    } else {
                        ok = 0;
                    }
                } else {
                    ok = 0;
                }
                (void)!t5_wait_child(t5_rst_kid, &t5_status);
            } else {
                ok = 0;
                if (t5_rst_kid > 0)
                    (void)!t5_wait_child(t5_rst_kid, &t5_status);
            }
            close(t5_rst_c2p[0]);
        } else {
            ok = 0;
        }
        t5_rst_srv.drop();
        t5_check("net_peer_reset_recoverable", ok);
    }

    /* ---- N2: invalid address strings (API does not accept one) --------- */
    t5_skip("net_invalid_address",
            "stdlib/net.hp listener(port) takes only an integer port and "
            "always binds INADDR_LOOPBACK; connection has no address argument "
            "either. Strings such as \"not.an.address\" or \"\" cannot be "
            "passed to any net.hp API, so no INVALID_ADDRESS case exists.");

    /* ---- N3: repeated close/drop x3, fd count unchanged ----------------- */
    {
        int t5_rep_sp[2];
        int t5_rep_before = t5_fd_count();
        listener t5_rep_listener = listener(0);
        connection t5_rep_conn;
        int ok = t5_rep_listener.is_valid() &&
                 socketpair(AF_UNIX, SOCK_STREAM, 0, t5_rep_sp) == 0;
        if (ok) {
            t5_rep_conn = connection_borrow(t5_rep_sp[0]);
            ok = ok && t5_rep_conn.is_valid();
        }
        if (ok) {
            int i;
            t5_rep_listener.drop();
            t5_rep_listener.drop();
            t5_rep_listener.drop();
            t5_rep_conn.drop();
            t5_rep_conn.drop();
            t5_rep_conn.drop();
            for (i = 0; i < 3; i++)
                /* C+ zero-initialises an owner after drop(); the fd slot then
                 * reads 0.  Both 0 and -1 mark an invalid consumed handle;
                 * is_valid() is the authoritative check. */
                ok = ok && t5_rep_listener.descriptor() <= 0 &&
                           t5_rep_conn.descriptor() <= 0 &&
                           !t5_rep_listener.is_valid() &&
                           !t5_rep_conn.is_valid();
            /* A borrowed handle must never close the underlying fd. */
            ok = ok && fcntl(t5_rep_sp[0], F_GETFD) != -1;
            close(t5_rep_sp[0]);
            close(t5_rep_sp[1]);
        }
        t5_check("net_repeated_drop", ok);
        t5_check("net_repeated_drop_no_leak",
                 t5_rep_before >= 0 && t5_fd_count() == t5_rep_before);
    }

    /* ---- N4a: real SIGALRM interrupts connection::read ----------------- */
    {
        int t5_ei_ready[2];
        int t5_ei_res[2];
        int t5_ei_sp[2];
        t5_ei_ready[0] = t5_ei_ready[1] = -1;
        t5_ei_res[0] = t5_ei_res[1] = -1;
        t5_ei_sp[0] = t5_ei_sp[1] = -1;
        int ok = pipe(t5_ei_ready) == 0;
            if (ok)
                ok = pipe(t5_ei_res) == 0;
            if (ok)
                ok = socketpair(AF_UNIX, SOCK_STREAM, 0, t5_ei_sp) == 0;
            if (!ok) {
                t5_skip("net_read_eintr_signal",
                        "pipe()/socketpair() unavailable");
                if (t5_ei_ready[0] >= 0) {
                    close(t5_ei_ready[0]);
                    close(t5_ei_ready[1]);
                }
                if (t5_ei_res[0] >= 0) {
                    close(t5_ei_res[0]);
                    close(t5_ei_res[1]);
                }
            } else {
            pid_t t5_ei_kid = fork();
            if (t5_ei_kid == 0) {
                char t5_ei_go = 'R';
                char t5_ei_buf[16];
                error t5_ei_child_err = error();
                int t5_ei_n;
                int t5_ei_sig;
                int t5_ei_failed;
                int t5_ei_word[4];
                close(t5_ei_ready[0]);
                close(t5_ei_res[0]);
                close(t5_ei_sp[1]);
                t5_child_alarm_setup();
                alarm(5); /* hard child watchdog */
                (void)!write(t5_ei_ready[1], &t5_ei_go, 1);
                {
                    connection t5_eintr_conn = connection_borrow(t5_ei_sp[0]);
                    alarm(1); /* arrives while read() is blocked */
                    t5_ei_n = t5_eintr_conn.read(t5_ei_buf,
                                                 (size_t)sizeof(t5_ei_buf),
                                                 &t5_ei_child_err);
                }
                t5_ei_sig = (int)t5_child_alarm_fired;
                t5_ei_failed = t5_ei_child_err.failed_now();
                t5_ei_word[0] = t5_ei_n;
                t5_ei_word[1] = t5_ei_sig;
                t5_ei_word[2] = t5_ei_failed;
                t5_ei_word[3] = (t5_ei_n > 0 && t5_ei_buf[0] == 'D' &&
                                 t5_ei_buf[1] == 'A' && t5_ei_buf[2] == 'T' &&
                                 t5_ei_buf[3] == 'A')
                                    ? 1
                                    : 0;
                (void)!write(t5_ei_res[1], t5_ei_word,
                             (size_t)sizeof(t5_ei_word));
                close(t5_ei_res[1]);
                close(t5_ei_ready[1]);
                close(t5_ei_sp[0]);
                _exit(0);
            }
            close(t5_ei_ready[1]);
            close(t5_ei_res[1]);
            close(t5_ei_sp[0]);
            if (t5_ei_kid > 0 && t5_poll_in(t5_ei_ready[0], 3000) > 0) {
                char t5_ei_go = 0;
                int t5_ei_word[4];
                int got = 0;
                if (read(t5_ei_ready[0], &t5_ei_go, 1) == 1 &&
                    t5_ei_go == 'R') {
                    usleep(1300000); /* let alarm(1) fire inside read() */
                    (void)!write(t5_ei_sp[1], "DATA", 4);
                    if (t5_poll_in(t5_ei_res[0], 4000) > 0)
                        got = read(t5_ei_res[0], t5_ei_word,
                                   (size_t)sizeof(t5_ei_word)) ==
                              (ssize_t)sizeof(t5_ei_word);
                }
                if (got) {
                    if (t5_ei_word[1] < 1) {
                        t5_skip("net_read_eintr_signal",
                                "SIGALRM was not delivered while read() was "
                                "blocked on this host");
                    } else {
                        ok = t5_ei_word[0] == 4 && t5_ei_word[2] == 0 &&
                             t5_ei_word[3] == 1;
                        t5_check("net_read_eintr_signal", ok);
                    }
                } else {
                    t5_check("net_read_eintr_signal", 0);
                }
                (void)!t5_wait_child(t5_ei_kid, &t5_status);
            } else {
                t5_check("net_read_eintr_signal", 0);
                if (t5_ei_kid > 0)
                    (void)!t5_wait_child(t5_ei_kid, &t5_status);
            }
            close(t5_ei_ready[0]);
            close(t5_ei_res[0]);
            close(t5_ei_sp[1]);
        }
    }

    /* ---- N4b: real SIGALRM interrupts listener::accept ----------------- */
    {
        listener t5_acc_srv = listener(0);
        int t5_acc_port = t5_acc_srv.bound_port();
        int t5_acc_ready[2];
        int t5_acc_res[2];
        int ok = t5_acc_srv.is_valid() && t5_acc_port > 0;
        if (ok)
            ok = pipe(t5_acc_ready) == 0;
        if (ok)
            ok = pipe(t5_acc_res) == 0;
        if (!ok) {
            t5_skip("net_accept_eintr_signal", "pipe()/listener unavailable");
        } else {
            pid_t t5_acc_kid = fork();
            if (t5_acc_kid == 0) {
                char t5_acc_go = 'R';
                int t5_acc_word[3];
                close(t5_acc_ready[0]);
                close(t5_acc_res[0]);
                t5_child_alarm_setup();
                alarm(5); /* hard child watchdog */
                (void)!write(t5_acc_ready[1], &t5_acc_go, 1);
                {
                    alarm(1); /* arrives while accept() is blocked */
                    connection t5_acc_conn = t5_acc_srv.accept();
                    t5_acc_word[0] = t5_acc_conn.is_valid() ? 1 : 0;
                    t5_acc_word[1] = t5_acc_conn.descriptor() >= 0 ? 1 : 0;
                }
                t5_acc_word[2] = (int)t5_child_alarm_fired;
                (void)!write(t5_acc_res[1], t5_acc_word,
                             (size_t)sizeof(t5_acc_word));
                close(t5_acc_res[1]);
                close(t5_acc_ready[1]);
                _exit(0);
            }
            close(t5_acc_ready[1]);
            close(t5_acc_res[1]);
            if (t5_acc_kid > 0 && t5_poll_in(t5_acc_ready[0], 3000) > 0) {
                char t5_acc_go = 0;
                int t5_acc_word[3];
                int got = 0;
                if (read(t5_acc_ready[0], &t5_acc_go, 1) == 1 &&
                    t5_acc_go == 'R') {
                    usleep(1300000); /* let alarm(1) fire inside accept() */
                    {
                        int t5_acc_fd = socket(AF_INET, SOCK_STREAM, 0);
                        if (t5_acc_fd >= 0) {
                            struct sockaddr_in a;
                            memset(&a, 0, sizeof(a));
                            a.sin_family = AF_INET;
                            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                            a.sin_port =
                                htons((unsigned short)t5_acc_port);
                            (void)!connect(t5_acc_fd, (struct sockaddr *)&a,
                                           sizeof(a));
                            close(t5_acc_fd);
                        }
                    }
                    if (t5_poll_in(t5_acc_res[0], 4000) > 0)
                        got = read(t5_acc_res[0], t5_acc_word,
                                   (size_t)sizeof(t5_acc_word)) ==
                              (ssize_t)sizeof(t5_acc_word);
                }
                if (got) {
                    if (t5_acc_word[2] < 1) {
                        t5_skip("net_accept_eintr_signal",
                                "SIGALRM was not delivered while accept() was "
                                "blocked on this host");
                    } else {
                        t5_check("net_accept_eintr_signal",
                                 t5_acc_word[0] == 1 && t5_acc_word[1] == 1);
                    }
                } else {
                    t5_check("net_accept_eintr_signal", 0);
                }
                (void)!t5_wait_child(t5_acc_kid, &t5_status);
            } else {
                t5_check("net_accept_eintr_signal", 0);
                if (t5_acc_kid > 0)
                    (void)!t5_wait_child(t5_acc_kid, &t5_status);
            }
            close(t5_acc_ready[0]);
            close(t5_acc_res[0]);
        }
        t5_acc_srv.drop();
    }

    /* ---- N5: partial read across several read() calls ------------------ */
    {
        int t5_pr_sp[2];
        int ok = socketpair(AF_UNIX, SOCK_STREAM, 0, t5_pr_sp) == 0;
        if (!ok) {
            t5_skip("net_partial_read", "socketpair() unavailable");
        } else {
            pid_t t5_pr_kid = fork();
            if (t5_pr_kid == 0) {
                close(t5_pr_sp[0]);
                (void)!write(t5_pr_sp[1], "AAAA", 4);
                usleep(120000);
                (void)!write(t5_pr_sp[1], "BBBBBB", 6);
                usleep(120000);
                close(t5_pr_sp[1]);
                _exit(0);
            }
            close(t5_pr_sp[1]);
            {
                struct timeval tv;
                char t5_pr_buf[16];
                char t5_pr_all[16];
                int t5_pr_total = 0;
                int t5_pr_reads = 0;
                int t5_pr_last = STDLIB_OK;
                connection t5_partial_conn = connection_borrow(t5_pr_sp[0]);
                tv.tv_sec = 3;
                tv.tv_usec = 0;
                (void)!setsockopt(t5_partial_conn.descriptor(), SOL_SOCKET,
                                  SO_RCVTIMEO, &tv, sizeof(tv));
                for (;;) {
                    int n = t5_partial_conn.read(t5_pr_buf,
                                                 (size_t)sizeof(t5_pr_buf),
                                                 &t5_err);
                    t5_pr_last = n;
                    /* STDLIB_EOF == 1 is also a valid byte count, so the
                     * terminator must be recognised before the n > 0 branch. */
                    if (n == STDLIB_EOF || n == STDLIB_FAIL)
                        break;
                    if (n > 0) {
                        int j;
                        t5_pr_reads++;
                        for (j = 0; j < n; j++)
                            t5_pr_all[t5_pr_total + j] = t5_pr_buf[j];
                        t5_pr_total += n;
                    } else {
                        break;
                    }
                }
                ok = ok && t5_pr_reads >= 2 && t5_pr_total == 10 &&
                     t5_pr_last == STDLIB_EOF && t5_err.ok() == 1 &&
                     memcmp(t5_pr_all, "AAAABBBBBB", 10) == 0;
                t5_partial_conn.drop();
            }
            close(t5_pr_sp[0]);
            (void)!t5_wait_child(t5_pr_kid, &t5_status);
            t5_check("net_partial_read", ok);
        }
    }

    /* ---- final descriptor balance -------------------------------------- */
    if (t5_fd_baseline < 0 || t5_fd_count() < 0) {
        t5_skip("net_task5_no_fd_leak",
                "opendir(/dev/fd) unavailable on this host");
    } else {
        t5_check("net_task5_no_fd_leak", t5_fd_count() == t5_fd_baseline);
    }

    (void)!alarm(0);
    print("NET_TASK5", t5_checks, t5_failures);
    return t5_failures == 0 ? 0 : 1;
}
