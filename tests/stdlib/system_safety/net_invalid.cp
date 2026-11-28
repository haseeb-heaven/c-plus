/* Network safety: invalid ports/addresses, failed-socket cleanup, borrowed
 * descriptors and idempotent double/triple drop.
 *
 * SELF-CHECKING: each check prints "<tag> <1|0>" (1 = passed).  The final line
 * is "NET_SAFETY <checks> <failures>".  Exit 0 only when failures == 0.
 *
 * net.hp exposes no address-string API (listener(port) always binds
 * INADDR_LOOPBACK), so the invalid-address case is a documented SKIP rather
 * than a fabricated call.  All waits are bounded: a 25 s SIGALRM watchdog in
 * the parent and an external `timeout -k 5 30`.  No external servers are used.
 *
 * Every owned handle has a unique name (C+ RAII tracks owned locals by name).
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/net.hp"

static int nsiv_checks = 0;
static int nsiv_failures = 0;

static void nsiv_check(const char *tag, int cond) {
    nsiv_checks++;
    if (!cond)
        nsiv_failures++;
    print(tag, cond ? 1 : 0);
    (void)!fflush(stdout);
}

static void nsiv_skip(const char *tag, const char *reason) {
    nsiv_checks++;
    print(tag, 1);
    print("SKIP_REASON", tag, reason);
    (void)!fflush(stdout);
}

static void nsiv_watchdog(int sig) {
    static const char msg[] = "FATAL net_invalid watchdog timeout\n";
    (void)sig;
    (void)!write(2, msg, sizeof(msg) - 1);
    _exit(90);
}

static int nsiv_fd_count(void) {
    DIR *d = opendir("/dev/fd");
    struct dirent *e;
    int n = 0;
    if (d == (DIR *)0)
        return -1;
    while ((e = readdir(d)) != (struct dirent *)0) {
        const char *s = e->d_name;
        if (s[0] >= '0' && s[0] <= '9')
            n++;
    }
    closedir(d);
    return n;
}

int main(void) {
    error nsiv_err = error();
    int nsiv_baseline = nsiv_fd_count();

    (void)!signal(SIGALRM, nsiv_watchdog);
    (void)!alarm(25);

    /* ---- invalid ports: inert handle, no descriptor, no leak ------------- */
    {
        listener nsiv_neg = listener(-1);
        listener nsiv_high = listener(70000);
        listener nsiv_overflow = listener(65536);
        listener nsiv_min = listener(INT_MIN);
        listener nsiv_good = listener(0);
        int ok = 1;
        ok = ok && nsiv_neg.descriptor() == -1 && !nsiv_neg.is_valid();
        ok = ok && nsiv_high.descriptor() == -1 && !nsiv_high.is_valid();
        ok = ok && nsiv_overflow.descriptor() == -1 && !nsiv_overflow.is_valid();
        ok = ok && nsiv_min.descriptor() == -1 && !nsiv_min.is_valid();
        ok = ok && nsiv_good.is_valid() && nsiv_good.bound_port() > 0;
        nsiv_good.drop();
        nsiv_min.drop();
        nsiv_overflow.drop();
        nsiv_high.drop();
        nsiv_neg.drop();
        nsiv_check("net_invalid_port_range", ok);
    }

    /* ---- invalid address: no API accepts one ----------------------------- */
    nsiv_skip("net_invalid_address",
              "stdlib/net.hp listener(port) accepts only an integer port and "
              "always binds INADDR_LOOPBACK; connection has no address argument. "
              "Strings such as \"not.an.address\" or \"\" cannot be passed to "
              "any net.hp API, so no invalid-address case exists to exercise.");

    /* ---- invalid connection: rejection with CATEGORY_INVALID ------------- */
    {
        char nsiv_buf[16];
        connection nsiv_bad = connection(-1);
        int ok;
        (void)!memset(nsiv_buf, 0, sizeof(nsiv_buf));
        nsiv_err.reset();
        ok = nsiv_bad.is_valid() == 0 && nsiv_bad.descriptor() == -1;
        ok = ok && nsiv_bad.read(nsiv_buf, sizeof(nsiv_buf), &nsiv_err) ==
                       STDLIB_FAIL;
        ok = ok && nsiv_err.failed_now() == 1 &&
             nsiv_err.category() == STDLIB_CATEGORY_INVALID;
        nsiv_err.reset();
        ok = ok && nsiv_bad.write("x", 1, &nsiv_err) == STDLIB_FAIL;
        ok = ok && nsiv_err.failed_now() == 1 &&
             nsiv_err.category() == STDLIB_CATEGORY_INVALID;
        /* NULL buffer, NULL data and zero capacity are rejected too, using a
         * borrowed live descriptor so the validity gate is not the cause. */
        {
            int nsiv_pair[2];
            if (socketpair(AF_UNIX, SOCK_STREAM, 0, nsiv_pair) == 0) {
                connection nsiv_live = connection_borrow(nsiv_pair[0]);
                nsiv_err.reset();
                ok = ok && nsiv_live.is_valid() == 1;
                ok = ok && nsiv_live.read((char *)0, 8, &nsiv_err) ==
                               STDLIB_FAIL &&
                     nsiv_err.category() == STDLIB_CATEGORY_INVALID;
                nsiv_err.reset();
                ok = ok && nsiv_live.read(nsiv_buf, 0, &nsiv_err) ==
                               STDLIB_FAIL &&
                     nsiv_err.category() == STDLIB_CATEGORY_INVALID;
                nsiv_err.reset();
                ok = ok && nsiv_live.write((const char *)0, 1, &nsiv_err) ==
                               STDLIB_FAIL &&
                     nsiv_err.category() == STDLIB_CATEGORY_INVALID;
                nsiv_live.drop(); /* borrowed: must not close nsiv_pair[0] */
                ok = ok && fcntl(nsiv_pair[0], F_GETFD) != -1;
                close(nsiv_pair[0]);
                close(nsiv_pair[1]);
            } else {
                ok = 0;
            }
        }
        nsiv_bad.drop();
        nsiv_bad.drop();
        ok = ok && nsiv_bad.descriptor() == -1;
        nsiv_check("net_invalid_connection_io", ok);
    }

    /* ---- borrowed fd 0/1/2 never validated or closed --------------------- */
    {
        listener nsiv_bl0 = listener_borrow(0);
        listener nsiv_bl1 = listener_borrow(1);
        connection nsiv_bc2 = connection_borrow(2);
        int ok = !nsiv_bl0.is_valid() && !nsiv_bl1.is_valid() &&
                 !nsiv_bc2.is_valid();
        nsiv_bl0.drop();
        nsiv_bl1.drop();
        nsiv_bc2.drop();
        ok = ok && fcntl(0, F_GETFD) != -1 && fcntl(1, F_GETFD) != -1 &&
             fcntl(2, F_GETFD) != -1;
        nsiv_check("net_borrowed_std_fds", ok);
    }

    /* ---- owning handle: triple drop closes exactly once ------------------ */
    {
        listener nsiv_srv = listener(0);
        int nsiv_port = nsiv_srv.bound_port();
        int nsiv_peer_fd = -1;
        int nsiv_owned_fd = -1;
        int ok = nsiv_srv.is_valid() && nsiv_port > 0;
        if (ok) {
            struct sockaddr_in a;
            memset(&a, 0, sizeof(a));
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            a.sin_port = htons((unsigned short)nsiv_port);
            nsiv_peer_fd = socket(AF_INET, SOCK_STREAM, 0);
            if (nsiv_peer_fd >= 0 &&
                connect(nsiv_peer_fd, (struct sockaddr *)&a, sizeof(a)) == 0) {
                connection nsiv_owned = nsiv_srv.accept();
                ok = ok && nsiv_owned.is_valid();
                if (ok) {
                    nsiv_owned_fd = nsiv_owned.descriptor();
                    nsiv_owned.drop();
                    nsiv_owned.drop();
                    nsiv_owned.drop();
                    ok = ok && nsiv_owned.descriptor() == -1 &&
                         !nsiv_owned.is_valid();
                }
            } else {
                ok = 0;
            }
        }
        nsiv_srv.drop();
        nsiv_srv.drop();
        nsiv_srv.drop();
        ok = ok && nsiv_srv.descriptor() == -1 && !nsiv_srv.is_valid();
        /* A closed descriptor must now be invalid (EBADF). */
        if (nsiv_owned_fd >= 0)
            ok = ok && fcntl(nsiv_owned_fd, F_GETFD) == -1;
        if (nsiv_peer_fd >= 0)
            close(nsiv_peer_fd);
        nsiv_check("net_double_drop_owning", ok);
    }

    /* ---- descriptor balance ---------------------------------------------- */
    if (nsiv_baseline < 0 || nsiv_fd_count() < 0)
        nsiv_skip("net_invalid_no_fd_leak",
                  "opendir(/dev/fd) unavailable; descriptor balance not checked");
    else
        nsiv_check("net_invalid_no_fd_leak", nsiv_fd_count() == nsiv_baseline);

    (void)!alarm(0);
    print("NET_SAFETY", nsiv_checks, nsiv_failures);
    return nsiv_failures == 0 ? 0 : 1;
}
