/* Resource-exhaustion resilience: socket exhaustion with controlled peers.
 *
 * Part A lowers soft RLIMIT_NOFILE in a forked child (baseline + 16) and opens
 * AF_UNIX SOCK_STREAM socketpairs in a bounded loop of at most 24.  Both ends
 * are owned by the child, so there are no external peers and no listen/accept
 * waits.  The refusal must be EMFILE/ENFILE, all descriptors are closed, and a
 * fresh socketpair must succeed afterwards.  If the limit cannot be lowered the
 * child exits with the SKIP code.
 *
 * Part B uses the stdlib net wrappers over loopback TCP with a single local
 * client whose connect() is non-blocking and polled (2 s).  SO_RCVTIMEO bounds
 * the read.  It runs in its own watched child so a blocking accept() can never
 * stall the suite.  Exit codes: 0 = ok, 1 = failed check, 2 = SKIP, 3 = setup.
 *
 * Self-checking: "<tag> <1|0>" per check, final
 * "RESOURCE_EXHAUSTION <checks> <failures>". Exit 0 only when failures == 0.
 */
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/net.hp"
#include "tests/security/resource_exhaustion/support/re_harness.hp"
#include "tests/security/resource_exhaustion/support/re_limits.hp"
#include "tests/security/resource_exhaustion/support/re_watchdog.hp"

#define RE_SE_MAX_PAIRS 24
#define RE_SE_EXIT_SKIP 2
#define RE_SE_EXIT_SETUP 3

static int re_se_child_failures = 0;

static void re_se_silent(int cond) {
    if (!cond)
        re_se_child_failures++;
}

static int re_se_nonblock(int fd) {
    int re_se_flags = fcntl(fd, F_GETFL, 0);
    if (re_se_flags < 0)
        return 0;
    return fcntl(fd, F_SETFL, re_se_flags | O_NONBLOCK) == 0 ? 1 : 0;
}

static void re_se_exhaust_child(void) {
    struct rlimit re_se_saved;
    int re_se_baseline = re_fd_count();
    int re_se_left[RE_SE_MAX_PAIRS];
    int re_se_right[RE_SE_MAX_PAIRS];
    int re_se_pairs = 0;
    int re_se_pair[2];
    int re_se_errno = 0;
    int re_se_refused = 0;
    int re_se_i;

    if (re_se_baseline <= 0)
        re_child_exit(RE_SE_EXIT_SETUP);
    if (!re_limit_supported(RLIMIT_NOFILE) ||
        !re_limit_lower(RLIMIT_NOFILE, (rlim_t)(re_se_baseline + 16),
                        &re_se_saved))
        re_child_exit(RE_SE_EXIT_SKIP);

    for (re_se_i = 0; re_se_i < RE_SE_MAX_PAIRS; re_se_i++) {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, re_se_pair) == 0) {
            re_se_left[re_se_pairs] = re_se_pair[0];
            re_se_right[re_se_pairs] = re_se_pair[1];
            re_se_pairs++;
        } else {
            re_se_refused = 1;
            re_se_errno = errno;
            break;
        }
    }

    re_se_silent(re_se_pairs > 0);
    re_se_silent(re_se_refused == 1);
    re_se_silent(re_se_errno == EMFILE || re_se_errno == ENFILE);
    re_se_silent(re_se_pairs < RE_SE_MAX_PAIRS);

    for (re_se_i = 0; re_se_i < re_se_pairs; re_se_i++) {
        (void)!close(re_se_left[re_se_i]);
        (void)!close(re_se_right[re_se_i]);
    }
    re_se_silent(socketpair(AF_UNIX, SOCK_STREAM, 0, re_se_pair) == 0);
    if (re_se_pair[0] >= 0) {
        (void)!close(re_se_pair[0]);
        (void)!close(re_se_pair[1]);
    }
    re_se_silent(re_fd_count() == re_se_baseline);

    re_limit_restore(RLIMIT_NOFILE, &re_se_saved);
    re_child_exit(re_se_child_failures == 0 ? 0 : 1);
}

