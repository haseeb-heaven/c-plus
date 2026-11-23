/* Network safety: partial receives from a controlled peer, partial sends
 * forced by a small SO_SNDBUF, a 64 KiB payload, and remote-disconnect EOF.
 *
 * All peers are local fork()+socketpair() children.  Every potentially blocking
 * operation is bounded: SO_RCVTIMEO/SO_SNDTIMEO (3-5 s), poll() handshakes, a
 * bounded waitpid loop, a per-child alarm(8) and a parent alarm(25) watchdog.
 * No external servers and no raw game-memory offsets are used.
 *
 * SELF-CHECKING: "<tag> <1|0>" per check, final "NET_SAFETY <checks> <failures>".
 * Every owned handle has a unique name (C+ RAII tracks owned locals by name).
 */
#define _GNU_SOURCE
#include <errno.h>
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

#define NSP_PAYLOAD 65536
#define NSP_CHUNK 8192

static int nsp_checks = 0;
static int nsp_failures = 0;

static void nsp_check(const char *tag, int cond) {
    nsp_checks++;
    if (!cond)
        nsp_failures++;
    print(tag, cond ? 1 : 0);
    (void)!fflush(stdout);
}

static void nsp_skip(const char *tag, const char *reason) {
    nsp_checks++;
    print(tag, 1);
    print("SKIP_REASON", tag, reason);
    (void)!fflush(stdout);
}

static void nsp_watchdog(int sig) {
    static const char msg[] = "FATAL net_partial watchdog timeout\n";
    (void)sig;
    (void)!write(2, msg, sizeof(msg) - 1);
    _exit(90);
}

static void nsp_ignore(int sig) {
    (void)sig;
}

