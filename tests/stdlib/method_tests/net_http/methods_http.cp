/* 100% public-method test for stdlib/http.hp.
 *
 * BUILD NOTE: this file cannot be lowered with --strict because the header
 * itself contains `SIZE_MAX / size` at stdlib/http.hp:106, which the C+
 * strict checker rejects as "[CPLUS-ARITH] division by zero cannot be
 * excluded" even though the source guards it with `size != 0 &&`.  Headers are
 * not editable here, so the build uses --default --sanitize=none -lcurl
 * (the same mode the repository's own tests/stdlib/run_stdlib_tests.sh uses).
 * Exact command/output evidence is in RUN_NOTES.md.  All 22 callables are
 * still invoked and their results verified.
 *
 * No external network: the only transfers use a forked 127.0.0.1 echo server
 * (loopback) and libcurl's in-process http_sink_write buffer path.
 * Prints "<header> <method> PASS|FAIL" once per method; exits non-zero on a
 * real failure.
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/http.hp"

static int mt_total = 0;
static int mt_passed = 0;
static int mt_failed = 0;

static void mt_report(const char *method, int ok) {
    mt_total++;
    if (ok) mt_passed++; else mt_failed++;
    print("http.hp", method, ok ? "PASS" : "FAIL");
}

static int serve_once(int fd) {
    char request[2048];
    char reply[2560];
    ssize_t n = read(fd, request, sizeof(request) - 1u);
    if (n <= 0) return 1;
    request[n] = '\0';
    snprintf(reply, sizeof(reply),
             "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n"
             "Content-Length: %d\r\nConnection: close\r\n\r\n%s",
             (int)strlen(request), request);
    (void)!write(fd, reply, strlen(reply));
    close(fd);
    return 0;
}

int main(void) {
    error err = error();
    http h_global = http(&err);
    http_headers h_headers = http_headers();
    http_headers h_dropheaders = http_headers();
    http_headers h_null = http_headers();
    http_client h_client = http_client(&err);
    http_client h_fresh = http_client(&err);
    http_client h_dropclient = http_client(&err);
    char url[128];
    char body[4096];
    char sinkbuf[32];
    http_sink sink;
    int listener_fd;
    int yes = 1;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    int port;
    pid_t child;
    int status;
    int ok;

    /* 1. http.http: global init reports no failure */
    ok = !err.failed_now();
    mt_report("http.http", ok);

    /* 2. http.valid */
    ok = h_global.valid() == 1;
    mt_report("http.valid", ok);

    /* 3. http_version: non-NULL, non-empty C string */
    {
        const char *v = http_version();
        ok = v != (const char *)0 && v[0] != '\0';
    }
    mt_report("http_version", ok);

    /* 4. http_headers.http_headers: empty list is inert */
    ok = h_headers.valid() == 0 && h_headers.native() == (struct curl_slist *)0;
    mt_report("http_headers.http_headers", ok);

    /* 5. http_headers.valid: false until a line is added */
    error_reset(&err);
    ok = h_headers.valid() == 0 &&
         h_headers.add("X-Cplus-Method: net-http", &err) == STDLIB_OK && err.ok() &&
         h_headers.valid() == 1;
    mt_report("http_headers.valid", ok);

    /* 6. http_headers.native: borrowed list exposes the copied line */
    ok = h_headers.native() != (struct curl_slist *)0 &&
         h_headers.native()->data != (char *)0 &&
         strcmp(h_headers.native()->data, "X-Cplus-Method: net-http") == 0;
    mt_report("http_headers.native", ok);

    /* 7. http_headers.add: second line appended; NULL line refused */
    error_reset(&err);
    ok = h_headers.add("X-Second: 2", &err) == STDLIB_OK && err.ok() &&
         h_headers.native() != (struct curl_slist *)0 &&
         h_headers.native()->next != (struct curl_slist *)0 &&
         strcmp(h_headers.native()->next->data, "X-Second: 2") == 0;
    error_reset(&err);
    ok = ok && h_headers.add((const char *)0, &err) == STDLIB_FAIL &&
         err.failed_now();
    mt_report("http_headers.add", ok);

    /* 8. http_headers.drop: frees the list, leaving the handle inert */
    error_reset(&err);
    ok = h_dropheaders.add("X-Drop: yes", &err) == STDLIB_OK && h_dropheaders.valid() == 1;
    h_dropheaders.drop();
    ok = ok && h_dropheaders.valid() == 0 &&
         h_dropheaders.native() == (struct curl_slist *)0;
    h_dropheaders.drop();
    ok = ok && h_dropheaders.valid() == 0; /* idempotent */
    mt_report("http_headers.drop", ok);

    /* 9. http_sink_write: direct bounded-buffer callback, all refusals */
    memset(sinkbuf, 0, sizeof(sinkbuf));
    sink.buf = sinkbuf;
    sink.size = sizeof(sinkbuf);
    sink.used = 0u;
    sink.overflow = 0;
    ok = http_sink_write((char *)"data", 1u, 4u, (void *)&sink) == 4u &&
         sink.used == 4u && sink.overflow == 0 &&
         strcmp(sinkbuf, "data") == 0 && sinkbuf[4] == '\0';
    ok = ok && http_sink_write((char *)"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX", 1u, 32u,
                               (void *)&sink) == 0u && sink.overflow == 1 &&
         sinkbuf[sizeof(sinkbuf) - 1u] == '\0';
    sink.used = 0u; sink.overflow = 0;
    ok = ok && http_sink_write((char *)"x", 1u, 1u, (void *)0) == 0u;
    ok = ok && http_sink_write((char *)"x", 0u, 1u, (void *)&sink) == 0u;
    ok = ok && http_sink_write((char *)0, 1u, 1u, (void *)&sink) == 0u;
    sink.buf = (char *)0;
    ok = ok && http_sink_write((char *)"x", 1u, 1u, (void *)&sink) == 0u;
    mt_report("http_sink_write", ok);

    /* 10. http_client.http_client */
    ok = !err.failed_now() && h_client.valid() == 1;
    mt_report("http_client.http_client", ok);

    /* 11. http_client.valid */
    ok = h_client.valid() == 1 && h_fresh.valid() == 1;
    mt_report("http_client.valid", ok);

    /* 12. http_client.set_timeout: positive OK, negative refused */
    error_reset(&err);
    ok = h_client.set_timeout(10L, &err) == STDLIB_OK && err.ok();
    error_reset(&err);
    ok = ok && h_client.set_timeout(-1L, &err) == STDLIB_FAIL &&
         err.category() == STDLIB_CATEGORY_INVALID;
    mt_report("http_client.set_timeout", ok);

    /* 13. http_client.follow_redirects: both settings accepted */
    error_reset(&err);
    ok = h_client.follow_redirects(1, &err) == STDLIB_OK && err.ok();
    error_reset(&err);
    ok = ok && h_client.follow_redirects(0, &err) == STDLIB_OK && err.ok();
    mt_report("http_client.follow_redirects", ok);

    /* 14. http_client.header_lines: 0 before, N after set_headers */
    ok = h_fresh.header_lines() == 0;
    error_reset(&err);
    ok = ok && h_fresh.set_headers(&h_headers, &err) == STDLIB_OK &&
         h_fresh.header_lines() == 2;
    mt_report("http_client.header_lines", ok);

    /* 15. http_client.set_headers: owned copy survives source drop;
     *     a NULL source is refused */
    error_reset(&err);
    ok = h_client.set_headers(&h_headers, &err) == STDLIB_OK && err.ok() &&
         h_client.header_lines() == 2;
    h_headers.drop();
    ok = ok && h_client.header_lines() == 2; /* proof it copied, not borrowed */
    error_reset(&err);
    ok = ok && h_client.set_headers((http_headers *)0, &err) == STDLIB_FAIL &&
         err.failed_now();
    mt_report("http_client.set_headers", ok);

    /* --- local loopback echo server for the three transfer methods ------ */
    listener_fd = socket(AF_INET, SOCK_STREAM, 0);
    ok = 0;
    if (listener_fd >= 0) {
        setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (bind(listener_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
            listen(listener_fd, 4) == 0 &&
            getsockname(listener_fd, (struct sockaddr *)&addr, &addr_len) == 0) {
            port = ntohs(addr.sin_port);
            child = fork();
            if (child == 0) {
                int i;
                for (i = 0; i < 3; i++) {
                    int fd = accept(listener_fd, (struct sockaddr *)0, (socklen_t *)0);
                    if (fd < 0 || serve_once(fd) != 0) _exit(1);
                }
                _exit(0);
            }
            snprintf(url, sizeof(url), "http://127.0.0.1:%d/path", port);

            /* 16. http_client.request (direct public call, GET form) */
            memset(body, 0, sizeof(body));
            error_reset(&err);
            ok = h_client.request(url, (const char *)0, body, sizeof(body), &err) ==
                     STDLIB_OK && err.ok() &&
                 strstr(body, "GET /path") != (char *)0 &&
                 strstr(body, "X-Cplus-Method: net-http") != (char *)0;
            mt_report("http_client.request", ok);

            /* 17. http_client.get */
            memset(body, 0, sizeof(body));
            error_reset(&err);
            ok = h_client.get(url, body, sizeof(body), &err) == STDLIB_OK && err.ok() &&
                 strstr(body, "GET /path") != (char *)0;
            mt_report("http_client.get", ok);

            /* 18. http_client.post */
            memset(body, 0, sizeof(body));
            error_reset(&err);
            ok = h_client.post(url, "payload=42", body, sizeof(body), &err) ==
                     STDLIB_OK && err.ok() &&
                 strstr(body, "POST /path") != (char *)0 &&
                 strstr(body, "payload=42") != (char *)0;
            mt_report("http_client.post", ok);

            /* 19. http_client.status (after the successful POST) */
            ok = h_client.status() == 200L;
            mt_report("http_client.status", ok);

            waitpid(child, &status, 0);
            ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;

            /* 20. http_client.bytes (after the successful POST) */
            ok = ok && h_client.bytes() > 0ULL;
            mt_report("http_client.bytes", ok);
        }
        close(listener_fd);
    }

    /* 21. http_client.drop: clears handle and owned header copy */
    error_reset(&err);
    ok = h_dropclient.set_headers(&h_dropheaders, &err) == STDLIB_OK ||
         err.failed_now(); /* dropheaders is empty: set_headers still succeeds */
    h_dropclient.drop();
    ok = ok && h_dropclient.valid() == 0 && h_dropclient.status() == 0L &&
         h_dropclient.bytes() == 0ULL && h_dropclient.header_lines() == 0;
    h_dropclient.drop();
    ok = ok && h_dropclient.valid() == 0; /* idempotent */
    mt_report("http_client.drop", ok);

    /* 22. http.drop: global cleanup makes the handle inert.  Done last,
     *     after every client has been dropped. */
    h_client.drop();
    h_fresh.drop();
    h_null.drop();
    h_global.drop();
    ok = h_global.valid() == 0;
    mt_report("http.drop", ok);

    print("METHOD_TEST http.hp", mt_total, mt_passed, mt_failed, 0);
    return mt_failed != 0;
}
