/* Issue 215 regression: initial-URL protocol restriction + header lifetime.
 *
 *  - CURLOPT_PROTOCOLS_STR must reject file:// (and other non-HTTP schemes)
 *    on the initial request, not just on redirects.
 *  - set_headers() must install the new list before freeing the old one, and
 *    must release the unused copy when setopt fails.  curl_easy_setopt is
 *    interposed below so both failure paths are reachable from a test.
 *
 * Fully offline: a forked loopback server echoes the request headers.
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <curl/curl.h>

/* Interpose only CURLOPT_HTTPHEADER; every other option reaches libcurl
 * unchanged with its original argument type. */
#undef curl_easy_setopt
static int injected_header_failure = 0;
static int header_setopt_calls = 0;
static CURLcode issue215_header_setopt(CURL *h, void *list) {
    header_setopt_calls++;
    if (injected_header_failure)
        return CURLE_FAILED_INIT;
    return (curl_easy_setopt)(h, CURLOPT_HTTPHEADER, (struct curl_slist *)list);
}
#define curl_easy_setopt(h, opt, val)                                         \
    (((opt) == CURLOPT_HTTPHEADER)                                            \
         ? issue215_header_setopt((h), (void *)(val))                         \
         : (curl_easy_setopt)((h), (opt), (val)))

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/http.hp"

static int failures = 0;

static void check(const char *label, int ok) {
    print("issue215", label, ok ? "PASS" : "FAIL");
    if (!ok)
        failures++;
}

static void serve_echo(int listener) {
    for (;;) {
        int fd = accept(listener, (struct sockaddr *)0, (socklen_t *)0);
        char req[4096];
        char resp[4608];
        ssize_t n;
        if (fd < 0)
            _exit(1);
        n = read(fd, req, sizeof(req) - 1u);
        if (n <= 0) {
            close(fd);
            continue;
        }
        req[n] = '\0';
        snprintf(resp, sizeof(resp),
                 "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n"
                 "Content-Length: %d\r\nConnection: close\r\n\r\n%s",
                 (int)strlen(req), req);
        (void)!write(fd, resp, strlen(resp));
        close(fd);
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

int main(void) {
    /* Line-buffered so every check is visible even if an unfixed build
     * crashes while libcurl uses a freed header list. */
    setvbuf(stdout, (char *)0, _IOLBF, 0u);
    error err = error();
    http global = http(&err);
    http_client client = http_client(&err);
    http_client fresh = http_client(&err);
    http_headers old_headers = http_headers();
    http_headers new_headers = http_headers();
    char body[8192];
    char url[512];
    char file_path[512];
    char file_url[640];
    int listener, port = 0;
    pid_t child;
    int rc, have_old, have_new;

    if (!global.valid() || !client.valid() || !fresh.valid())
        return 10;
    check("construction", header_setopt_calls == 0);

    /* A readable local file: before the fix libcurl happily returned it. */
    snprintf(file_path, sizeof(file_path), "build/stdlib-tests/issue215_data.txt");
    {
        FILE *f = fopen(file_path, "w");
        if (f == (FILE *)0)
            return 11;
        fprintf(f, "cplus-issue215-local-file\n");
        fclose(f);
    }
    {
        char cwd[512];
        if (getcwd(cwd, sizeof(cwd)) == (char *)0)
            return 12;
        snprintf(file_url, sizeof(file_url), "file://%s/%s", cwd, file_path);
    }
    error_reset(&err);
    rc = client.get(file_url, body, sizeof(body), &err);
    check("initial-file", rc == STDLIB_FAIL && err.failed_now() && body[0] == '\0');

    error_reset(&err);
    rc = client.get("gopher://127.0.0.1:1/issue215", body, sizeof(body), &err);
    check("initial-gopher", rc == STDLIB_FAIL && err.failed_now());

    error_reset(&err);
    rc = client.get("dict://127.0.0.1:1/issue215", body, sizeof(body), &err);
    check("initial-dict", rc == STDLIB_FAIL && err.failed_now());

    listener = listener_on(&port);
    if (listener < 0)
        return 13;
    child = fork();
    if (child == 0) {
        serve_echo(listener);
        _exit(0);
    }
    close(listener);
    if (child < 0)
        return 14;

    snprintf(url, sizeof(url), "http://127.0.0.1:%d/echo", port);

    old_headers.add("X-Old: one", &err);
    new_headers.add("X-New: two", &err);
    new_headers.add("X-New: three", &err);
    check("headers-built", old_headers.valid() && new_headers.valid());

    error_reset(&err);
    check("set-old", client.set_headers(&old_headers, &err) == STDLIB_OK &&
                     client.header_lines() == 1);

    /* Inject a failure: the previous list must survive, and libcurl must
     * still hold the old (still-allocated) list. */
    injected_header_failure = 1;
    error_reset(&err);
    check("set-new-fails", client.set_headers(&new_headers, &err) == STDLIB_FAIL &&
                          err.failed_now());
    check("old-retained", client.header_lines() == 1);
    injected_header_failure = 0;
    error_reset(&err);
    memset(body, 0, sizeof(body));
    rc = client.get(url, body, sizeof(body), &err);
    have_old = strstr(body, "X-Old: one") != (char *)0;
    have_new = strstr(body, "X-New:") != (char *)0;
    check("old-still-sent", rc == STDLIB_OK && err.ok() && have_old && !have_new);

    /* First set_headers fails: nothing is installed and the copy is freed. */
    injected_header_failure = 1;
    error_reset(&err);
    check("first-fails", fresh.set_headers(&old_headers, &err) == STDLIB_FAIL &&
                        fresh.header_lines() == 0);
    injected_header_failure = 0;

    /* A later successful replacement is visible to libcurl. */
    error_reset(&err);
    check("set-new", client.set_headers(&new_headers, &err) == STDLIB_OK &&
                     client.header_lines() == 2);
    error_reset(&err);
    memset(body, 0, sizeof(body));
    rc = client.get(url, body, sizeof(body), &err);
    have_old = strstr(body, "X-Old:") != (char *)0;
    have_new = strstr(body, "X-New: two") != (char *)0 &&
               strstr(body, "X-New: three") != (char *)0;
    check("new-sent", rc == STDLIB_OK && err.ok() && have_new && !have_old);

    kill(child, SIGKILL);
    waitpid(child, (int *)0, 0);

    client.drop();
    fresh.drop();
    old_headers.drop();
    new_headers.drop();
    global.drop();
    print("issue215-summary", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
