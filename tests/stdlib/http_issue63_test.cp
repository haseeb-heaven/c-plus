/* Issue 63 regressions for stdlib/http.hp, fully offline.
 *
 *  - http_headers::add refuses CR/LF (header injection).
 *  - redirects are limited to http/https and to a bounded number of hops.
 *  - CURLOPT_POSTFIELDS is copied into client-owned storage that outlives
 *    the post() call.
 *
 * Two forked loopback servers are used: one answers HTTP requests, the other
 * merely records (via a pipe) whether libcurl ever connected to a redirect
 * target.  No external network is used.
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/http.hp"

/* Matches HTTP_MAX_REDIRECTS in stdlib/http.hp; kept local so this harness
 * also builds against the pre-fix header for before/after evidence. */
#define ISSUE63_MAX_REDIRECTS 5L

static int failures = 0;

static void check(const char *label, int ok) {
    print("issue63", label, ok ? "PASS" : "FAIL");
    if (!ok)
        failures++;
}

/* HTTP server: runs until killed; writes one byte to `tally` per /loop hit. */
static void serve(int listener, int port, int ftp_port, int tally) {
    for (;;) {
        int fd = accept(listener, (struct sockaddr *)0, (socklen_t *)0);
        char req[1024];
        char resp[512];
        ssize_t n;
        if (fd < 0)
            _exit(1);
        n = read(fd, req, sizeof(req) - 1u);
        if (n <= 0) {
            close(fd);
            continue;
        }
        req[n] = '\0';
        if (strstr(req, "GET /ftp ") != (char *)0) {
            snprintf(resp, sizeof(resp),
                     "HTTP/1.1 302 Found\r\n"
                     "Location: ftp://127.0.0.1:%d/secret\r\n"
                     "Content-Length: 0\r\nConnection: close\r\n\r\n",
                     ftp_port);
        } else if (strstr(req, "GET /loop ") != (char *)0) {
            (void)!write(tally, "x", 1u);
            snprintf(resp, sizeof(resp),
                     "HTTP/1.1 302 Found\r\n"
                     "Location: http://127.0.0.1:%d/loop\r\n"
                     "Content-Length: 0\r\nConnection: close\r\n\r\n",
                     port);
        } else {
            /* Echo the raw request (including a POST body) back. */
            snprintf(resp, sizeof(resp),
                     "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n"
                     "Content-Length: %d\r\nConnection: close\r\n\r\n%s",
                     (int)strlen(req), req);
        }
        (void)!write(fd, resp, strlen(resp));
        close(fd);
    }
}

/* FTP trap: records any connection to the redirect target, then closes it. */
static void trap(int listener, int fd) {
    for (;;) {
        int c = accept(listener, (struct sockaddr *)0, (socklen_t *)0);
        if (c < 0)
            _exit(1);
        (void)!write(fd, "c", 1u);
        close(c);
    }
}

static int listener_on(int *port_out) {
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    if (listener < 0)
        return -1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(listener, 16) != 0 ||
        getsockname(listener, (struct sockaddr *)&addr, &addr_len) != 0) {
        close(listener);
        return -1;
    }
    *port_out = ntohs(addr.sin_port);
    return listener;
}

static int pipe_bytes(int fd) {
    char byte;
    int count = 0;
    while (read(fd, &byte, 1u) == 1)
        count++;
    close(fd);
    return count;
}

