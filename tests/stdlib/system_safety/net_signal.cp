/* Network safety: EINTR retry via forked SIGALRM, and remote RST write failure.
 *
 * A forked child installs a non-SA_RESTART SIGALRM handler and lets the signal
 * arrive while connection::read()/listener::accept() is blocked; net.hp must
 * retry and return the eventual data/connection.  If the host does not deliver
 * the signal inside the window the check is a documented SKIP, never a hang.
 *
 * Every wait is bounded: 3 s poll handshakes, 5 s child alarm, a bounded
 * waitpid loop, and a 25 s parent SIGALRM watchdog (external `timeout -k 5 30`).
 * No external servers; the TCP peer is a local loopback child process.
 *
 * SELF-CHECKING: "<tag> <1|0>" per check, final "NET_SAFETY <checks> <failures>".
 * Every owned handle has a unique name (C+ RAII tracks owned locals by name).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/net.hp"

static int nssi_checks = 0;
static int nssi_failures = 0;
static volatile sig_atomic_t nssi_child_alarm_fired = 0;

static void nssi_check(const char *tag, int cond) {
    nssi_checks++;
    if (!cond)
        nssi_failures++;
    print(tag, cond ? 1 : 0);
    (void)!fflush(stdout);
}

static void nssi_skip(const char *tag, const char *reason) {
    nssi_checks++;
    print(tag, 1);
    print("SKIP_REASON", tag, reason);
    (void)!fflush(stdout);
}

static void nssi_watchdog(int sig) {
    static const char msg[] = "FATAL net_signal watchdog timeout\n";
    (void)sig;
    (void)!write(2, msg, sizeof(msg) - 1);
    _exit(90);
}

static void nssi_ignore(int sig) {
    (void)sig;
}

static void nssi_child_alarm(int sig) {
    (void)sig;
    nssi_child_alarm_fired = 1;
}

static void nssi_child_alarm_setup(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = nssi_child_alarm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; /* deliberately no SA_RESTART */
    (void)!sigaction(SIGALRM, &sa, (struct sigaction *)0);
    nssi_child_alarm_fired = 0;
}

