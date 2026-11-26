/* Issue #61 regressions for stdlib/web.hp:
 *   - FD_SET/select bounds for a negative or out-of-range listener
 *   - web_reply() NULL guards, snprintf truncation and short writes
 *   - bounded request-line recv (a stalled client must not hang web_accept)
 *
 * The header's reply-head buffer is shrunk here so the truncation path is
 * actually exercised; the shipped default (128) is used by apps/webserver.
 * Prints one line per group and a final summary; compare to
 * regressions.expected.
 */
#define WEB_REPLY_HEAD_MAX 40
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/web.hp"

#define BIG_LEN 8192
#define OK_TEXT "OK"
#define HELLO "hello"
/* snprintf() reserves one byte for NUL, so a truncated head sends this many
 * valid characters. */
#define HEAD_SEND (WEB_REPLY_HEAD_MAX - 1)

static char big_body[BIG_LEN];
static char io_buf[BIG_LEN + 128];

static void mark(const char *name, int ok, int *failures) {
    printf("WEB_%s %d\n", name, ok ? 1 : 0);
    if (!ok)
        (*failures)++;
}

static long elapsed_ms(struct timeval *a, struct timeval *b) {
    return (long)(b->tv_sec - a->tv_sec) * 1000L +
           (long)(b->tv_usec - a->tv_usec) / 1000L;
}

/* Read until at least want bytes arrive or timeout_ms of inactivity elapse. */
static size_t read_reply(int fd, char *buf, size_t cap, size_t want,
                         int timeout_ms) {
    size_t total = 0;
    while (total < cap) {
        ssize_t got = web_recv_bounded(fd, buf + total, cap - total, timeout_ms);
        if (got <= 0)
            break;
        total += (size_t)got;
        if (total >= want)
            break;
    }
    return total;
}

