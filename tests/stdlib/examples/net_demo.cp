/* Demo: RAII sockets - bind, accept a forked client, borrow without owning. */
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
        print("cannot bind");
        return 1;
    }
    int port = server.bound_port();
    print("listening", port);

    pid_t child = fork();
    if (child == 0) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in peer;
        memset(&peer, 0, sizeof(peer));
        peer.sin_family = AF_INET;
        peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        peer.sin_port = htons((unsigned short)port);
        if (connect(fd, (struct sockaddr *)&peer, sizeof(peer)) == 0)
            (void)!write(fd, "demo-request\n", 13);
        close(fd);
        _exit(0);
    }

    connection conn = server.accept();
    if (!conn.is_valid()) {
        waitpid(child, NULL, 0);
        return 2;
    }
    char request[64];
    int got = conn.read(request, sizeof(request) - 1, &err);
    if (got > 0) {
        request[got] = '\0';
        char *newline = strchr(request, '\n');
        if (newline != NULL)
            *newline = '\0';
        print("server got", request);
    }
    conn.write("ok\n", 3, &err);
    waitpid(child, NULL, 0);

    listener borrowed = listener_borrow(server.descriptor());
    print("owner still valid", server.is_valid(), borrowed.is_valid());
    return 0;
}
