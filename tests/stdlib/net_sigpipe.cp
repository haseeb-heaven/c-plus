/* Issue #221 regression: connection::write() to a peer that has reset the
 * socket must return an I/O error, not kill the process with SIGPIPE.  The
 * test explicitly restores SIGPIPE's DEFAULT disposition; nothing installs
 * an ignore handler. */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/net.hp"

static char nsp_body[8192];

/* Connect to loopback:port, force an RST close, then tell the parent through
 * a pipe that the peer is gone. */
static pid_t nsp_reset_peer(int port, int ready_fd) {
    pid_t kid = fork();
    if (kid == 0) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in addr;
        struct linger lin;
        char go = 'G';
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons((unsigned short)port);
        lin.l_onoff = 1;
        lin.l_linger = 0;
        if (fd >= 0) {
            (void)setsockopt(fd, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
            if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0)
                (void)shutdown(fd, SHUT_RDWR);
            close(fd);
        }
        (void)write(ready_fd, &go, 1);
        _exit(0);
    }
    return kid;
}

static int nsp_wait_ready(int fd) {
    char go = 0;
    ssize_t n;
    do {
        n = read(fd, &go, 1);
    } while (n < 0 && errno == EINTR);
    return n == 1 && go == 'G';
}

int main(void) {
    error err = error();
    int ready[2];
    int net_result = 0;
    int net_attempts = 0;
    int net_ok = 0;

    (void)signal(SIGPIPE, SIG_DFL);
    memset(nsp_body, 'x', sizeof(nsp_body));

    if (pipe(ready) == 0) {
        listener srv = listener(0);
        int port = srv.bound_port();
        if (srv.is_valid() && port > 0) {
            pid_t kid = nsp_reset_peer(port, ready[1]);
            connection peer = srv.accept();
            if (peer.is_valid() && nsp_wait_ready(ready[0])) {
                struct timeval tv;
                tv.tv_sec = 2;
                tv.tv_usec = 0;
                (void)setsockopt(peer.descriptor(), SOL_SOCKET, SO_SNDTIMEO,
                                 &tv, sizeof(tv));
                /* Even if the first sends are buffered, the RST must surface
                 * as a returned error before the process dies. */
                usleep(20000);
                while (net_result == STDLIB_OK && net_attempts < 16) {
                    net_result = peer.write(nsp_body, sizeof(nsp_body), &err);
                    net_attempts++;
                }
                net_ok = net_result == STDLIB_FAIL && err.failed_now() != 0;
            }
            (void)waitpid(kid, NULL, 0);
        }
        close(ready[0]);
        close(ready[1]);
    }
    /* Reaching this line at all proves SIGPIPE did not kill the process. */
    print("net-write-survived", 1, net_ok, net_result, err.failed_now());
    return net_ok ? 0 : 1;
}