int main(void) {
    error err = error();
    http global = http(&err);
    char body[4096];
    char url[160];
    int listener, port = 0, ftp_listener, ftp_port = 0;
    int fds[2], ftp_fds[2];
    pid_t child, ftp_child;
    http_client client;
    int rc, ftp_rc, loop_rc, posted, ok, ftp_conn, redirs;
    char post[64];
    const char *owned;

    if (!global.valid())
        return 10;

    /* 1. CR/LF injection is refused; ordinary lines still work. */
    {
        http_headers bad = http_headers();
        http_headers good = http_headers();
        error_reset(&err);
        rc = bad.add("X-Ok: yes\r\nX-Injected: no", &err);
        check("header-crlf", rc == STDLIB_FAIL && err.failed_now() &&
              err.category() == STDLIB_CATEGORY_INVALID &&
              !bad.valid() && bad.native() == (struct curl_slist *)0);
        error_reset(&err);
        rc = bad.add("X-Ok: yes\rX-Injected: no", &err);
        check("header-bare-cr", rc == STDLIB_FAIL && err.failed_now() &&
              !bad.valid() && bad.native() == (struct curl_slist *)0);
        error_reset(&err);
        rc = bad.add("X-Ok: yes\nX-Injected: no", &err);
        check("header-bare-lf", rc == STDLIB_FAIL && err.failed_now() &&
              !bad.valid() && bad.native() == (struct curl_slist *)0);
        error_reset(&err);
        rc = good.add("X-Cplus: issue63", &err);
        check("header-clean", rc == STDLIB_OK && err.ok() &&
              good.valid() && good.native() != (struct curl_slist *)0);
        good.drop();
        bad.drop();
    }

    listener = listener_on(&port);
    ftp_listener = listener_on(&ftp_port);
    if (listener < 0 || ftp_listener < 0 || pipe(fds) != 0 ||
        pipe(ftp_fds) != 0)
        return 11;

    child = fork();
    if (child == 0) {
        close(fds[0]);
        close(ftp_fds[0]);
        close(ftp_fds[1]);
        serve(listener, port, ftp_port, fds[1]);
        _exit(0);
    }
    ftp_child = fork();
    if (ftp_child == 0) {
        close(fds[0]);
        close(fds[1]);
        close(ftp_fds[0]);
        trap(ftp_listener, ftp_fds[1]);
        _exit(0);
    }
    close(fds[1]);
    close(ftp_fds[1]);
    close(listener);
    close(ftp_listener);

    client = http_client(&err);
    if (!client.valid()) {
        kill(child, SIGKILL);
        kill(ftp_child, SIGKILL);
        waitpid(child, (int *)0, 0);
        waitpid(ftp_child, (int *)0, 0);
        return 12;
    }
    client.set_timeout(5L, &err);
    client.follow_redirects(1, &err);

    /* 2. A 302 to ftp:// must NOT be followed.  libcurl reports the refused
     *    scheme as CURLE_UNSUPPORTED_PROTOCOL, but it stays on the 302 reply:
     *    no FTP body is returned and (checked below) no FTP connection is
     *    ever attempted. */
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/ftp", port);
    error_reset(&err);
    memset(body, 0, sizeof(body));
    ftp_rc = client.get(url, body, sizeof(body), &err);
    check("redirect-protocol", ftp_rc == STDLIB_FAIL && err.failed_now() &&
          client.status() == 302L && body[0] == '\0');

    /* 3. A redirect loop is capped.  Without a cap the server would answer
     *    21 hops and the GET would succeed. */
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/loop", port);
    error_reset(&err);
    memset(body, 0, sizeof(body));
    loop_rc = client.get(url, body, sizeof(body), &err);
    check("redirect-max", loop_rc == STDLIB_FAIL && err.failed_now() &&
          client.status() == 302L);

    /* 4. POST body is owned by the client, not borrowed from the caller. */
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/echo", port);
    strcpy(post, "payload=63");
    error_reset(&err);
    memset(body, 0, sizeof(body));
    posted = client.post(url, post, body, sizeof(body), &err);
    owned = client.body_copy();
    ok = owned != (const char *)0 && owned != (const char *)post &&
         strcmp(owned, "payload=63") == 0;
    memset(post, 'Z', strlen(post)); /* caller storage clobbered */
    ok = ok && strcmp(client.body_copy(), "payload=63") == 0;
    check("post-owned", posted == STDLIB_OK && err.ok() && ok &&
          strstr(body, "payload=63") != (char *)0);

    kill(child, SIGKILL);
    kill(ftp_child, SIGKILL);
    waitpid(child, (int *)0, 0);
    waitpid(ftp_child, (int *)0, 0);
    ftp_conn = pipe_bytes(ftp_fds[0]);
    redirs = pipe_bytes(fds[0]);
    check("redirect-no-ftp-connection", ftp_conn == 0);
    check("redirect-hop-count", redirs >= 2 &&
          redirs <= (int)ISSUE63_MAX_REDIRECTS + 1);

    client.drop();
    global.drop();
    print("issue63-summary", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