static void re_se_loopback_child(void) {
    error re_se_err = error();
    listener re_se_server = listener(0);
    struct sockaddr_in re_se_addr;
    struct pollfd re_se_poll;
    struct timeval re_se_tv;
    int re_se_port = re_se_server.bound_port();
    int re_se_peer = -1;
    int re_se_poll_rc;
    char re_se_buf[4];
    int re_se_read_rc;

    re_se_tv.tv_sec = 2;
    re_se_tv.tv_usec = 0;
    re_se_silent(re_se_server.is_valid() == 1 && re_se_port > 0);

    if (re_se_port > 0) {
        re_se_peer = socket(AF_INET, SOCK_STREAM, 0);
        if (re_se_peer >= 0 && re_se_nonblock(re_se_peer)) {
            memset(&re_se_addr, 0, sizeof(re_se_addr));
            re_se_addr.sin_family = AF_INET;
            re_se_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            re_se_addr.sin_port = htons((unsigned short)re_se_port);
            (void)!connect(re_se_peer, (struct sockaddr *)&re_se_addr,
                           sizeof(re_se_addr));
            re_se_poll.fd = re_se_peer;
            re_se_poll.events = POLLOUT;
            re_se_poll.revents = 0;
            do {
                re_se_poll_rc = poll(&re_se_poll, 1, 2000);
            } while (re_se_poll_rc < 0 && errno == EINTR);
            re_se_silent(re_se_poll_rc > 0);
        } else {
            re_se_silent(0);
        }
    }

    if (re_se_peer >= 0) {
        connection re_se_conn = re_se_server.accept();
        re_se_silent(re_se_conn.is_valid() == 1);
        if (re_se_conn.is_valid()) {
            (void)!setsockopt(re_se_conn.descriptor(), SOL_SOCKET, SO_RCVTIMEO,
                              &re_se_tv, sizeof(re_se_tv));
            memset(re_se_buf, 0, sizeof(re_se_buf));
            (void)!write(re_se_peer, "PING", 4);
            re_se_read_rc = re_se_conn.read(re_se_buf, (size_t)sizeof(re_se_buf),
                                            &re_se_err);
            re_se_silent(re_se_read_rc == 4);
            re_se_silent(__builtin_memcmp(re_se_buf, "PING", 4) == 0);
            re_se_silent(re_se_conn.write("Q", 1, &re_se_err) == STDLIB_OK);
            re_se_conn.drop();
        }
        (void)!close(re_se_peer);
    }
    re_se_server.drop();
    re_child_exit(re_se_child_failures == 0 ? 0 : 1);
}

int main(void) {
    struct re_watchdog_result re_se_wd;

    (void)!fflush(stdout);
    re_se_wd = re_run_watchdog(re_se_exhaust_child, 6, 2);
    re_check("re_se_exhaust_reaped", re_se_wd.reaped == 1);
    re_check("re_se_exhaust_no_watchdog", re_se_wd.timed_out == 0);
    if (!re_se_wd.exited) {
        re_check("re_se_exhaust_exited", 0);
    } else if (re_se_wd.exit_code == RE_SE_EXIT_SKIP) {
        re_skip("re_se_socket_exhaust_injection",
                "RLIMIT_NOFILE lowering is unsupported on this host");
    } else {
        re_check("re_se_exhaust_exited", re_se_wd.exit_code == 0 ||
                                        re_se_wd.exit_code == RE_SE_EXIT_SETUP);
        re_check("re_se_socket_exhaust", re_se_wd.exit_code == 0);
    }

    (void)!fflush(stdout);
    re_se_wd = re_run_watchdog(re_se_loopback_child, 6, 2);
    re_check("re_se_loopback_reaped", re_se_wd.reaped == 1);
    re_check("re_se_loopback_no_watchdog", re_se_wd.timed_out == 0);
    re_check("re_se_loopback_exited", re_se_wd.exited == 1 &&
                                     re_se_wd.exit_code == 0);

    re_report();
    return re_harness_failures == 0 ? 0 : 1;
}
