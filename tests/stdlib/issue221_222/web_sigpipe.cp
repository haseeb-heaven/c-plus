/* Issue #221 regression (web.hp): a remote client that disconnects before a
 * reply is written must not kill the server with SIGPIPE.  A forced RST makes
 * the very next send fail deterministically; the reply runs in a child with
 * SIGPIPE reset to its default disposition so a pre-fix crash is reported by
 * the harness instead of terminating it. */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/web.hp"

int main(void) {
    web_server srv;
    web_conn c;
    struct sockaddr_in addr;
    struct linger lng;
    socklen_t olen;
    int port;
    int client;
    int failures = 0;
    int opt;
    pid_t kid;
    int status;

    if (web_server_init(&srv, "127.0.0.1", 0) != 0) {
        print("web-sigpipe", 0, 0, 0);
        return 1;
    }
    olen = (socklen_t)sizeof(addr);
    if (getsockname(srv.listen_fd, (struct sockaddr *)&addr, &olen) != 0) {
        print("web-sigpipe", 0, 0, 0);
        return 1;
    }
    port = ntohs(addr.sin_port);
    client = socket(AF_INET, SOCK_STREAM, 0);
    if (client < 0) {
        print("web-sigpipe", 0, 0, 0);
        return 1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if (connect(client, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        send(client, "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n",
             strlen("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"), 0) < 0 ||
        web_accept(&srv, &c) != 1) {
        close(client);
        print("web-sigpipe", 0, 0, 0);
        return 1;
    }

    /* Client closes with a forced RST so the server's next send sees EPIPE. */
    lng.l_onoff = 1;
    lng.l_linger = 0;
    (void)setsockopt(client, SOL_SOCKET, SO_LINGER, &lng, sizeof(lng));
    close(client);

#ifdef SO_NOSIGPIPE
    opt = 0;
    olen = (socklen_t)sizeof(opt);
    if (getsockopt(c.fd, SOL_SOCKET, SO_NOSIGPIPE, &opt, &olen) != 0 || opt != 1)
        ++failures;
    opt = 0;
    olen = (socklen_t)sizeof(opt);
    if (getsockopt(srv.listen_fd, SOL_SOCKET, SO_NOSIGPIPE, &opt, &olen) != 0 ||
        opt != 1)
        ++failures;
#endif

    kid = fork();
    if (kid == 0) {
        signal(SIGPIPE, SIG_DFL);
        web_reply(&c, 200, "hello");
        _exit(0);
    }
    status = 0;
    if (kid < 0 || waitpid(kid, &status, 0) != kid) {
        ++failures;
    } else if (WIFSIGNALED(status)) {
        ++failures; /* SIGPIPE killed the child: the original bug */
    } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        ++failures;
    }

    close(c.fd);
    web_server_shutdown(&srv);
    print("web-sigpipe", failures, WIFSIGNALED(status) ? 1 : 0,
          (kid > 0 && WIFEXITED(status)) ? WEXITSTATUS(status) : -1);
    return failures == 0 ? 0 : 1;
}