static int nsp_poll_in(int fd, int ms) {
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

static int nsp_wait_child(pid_t pid, int *status) {
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

static char nsp_byte_at(int i) {
    return (char)(i * 7 + 11);
}

int main(void) {
    error nsp_err = error();
    int nsp_status = 0;

    (void)!signal(SIGPIPE, nsp_ignore);
    (void)!signal(SIGALRM, nsp_watchdog);
    (void)!alarm(25);

    /* ---- partial receive: two short reads then EOF ----------------------- */
    {
        int nsp_pr_sp[2];
        nsp_pr_sp[0] = nsp_pr_sp[1] = -1;
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, nsp_pr_sp) != 0) {
            nsp_skip("net_partial_recv", "socketpair() unavailable");
        } else {
            pid_t nsp_pr_kid = fork();
            if (nsp_pr_kid == 0) {
                close(nsp_pr_sp[0]);
                (void)!alarm(8);
                (void)!write(nsp_pr_sp[1], "AAAA", 4);
                usleep(120000);
                (void)!write(nsp_pr_sp[1], "BBBBBB", 6);
                usleep(120000);
                close(nsp_pr_sp[1]);
                _exit(0);
            }
            close(nsp_pr_sp[1]);
            {
                struct timeval tv;
                char nsp_pr_buf[16];
                char nsp_pr_all[16];
                int nsp_pr_total = 0;
                int nsp_pr_reads = 0;
                int nsp_pr_last = STDLIB_OK;
                connection nsp_pr_conn = connection_borrow(nsp_pr_sp[0]);
                tv.tv_sec = 3;
                tv.tv_usec = 0;
                (void)!setsockopt(nsp_pr_conn.descriptor(), SOL_SOCKET,
                                  SO_RCVTIMEO, &tv, sizeof(tv));
                for (;;) {
                    int n = nsp_pr_conn.read(nsp_pr_buf,
                                             (size_t)sizeof(nsp_pr_buf),
                                             &nsp_err);
                    nsp_pr_last = n;
                    if (n == STDLIB_EOF || n == STDLIB_FAIL)
                        break;
                    if (n <= 0)
                        break;
                    nsp_pr_reads++;
                    (void)!__builtin_memcpy(nsp_pr_all + nsp_pr_total,
                                            nsp_pr_buf, (size_t)n);
                    nsp_pr_total += n;
                }
                nsp_check("net_partial_recv",
                          nsp_pr_reads >= 2 && nsp_pr_total == 10 &&
                          nsp_pr_last == STDLIB_EOF && nsp_err.ok() == 1 &&
                          __builtin_memcmp(nsp_pr_all, "AAAABBBBBB", 10) == 0);
                nsp_pr_conn.drop(); /* borrowed: leaves nsp_pr_sp[0] open */
            }
            close(nsp_pr_sp[0]);
            (void)!nsp_wait_child(nsp_pr_kid, &nsp_status);
        }
    }

    /* ---- partial sends + 64 KiB payload integrity ------------------------ */
    {
        int nsp_lg_sp[2];
        int nsp_lg_res[2];
        char nsp_lg_tx[NSP_PAYLOAD];
        int ok;
        int i;
        nsp_lg_sp[0] = nsp_lg_sp[1] = -1;
        nsp_lg_res[0] = nsp_lg_res[1] = -1;
        ok = socketpair(AF_UNIX, SOCK_STREAM, 0, nsp_lg_sp) == 0;
        for (i = 0; i < NSP_PAYLOAD; i++)
            nsp_lg_tx[i] = nsp_byte_at(i);
        if (ok)
            ok = pipe(nsp_lg_res) == 0;
        if (!ok) {
            nsp_skip("net_partial_send", "socketpair()/pipe() unavailable");
            nsp_skip("net_large_payload", "socketpair()/pipe() unavailable");
            if (nsp_lg_sp[0] >= 0) {
                close(nsp_lg_sp[0]);
                close(nsp_lg_sp[1]);
            }
        } else {
            int nsp_sndbuf = 4096;
            struct timeval tv;
            (void)!setsockopt(nsp_lg_sp[0], SOL_SOCKET, SO_SNDBUF,
                              &nsp_sndbuf, sizeof(nsp_sndbuf));
            tv.tv_sec = 5;
            tv.tv_usec = 0;
            (void)!setsockopt(nsp_lg_sp[0], SOL_SOCKET, SO_SNDTIMEO, &tv,
                              sizeof(tv));
            tv.tv_sec = 5;
            (void)!setsockopt(nsp_lg_sp[1], SOL_SOCKET, SO_RCVTIMEO, &tv,
                              sizeof(tv));

            pid_t nsp_lg_kid = fork();
            if (nsp_lg_kid == 0) {
                char nsp_lg_rx[NSP_PAYLOAD];
                char nsp_lg_chunk[NSP_CHUNK];
                int nsp_lg_total = 0;
                int nsp_lg_ok = 1;
                int nsp_lg_word[3];
                close(nsp_lg_sp[0]);
                close(nsp_lg_res[0]);
                (void)!alarm(8);
                while (nsp_lg_total < NSP_PAYLOAD) {
                    ssize_t m = read(nsp_lg_sp[1], nsp_lg_chunk,
                                     (size_t)sizeof(nsp_lg_chunk));
                    if (m > 0) {
                        (void)!__builtin_memcpy(nsp_lg_rx + nsp_lg_total,
                                                nsp_lg_chunk, (size_t)m);
                        nsp_lg_total += (int)m;
                        usleep(2000); /* slow reader -> short writes */
                    } else if (m == 0) {
                        break;
                    } else if (errno != EINTR) {
                        break;
                    }
                }
                if (nsp_lg_total != NSP_PAYLOAD)
                    nsp_lg_ok = 0;
                if (nsp_lg_ok) {
                    int j;
                    for (j = 0; j < NSP_PAYLOAD; j++)
                        if (nsp_lg_rx[j] != nsp_byte_at(j)) {
                            nsp_lg_ok = 0;
                            break;
                        }
                }
                nsp_lg_word[0] = nsp_lg_total;
                nsp_lg_word[1] = nsp_lg_ok;
                nsp_lg_word[2] = 0;
                if (nsp_lg_total == NSP_PAYLOAD)
                    nsp_lg_word[2] =
                        nsp_lg_rx[0] == nsp_byte_at(0) &&
                                nsp_lg_rx[NSP_PAYLOAD - 1] ==
                                    nsp_byte_at(NSP_PAYLOAD - 1)
                            ? 1
                            : 0;
                (void)!write(nsp_lg_res[1], nsp_lg_word,
                             (size_t)sizeof(nsp_lg_word));
                close(nsp_lg_res[1]);
                close(nsp_lg_sp[1]);
                _exit(0);
            }

            close(nsp_lg_sp[1]);
            close(nsp_lg_res[1]);
            {
                connection nsp_lg_conn = connection_borrow(nsp_lg_sp[0]);
                error_reset(&nsp_err);
                (void)!nsp_lg_conn.write(nsp_lg_tx, (size_t)NSP_PAYLOAD,
                                         &nsp_err);
                nsp_check("net_partial_send",
                          nsp_err.ok() == 1 &&
                              nsp_lg_conn.descriptor() == nsp_lg_sp[0]);
                nsp_lg_conn.drop(); /* borrowed: leaves nsp_lg_sp[0] open */
            }
            {
                int nsp_lg_word[3];
                int got = 0;
                if (nsp_poll_in(nsp_lg_res[0], 5000) > 0)
                    got = read(nsp_lg_res[0], nsp_lg_word,
                               (size_t)sizeof(nsp_lg_word)) ==
                          (ssize_t)sizeof(nsp_lg_word);
                nsp_check("net_large_payload",
                          got && nsp_lg_word[0] == NSP_PAYLOAD &&
                          nsp_lg_word[1] == 1 && nsp_lg_word[2] == 1);
            }
            close(nsp_lg_res[0]);
            close(nsp_lg_sp[0]);
            (void)!nsp_wait_child(nsp_lg_kid, &nsp_status);
        }
    }

    /* ---- remote disconnect: clean FIN becomes STDLIB_EOF ---------------- */
    {
        int nsp_eof_sp[2];
        nsp_eof_sp[0] = nsp_eof_sp[1] = -1;
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, nsp_eof_sp) != 0) {
            nsp_skip("net_remote_disconnect", "socketpair() unavailable");
        } else {
            pid_t nsp_eof_kid = fork();
            if (nsp_eof_kid == 0) {
                close(nsp_eof_sp[0]);
                (void)!alarm(8);
                (void)!write(nsp_eof_sp[1], "BYE", 3);
                usleep(120000);
                close(nsp_eof_sp[1]); /* clean FIN */
                _exit(0);
            }
            close(nsp_eof_sp[1]);
            {
                struct timeval tv;
                char nsp_eof_buf[16];
                int nsp_eof_first;
                int nsp_eof_after;
                connection nsp_eof_conn = connection_borrow(nsp_eof_sp[0]);
                tv.tv_sec = 3;
                tv.tv_usec = 0;
                (void)!setsockopt(nsp_eof_conn.descriptor(), SOL_SOCKET,
                                  SO_RCVTIMEO, &tv, sizeof(tv));
                nsp_eof_first = nsp_eof_conn.read(nsp_eof_buf,
                                                  (size_t)sizeof(nsp_eof_buf),
                                                  &nsp_err);
                nsp_eof_after = nsp_eof_conn.read(nsp_eof_buf,
                                                  (size_t)sizeof(nsp_eof_buf),
                                                  &nsp_err);
                nsp_check("net_remote_disconnect",
                          nsp_eof_first == 3 &&
                          __builtin_memcmp(nsp_eof_buf, "BYE", 3) == 0 &&
                          nsp_eof_after == STDLIB_EOF && nsp_err.ok() == 1);
                nsp_eof_conn.drop();
            }
            close(nsp_eof_sp[0]);
            (void)!nsp_wait_child(nsp_eof_kid, &nsp_status);
        }
    }

    (void)!alarm(0);
    print("NET_SAFETY", nsp_checks, nsp_failures);
    return nsp_failures == 0 ? 0 : 1;
}