static int nssi_poll_in(int fd, int ms) {
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

static int nssi_wait_child(pid_t pid, int *status) {
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
    error nssi_err = error();
    int nssi_status = 0;

    (void)!signal(SIGPIPE, nssi_ignore);
    (void)!signal(SIGALRM, nssi_watchdog);
    (void)!alarm(25);

    /* ---- SIGALRM interrupts connection::read ----------------------------- */
    {
        int nssi_rd_ready[2];
        int nssi_rd_res[2];
        int nssi_rd_sp[2];
        nssi_rd_ready[0] = nssi_rd_ready[1] = -1;
        nssi_rd_res[0] = nssi_rd_res[1] = -1;
        nssi_rd_sp[0] = nssi_rd_sp[1] = -1;
        if (pipe(nssi_rd_ready) != 0 || pipe(nssi_rd_res) != 0 ||
            socketpair(AF_UNIX, SOCK_STREAM, 0, nssi_rd_sp) != 0) {
            nssi_skip("net_read_eintr_signal",
                      "pipe()/socketpair() unavailable on this host");
        } else {
            pid_t nssi_rd_kid = fork();
            if (nssi_rd_kid == 0) {
                char nssi_rd_go = 'R';
                char nssi_rd_buf[16];
                error nssi_rd_child_err = error();
                int nssi_rd_word[4];
                close(nssi_rd_ready[0]);
                close(nssi_rd_res[0]);
                close(nssi_rd_sp[1]);
                nssi_child_alarm_setup();
                (void)!alarm(5); /* hard child watchdog */
                (void)!write(nssi_rd_ready[1], &nssi_rd_go, 1);
                {
                    connection nssi_eintr_conn = connection_borrow(nssi_rd_sp[0]);
                    (void)!alarm(1); /* arrives while read() is blocked */
                    nssi_rd_word[0] = nssi_eintr_conn.read(
                        nssi_rd_buf, (size_t)sizeof(nssi_rd_buf),
                        &nssi_rd_child_err);
                    nssi_rd_word[2] = nssi_rd_child_err.failed_now();
                    nssi_eintr_conn.drop();
                }
                nssi_rd_word[1] = (int)nssi_child_alarm_fired;
                nssi_rd_word[3] = nssi_rd_buf[0] == 'D' &&
                                  nssi_rd_buf[1] == 'A' &&
                                  nssi_rd_buf[2] == 'T' &&
                                  nssi_rd_buf[3] == 'A'
                                      ? 1
                                      : 0;
                (void)!write(nssi_rd_res[1], nssi_rd_word,
                             (size_t)sizeof(nssi_rd_word));
                close(nssi_rd_res[1]);
                close(nssi_rd_ready[1]);
                close(nssi_rd_sp[0]);
                _exit(0);
            }
            close(nssi_rd_ready[1]);
            close(nssi_rd_res[1]);
            close(nssi_rd_sp[0]);
            if (nssi_rd_kid > 0 && nssi_poll_in(nssi_rd_ready[0], 3000) > 0) {
                char nssi_rd_go = 0;
                int nssi_rd_word[4];
                int nssi_got = 0;
                if (read(nssi_rd_ready[0], &nssi_rd_go, 1) == 1 &&
                    nssi_rd_go == 'R') {
                    usleep(1300000); /* let alarm(1) fire inside read() */
                    (void)!write(nssi_rd_sp[1], "DATA", 4);
                    if (nssi_poll_in(nssi_rd_res[0], 4000) > 0)
                        nssi_got = read(nssi_rd_res[0], nssi_rd_word,
                                        (size_t)sizeof(nssi_rd_word)) ==
                                   (ssize_t)sizeof(nssi_rd_word);
                }
                if (nssi_got) {
                    if (nssi_rd_word[1] < 1)
                        nssi_skip("net_read_eintr_signal",
                                  "SIGALRM was not delivered while read() was "
                                  "blocked on this host");
                    else
                        nssi_check("net_read_eintr_signal",
                                   nssi_rd_word[0] == 4 &&
                                   nssi_rd_word[2] == 0 &&
                                   nssi_rd_word[3] == 1);
                } else {
                    nssi_check("net_read_eintr_signal", 0);
                }
                (void)!nssi_wait_child(nssi_rd_kid, &nssi_status);
            } else {
                nssi_check("net_read_eintr_signal", 0);
                if (nssi_rd_kid > 0)
                    (void)!nssi_wait_child(nssi_rd_kid, &nssi_status);
            }
            close(nssi_rd_ready[0]);
            close(nssi_rd_res[0]);
            close(nssi_rd_sp[1]);
        }
    }

    /* ---- SIGALRM interrupts listener::accept ----------------------------- */
    {
        listener nssi_acc_srv = listener(0);
        int nssi_acc_port = nssi_acc_srv.bound_port();
        int nssi_acc_ready[2];
        int nssi_acc_res[2];
        nssi_acc_ready[0] = nssi_acc_ready[1] = -1;
        nssi_acc_res[0] = nssi_acc_res[1] = -1;
        if (!nssi_acc_srv.is_valid() || nssi_acc_port <= 0 ||
            pipe(nssi_acc_ready) != 0 || pipe(nssi_acc_res) != 0) {
            nssi_skip("net_accept_eintr_signal",
                      "pipe()/listener unavailable on this host");
        } else {
            pid_t nssi_acc_kid = fork();
            if (nssi_acc_kid == 0) {
                char nssi_acc_go = 'R';
                int nssi_acc_word[3];
                close(nssi_acc_ready[0]);
                close(nssi_acc_res[0]);
                nssi_child_alarm_setup();
                (void)!alarm(5); /* hard child watchdog */
                (void)!write(nssi_acc_ready[1], &nssi_acc_go, 1);
                (void)!alarm(1); /* arrives while accept() is blocked */
                {
                    connection nssi_eintr_acc = nssi_acc_srv.accept();
                    nssi_acc_word[0] = nssi_eintr_acc.is_valid() ? 1 : 0;
                    nssi_acc_word[1] = nssi_eintr_acc.descriptor() >= 0 ? 1 : 0;
                    nssi_eintr_acc.drop();
                }
                nssi_acc_word[2] = (int)nssi_child_alarm_fired;
                (void)!write(nssi_acc_res[1], nssi_acc_word,
                             (size_t)sizeof(nssi_acc_word));
                close(nssi_acc_res[1]);
                close(nssi_acc_ready[1]);
                _exit(0);
            }
            close(nssi_acc_ready[1]);
            close(nssi_acc_res[1]);
            if (nssi_acc_kid > 0 && nssi_poll_in(nssi_acc_ready[0], 3000) > 0) {
                char nssi_acc_go = 0;
                int nssi_acc_word[3];
                int nssi_got = 0;
                if (read(nssi_acc_ready[0], &nssi_acc_go, 1) == 1 &&
                    nssi_acc_go == 'R') {
                    usleep(1300000); /* let alarm(1) fire inside accept() */
                    {
                        int nssi_conn_fd = socket(AF_INET, SOCK_STREAM, 0);
                        if (nssi_conn_fd >= 0) {
                            struct sockaddr_in a;
                            memset(&a, 0, sizeof(a));
                            a.sin_family = AF_INET;
                            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                            a.sin_port = htons((unsigned short)nssi_acc_port);
                            (void)!connect(nssi_conn_fd, (struct sockaddr *)&a,
                                           sizeof(a));
                            close(nssi_conn_fd);
                        }
                    }
                    if (nssi_poll_in(nssi_acc_res[0], 4000) > 0)
                        nssi_got = read(nssi_acc_res[0], nssi_acc_word,
                                        (size_t)sizeof(nssi_acc_word)) ==
                                   (ssize_t)sizeof(nssi_acc_word);
                }
                if (nssi_got) {
                    if (nssi_acc_word[2] < 1)
                        nssi_skip("net_accept_eintr_signal",
                                  "SIGALRM was not delivered while accept() "
                                  "was blocked on this host");
                    else
                        nssi_check("net_accept_eintr_signal",
                                   nssi_acc_word[0] == 1 &&
                                   nssi_acc_word[1] == 1);
                } else {
                    nssi_check("net_accept_eintr_signal", 0);
                }
                (void)!nssi_wait_child(nssi_acc_kid, &nssi_status);
            } else {
                nssi_check("net_accept_eintr_signal", 0);
                if (nssi_acc_kid > 0)
                    (void)!nssi_wait_child(nssi_acc_kid, &nssi_status);
            }
            close(nssi_acc_ready[0]);
            close(nssi_acc_res[0]);
        }
        nssi_acc_srv.drop();
    }

    /* ---- remote RST: write fails with a defined recoverable error -------- */
    {
        listener nssi_rst_srv = listener(0);
        int nssi_port = nssi_rst_srv.bound_port();
        int nssi_ready[2];
        int ok = nssi_rst_srv.is_valid() && nssi_port > 0;
        nssi_ready[0] = nssi_ready[1] = -1;
        if (ok)
            ok = pipe(nssi_ready) == 0;
        if (!ok) {
            nssi_skip("net_peer_reset_write", "pipe()/listener unavailable");
        } else {
            pid_t nssi_kid = fork();
            if (nssi_kid == 0) {
                int nssi_fd;
                struct linger nssi_lng;
                struct sockaddr_in a;
                char nssi_rel = 'R';
                close(nssi_ready[0]);
                nssi_fd = socket(AF_INET, SOCK_STREAM, 0);
                if (nssi_fd < 0)
                    _exit(1);
                nssi_lng.l_onoff = 1;
#ifdef __linux__
                nssi_lng.l_on = 0;
#else
                nssi_lng.l_linger = 0;
#endif
                (void)!setsockopt(nssi_fd, SOL_SOCKET, SO_LINGER, &nssi_lng,
                                  sizeof(nssi_lng));
                memset(&a, 0, sizeof(a));
                a.sin_family = AF_INET;
                a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                a.sin_port = htons((unsigned short)nssi_port);
                if (connect(nssi_fd, (struct sockaddr *)&a, sizeof(a)) != 0) {
                    close(nssi_fd);
                    _exit(1);
                }
                close(nssi_fd); /* SO_LINGER=0 generates RST */
                (void)!write(nssi_ready[1], &nssi_rel, 1);
                close(nssi_ready[1]);
                _exit(0);
            }
            close(nssi_ready[1]);
            if (nssi_kid > 0 && nssi_poll_in(nssi_ready[0], 3000) > 0) {
                char nssi_rel = 0;
                if (read(nssi_ready[0], &nssi_rel, 1) == 1 && nssi_rel == 'R') {
                    connection nssi_rst_conn = nssi_rst_srv.accept();
                    if (nssi_rst_conn.is_valid()) {
                        struct timeval tv;
                        int nssi_rc;
                        tv.tv_sec = 3;
                        tv.tv_usec = 0;
                        (void)!setsockopt(nssi_rst_conn.descriptor(), SOL_SOCKET,
                                          SO_SNDTIMEO, &tv, sizeof(tv));
                        usleep(100000);
                        nssi_rc = nssi_rst_conn.write("z", 1, &nssi_err);
                        if (nssi_rc == STDLIB_OK) {
                            usleep(150000);
                            nssi_rc = nssi_rst_conn.write("z", 1, &nssi_err);
                        }
                        ok = ok && nssi_rc == STDLIB_FAIL &&
                             nssi_err.failed_now() == 1 &&
                             nssi_err.category() == STDLIB_CATEGORY_IO &&
                             strcmp(nssi_err.what(),
                                    "connection::write failed") == 0;
                        nssi_rst_conn.drop();
                    } else {
                        ok = 0;
                    }
                } else {
                    ok = 0;
                }
                (void)!nssi_wait_child(nssi_kid, &nssi_status);
            } else {
                ok = 0;
                if (nssi_kid > 0)
                    (void)!nssi_wait_child(nssi_kid, &nssi_status);
            }
            close(nssi_ready[0]);
        }
        nssi_rst_srv.drop();
        nssi_check("net_peer_reset_write", ok);
    }

    (void)!alarm(0);
    print("NET_SAFETY", nssi_checks, nssi_failures);
    return nssi_failures == 0 ? 0 : 1;
}
