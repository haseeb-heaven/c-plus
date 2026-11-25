/* Socket wrapper test: real loopback accept, borrowing, zero-init safety. */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/net.hp"

int main(void) {
    error err = error();
    listener server = listener(0);
    if (!server.is_valid()) {
        print("bind-failed");
        return 1;
    }
    print("bound", server.is_valid(), server.bound_port() > 0);

    int port = server.bound_port();
    pid_t child = fork();
    if (child == 0) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in peer;
        memset(&peer, 0, sizeof(peer));
        peer.sin_family = AF_INET;
        peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        peer.sin_port = htons((unsigned short)port);
        if (connect(fd, (struct sockaddr *)&peer, sizeof(peer)) == 0) {
            (void)!write(fd, "ping", 4);
            char reply[16];
            ssize_t got = read(fd, reply, sizeof(reply) - 1);
            if (got > 0) {
                reply[got] = '\0';
                if (strcmp(reply, "pong") == 0)
                    _exit(0);
            }
        }
        close(fd);
        _exit(1);
    }

    connection peer = server.accept();
    if (!peer.is_valid()) {
        waitpid(child, NULL, 0);
        print("accept-failed");
        return 2;
    }
    char buf[16];
    int got = peer.read(buf, sizeof(buf) - 1, &err);
    if (got > 0)
        buf[got] = '\0';
    int wrote = peer.write("pong", 4, &err);
    waitpid(child, NULL, 0);
    print("echo", buf, got, wrote == STDLIB_OK, peer.descriptor() > 2,
          server.descriptor() > 2);

    /* Zero-initialised handles must never close anything. */
    listener zero_listener;
    connection zero_connection;
    print("zero-init", zero_listener.is_valid(), zero_connection.is_valid());

    listener borrowed = listener_borrow(server.descriptor());
    connection borrowed_conn = connection_borrow(peer.descriptor());
    print("borrowed", borrowed.is_valid(), borrowed_conn.is_valid());

    listener bad = listener(1);
    print("privileged", bad.is_valid(), err.failed_now());
    return 0;
}