int main(void) {
    int failures = 0;
    int i;
    int ok;
    char full[160];
    struct timeval t0;
    struct timeval t1;
    long ms;

    /* 1. Null arguments and invalid/out-of-range listening descriptors. */
    {
        web_server bad;
        web_conn c;
        c.fd = 77;
        c.req.body_len = 999;
        ok = (web_accept((web_server *)0, &c) == -1);
        bad.nap_ms = 10;
        bad.listen_fd = -1;
        ok = ok && (web_accept(&bad, (web_conn *)0) == -1);
        ok = ok && (web_accept(&bad, &c) == -1);
        ok = ok && (c.fd == -1 && c.req.body_len == 0);
        bad.listen_fd = FD_SETSIZE;
        ok = ok && (web_accept(&bad, &c) == -1);
        bad.listen_fd = FD_SETSIZE + 7;
        ok = ok && (web_accept(&bad, &c) == -1);
        mark("NULL_ACCEPT", ok, &failures);
    }

    /* 2. web_reply() tolerates a NULL connection/NULL body and sends a
     *    zero-length body when body == NULL. */
    {
        int sv[2];
        web_conn c;
        ssize_t n;
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) {
            c.fd = -1;
            web_reply((web_conn *)0, 200, "x");
            web_reply(&c, 200, "x");
            c.fd = sv[0];
            web_reply(&c, 200, (const char *)0);
            n = (ssize_t)read_reply(sv[1], io_buf, sizeof(io_buf) - 1,
                                    (size_t)HEAD_SEND, 100);
            ok = (n > 0 && strstr(io_buf, "HTTP/1.1 200 OK") == io_buf &&
                  strstr(io_buf, "Content-Length: 0") != (char *)0);
            close(sv[0]);
            close(sv[1]);
        } else {
            ok = 0;
        }
        mark("REPLY_NULL", ok, &failures);
    }

    /* 3. snprintf truncation: only the in-buffer prefix may be sent, yet the
     *    full body still goes out (no out-of-bounds read, no dropped body). */
    {
        int sv[2];
        web_conn c;
        int flen = snprintf(full, sizeof(full),
                            "HTTP/1.1 %d %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                            200, OK_TEXT, (size_t)strlen(HELLO));
        size_t n;
        if (flen > WEB_REPLY_HEAD_MAX && socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) {
            c.fd = sv[0];
            web_reply(&c, 200, HELLO);
            n = read_reply(sv[1], io_buf, sizeof(io_buf) - 1,
                           (size_t)HEAD_SEND + strlen(HELLO), 100);
            ok = (n == (size_t)HEAD_SEND + strlen(HELLO) &&
                  memcmp(io_buf, full, HEAD_SEND) == 0 &&
                  memcmp(io_buf + HEAD_SEND, HELLO, strlen(HELLO)) == 0);
            close(sv[0]);
            close(sv[1]);
        } else {
            ok = 0;
        }
        mark("REPLY_TRUNC", ok, &failures);
    }

    /* 4. Checked sends: a tiny SO_SNDBUF forces short writes; the reply must
     *    still arrive complete (parent drains while the child writes). */
    {
        int sv[2];
        int sndbuf = 1024;
        int st = 0;
        pid_t pid;
        size_t n;
        size_t want;
        int flen = snprintf(full, sizeof(full),
                            "HTTP/1.1 %d %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                            404, "Not Found", (size_t)BIG_LEN);
        for (i = 0; i < BIG_LEN; i++) {
            int ch = 'a';
            ch = ch + i % 26;
            big_body[i] = (char)ch;
        }
        want = (size_t)(flen < HEAD_SEND ? flen : HEAD_SEND) + BIG_LEN;
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) {
            (void)setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
            pid = fork();
            if (pid == 0) {
                web_conn child;
                child.fd = sv[0];
                web_reply(&child, 404, big_body);
                close(sv[0]);
                _exit(0);
            }
            close(sv[0]);
            n = read_reply(sv[1], io_buf, sizeof(io_buf) - 1, want, 200);
            ok = (n == want && memcmp(io_buf, full, HEAD_SEND) == 0 &&
                  memcmp(io_buf + HEAD_SEND, big_body, BIG_LEN) == 0);
            close(sv[1]);
            (void)waitpid(pid, &st, 0);
        } else {
            ok = 0;
        }
        mark("SHORT_WRITE", ok, &failures);
    }

    /* 5. Slowloris: a connected peer that never sends a request line must not
     *    hold web_accept() beyond its bounded budget. */
    {
        web_server srv;
        web_conn c;
        struct sockaddr_in addr;
        socklen_t alen = (socklen_t)sizeof(addr);
        int client;
        int accepted = 0;
        if (web_server_init(&srv, "127.0.0.1", 0) == 0 &&
            getsockname(srv.listen_fd, (struct sockaddr *)&addr, &alen) == 0) {
            srv.nap_ms = 20;
            client = socket(AF_INET, SOCK_STREAM, 0);
            if (client >= 0 &&
                connect(client, (struct sockaddr *)&addr, alen) == 0) {
                gettimeofday(&t0, (struct timezone *)0);
                accepted = web_accept(&srv, &c);
                gettimeofday(&t1, (struct timezone *)0);
                ms = elapsed_ms(&t0, &t1);
                ok = (accepted == 1 && ms < 1500 && c.req.path[0] == 0);
                (void)ms;
                web_conn_close(&c);
                close(client);
            } else {
                ok = 0;
            }
            web_server_shutdown(&srv);
        } else {
            ok = 0;
        }
        mark("SLOWLORIS", ok, &failures);
    }

    /* 6. Partial line: a peer that sends the request without the final CRLF
     *    is parsed, but only after a bounded wait. */
    {
        web_server srv;
        web_conn c;
        struct sockaddr_in addr;
        socklen_t alen = (socklen_t)sizeof(addr);
        const char *partial = "GET /partial HTTP/1.1";
        int client;
        int accepted = 0;
        if (web_server_init(&srv, "127.0.0.1", 0) == 0 &&
            getsockname(srv.listen_fd, (struct sockaddr *)&addr, &alen) == 0) {
            srv.nap_ms = 20;
            client = socket(AF_INET, SOCK_STREAM, 0);
            if (client >= 0 &&
                connect(client, (struct sockaddr *)&addr, alen) == 0 &&
                send(client, partial, strlen(partial), 0) == (ssize_t)strlen(partial)) {
                gettimeofday(&t0, (struct timezone *)0);
                accepted = web_accept(&srv, &c);
                gettimeofday(&t1, (struct timezone *)0);
                ms = elapsed_ms(&t0, &t1);
                ok = (accepted == 1 && ms < 1500 &&
                      strcmp(c.req.method, "GET") == 0 &&
                      strcmp(c.req.path, "/partial") == 0);
                (void)ms;
                web_conn_close(&c);
                close(client);
            } else {
                ok = 0;
            }
            web_server_shutdown(&srv);
        } else {
            ok = 0;
        }
        mark("SLOWLORIS_PARTIAL", ok, &failures);
    }

    printf("WEB_REGRESSIONS 6 %d\n", failures);
    return failures == 0 ? 0 : 1;
}
