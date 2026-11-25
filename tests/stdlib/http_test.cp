/* HTTP wrapper test: real GET/POST against a forked local server, file://,
 * header propagation and failure reporting.  Fully offline. */
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

static int serve_once(int fd) {
    char request[2048];
    char reply[2560];
    ssize_t n = read(fd, request, sizeof(request) - 1);
    if (n <= 0)
        return 1;
    request[n] = '\0';
    snprintf(reply, sizeof(reply),
             "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "
             "%d\r\nConnection: close\r\n\r\n%s",
             (int)strlen(request), request);
    (void)!write(fd, reply, strlen(reply));
    close(fd);
    return 0;
}

int main(void) {
    error err = error();
    http global = http(&err);
    if (!global.valid())
        return 1;
    print("curl", http_version() != NULL);

    http_headers headers = http_headers();
    headers.add("X-Cplus: yes", &err);
    print("headers", headers.valid(), headers.native() != NULL);

    int listener = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) != 0)
        return 2;
    if (listen(listener, 4) != 0)
        return 3;
    if (getsockname(listener, (struct sockaddr *)&addr, &addr_len) != 0)
        return 4;
    int port = ntohs(addr.sin_port);

    pid_t child = fork();
    if (child == 0) {
        int i;
        for (i = 0; i < 2; i++) {
            int fd = accept(listener, NULL, NULL);
            if (fd < 0)
                _exit(1);
            serve_once(fd);
        }
        _exit(0);
    }

    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/hello", port);

    char body[4096];
    http_client client = http_client(&err);
    if (!client.valid())
        return 5;
    client.set_timeout(10, &err);
    client.follow_redirects(1, &err);
    if (client.set_headers(&headers, &err) != STDLIB_OK)
        return 6;

    int got = client.get(url, body, sizeof(body), &err);
    print("get", got == STDLIB_OK, client.status(), client.bytes() > 0,
          strstr(body, "X-Cplus: yes") != NULL,
          strstr(body, "GET /hello") != NULL);

    int posted = client.post(url, "payload=42", body, sizeof(body), &err);
    print("post", posted == STDLIB_OK, client.status(),
          strstr(body, "payload=42") != NULL, strstr(body, "POST ") != NULL);

    waitpid(child, NULL, 0);
    close(listener);

    http_client plain = http_client(&err);
    int file_ok = plain.get("file://tests/stdlib/data/sample.json", body,
                            sizeof(body), &err);
    print("file", file_ok == STDLIB_OK, strstr(body, "cplus") != NULL,
          plain.valid());

    int refused = plain.get("http://127.0.0.1:1/nope", body, sizeof(body), &err);
    char why[64];
    snprintf(why, sizeof(why), "%s", err.what());
    print("refused", refused == STDLIB_FAIL, err.failed_now(), why[0] != '\0');

    char tiny[8];
    int bounded = plain.get("file://tests/stdlib/data/sample.json", tiny,
                            sizeof(tiny), &err);
    print("bounded", bounded == STDLIB_FAIL, err.failed_now(), plain.status());
    return 0;
}
