/* Demo E: a minimal HTTP responder using the POSIX socket API.
 *
 * The demo forks its own client so it is self-contained: a real request is
 * accepted, parsed minimally, answered, and the connection is closed by the
 * RAII destructor. */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

resource struct Listener {
    int fd;
};

void Listener_drop(Listener *self) {
    if (self->fd >= 0) {
        close(self->fd);
        self->fd = -1;
    }
}

Listener Listener_open(int port) {
    Listener l;
    l.fd = -1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return l;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return l;
    }
    if (listen(fd, 1) != 0) {
        close(fd);
        return l;
    }
    l.fd = fd;
    return l;
}

resource struct Connection {
    int fd;
};

void Connection_drop(Connection *self) {
    if (self->fd >= 0) {
        close(self->fd);
        self->fd = -1;
    }
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    Listener server = Listener_open(0);
    if (server.fd < 0) {
        printf("cannot create listener\n");
        return 1;
    }

    struct sockaddr_in bound;
    socklen_t bound_len = sizeof(bound);
    getsockname(server.fd, (struct sockaddr *)&bound, &bound_len);
    int port = ntohs(bound.sin_port);
    printf("listening on 127.0.0.1:%d\n", port);

    pid_t client = fork();
    if (client == 0) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons((unsigned short)port);
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            const char *request = "GET / HTTP/1.0\r\nHost: localhost\r\n\r\n";
            (void)!write(fd, request, strlen(request));
            char reply[128];
            ssize_t n = read(fd, reply, sizeof(reply) - 1);
            if (n > 0) {
                reply[n] = '\0';
                char *nl = strchr(reply, '\n');
                if (nl)
                    *nl = '\0';
                dprintf(1, "client received: %s\n", reply);
            }
        }
        close(fd);
        _exit(0);
    }

    struct sockaddr_in peer;
    socklen_t peer_len = sizeof(peer);
    int fd = accept(server.fd, (struct sockaddr *)&peer, &peer_len);
    if (fd < 0) {
        printf("accept failed\n");
        return 1;
    }
    Connection conn;
    conn.fd = fd;

    char request[512];
    ssize_t n = read(conn.fd, request, sizeof(request) - 1);
    if (n > 0) {
        request[n] = '\0';
        char *nl = strchr(request, '\n');
        if (nl)
            *nl = '\0';
        printf("server received: %s\n", request);
    }

    const char *response =
        "HTTP/1.0 200 OK\r\n"
        "Content-Length: 2\r\n"
        "Connection: close\r\n\r\n"
        "ok";
    (void)!write(conn.fd, response, strlen(response));

    waitpid(client, NULL, 0);
    printf("server closing connection\n");
    Connection_drop(&conn);
    conn.fd = -1;
    return 0;
}
