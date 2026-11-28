/* Issue #221 regression (net.hp): a peer that disconnects while a connection
 * is being written to must surface EPIPE as a recoverable STDLIB_FAIL, not
 * kill the process with SIGPIPE.  The write happens in a forked child with
 * SIGPIPE reset to its default disposition, so the pre-fix process death is
 * reported as a failed test instead of taking down the harness. */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/net.hp"

int main(void) {
    int sp[2];
    struct linger lng;
    pid_t kid;
    int status;
    int failures = 0;
    int opt;
    socklen_t olen;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0) {
        print("net-sigpipe", 0, 0, 0);
        return 1;
    }

    /* Both borrowed and accepted descriptors must carry the per-socket
     * suppression option where the platform provides it. */
#ifdef SO_NOSIGPIPE
    {
        connection probe = connection_borrow(sp[0]);
        opt = 0;
        olen = (socklen_t)sizeof(opt);
        if (getsockopt(sp[0], SOL_SOCKET, SO_NOSIGPIPE, &opt, &olen) != 0 ||
            opt != 1)
            ++failures;
    }
#endif

    /* Force an RST and close the peer before forking so the child's very
     * next write deterministically sees EPIPE instead of racing the parent. */
    lng.l_onoff = 1;
    lng.l_linger = 0;
    (void)setsockopt(sp[1], SOL_SOCKET, SO_LINGER, &lng, sizeof(lng));
    close(sp[1]);

    kid = fork();
    if (kid == 0) {
        error err = error();
        int write_rc;
        signal(SIGPIPE, SIG_DFL);
        {
            connection peer = connection_borrow(sp[0]);
            write_rc = peer.write("x", 1, &err);
        }
        close(sp[0]);
        _exit(write_rc == STDLIB_FAIL && err.category() == STDLIB_CATEGORY_IO
                  ? 0 : 42);
    }
    close(sp[0]);
    status = 0;
    if (kid < 0 || waitpid(kid, &status, 0) != kid) {
        ++failures;
    } else if (WIFSIGNALED(status)) {
        ++failures; /* SIGPIPE killed the child: the original bug */
    } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        ++failures;
    }

    print("net-sigpipe", failures, WIFSIGNALED(status) ? 1 : 0,
          (kid > 0 && WIFEXITED(status)) ? WEXITSTATUS(status) : -1);
    return failures == 0 ? 0 : 1;
}
