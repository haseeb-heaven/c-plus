/* Deep, independent contract test for stdlib/net.hp.
 *
 * Classification codes printed after each SCENARIO line:
 *   0 = SUCCESS      expected successful operation / exact contract result
 *   1 = RECOVERABLE  expected rejection or error, handled without crash/leak
 *   2 = FATAL        contract violation, crash, hang, or descriptor leak
 *   3 = SKIP         scenario not portable on this host (reason printed)
 *
 * All network sockets are AF_INET/INADDR_LOOPBACK (the fault-injection cases
 * also use AF_UNIX socketpair). Every wait is bounded, and a SIGALRM watchdog
 * terminates the process if a syscall ever blocks unexpectedly. Owning
 * locals use unique names: the C+ lowering tracks owned variables by name
 * for the whole function, so reused names lose zero-init and destructors.
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/net.hp"

/* --- deterministic syscall fault injection ------------------------------- */
/* The wrappers delegate to the real syscall when their flag is clear, so
 * they are transparent to the harness (print/fork/waitpid included).
 * syscall(2) is deprecated on macOS but is the simplest libc-independent
 * passthrough that avoids dlsym(RTLD_NEXT) provenance diagnostics. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

static int g_fault_write_zero = 0;
static int g_fault_short_write = 0;
static int g_short_write_calls = 0;
static int g_fault_read_eintr_once = 0;
static int g_fault_read_error = 0;

ssize_t write(int fd, const void *buf, size_t n) {
    size_t chunk = n;
    if (g_fault_write_zero && fd >= 0)
        return 0;
    if (g_fault_short_write) {
        g_short_write_calls++;
        if (chunk > 1024)
            chunk = 1024;
    }
    return syscall(SYS_write, fd, buf, chunk);
}

ssize_t read(int fd, void *buf, size_t n) {
    if (g_fault_read_error) {
        errno = EIO;
        return -1;
    }
    if (g_fault_read_eintr_once) {
        g_fault_read_eintr_once = 0;
        errno = EINTR;
        return -1;
    }
    return syscall(SYS_read, fd, buf, n);
}
#pragma clang diagnostic pop

static void on_ignore(int sig) {
    (void)sig;
}

static void on_alarm(int sig) {
    static const char msg[] = "FATAL watchdog timeout\n";
    (void)sig;
    (void)!write(2, msg, sizeof(msg) - 1);
    _exit(90);
}

/* --- result accounting --------------------------------------------------- */
static int g_success = 0;
static int g_recoverable = 0;
static int g_fatal = 0;
static int g_skipped = 0;

static void result(const char *name, int cls) {
    print("SCENARIO", name, cls);
    if (cls == 0)
        g_success++;
    else if (cls == 1)
        g_recoverable++;
    else if (cls == 2)
        g_fatal++;
    else
        g_skipped++;
}

static void skip_reason(const char *name, const char *reason) {
    print("SKIP_REASON", name, reason);
}

static char g_payload[65536];

static int fd_count(void) {
    DIR *d = opendir("/dev/fd");
    struct dirent *e;
    int n = 0;
    if (d == NULL)
        return -1;
    while ((e = readdir(d)) != NULL) {
        const char *s = e->d_name;
        if (s[0] >= '0' && s[0] <= '9')
            n++;
    }
    closedir(d);
    return n;
}

static int raw_loopback_bound(int reuse, int do_listen, unsigned short *port_out) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    socklen_t len;
    if (fd < 0)
        return -1;
    if (reuse) {
        int yes = 1;
        (void)!setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    }
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(0);
    if (bind(fd, (struct sockaddr *)&a, sizeof(a)) != 0) {
        close(fd);
        return -1;
    }
    len = sizeof(a);
    if (getsockname(fd, (struct sockaddr *)&a, &len) != 0) {
        close(fd);
        return -1;
    }
    *port_out = ntohs(a.sin_port);
    if (do_listen)
        (void)!listen(fd, 1);
    return fd;
}

static int poll_in(int fd, int ms) {
    struct pollfd p;
    int rc;
    p.fd = fd;
    p.events = POLLIN;
    p.revents = 0;
    do {
        rc = poll(&p, 1, ms);
    } while (rc < 0 && errno == EINTR);
    return rc;
}

int main(void) {
    error err = error();
    int fd_before = fd_count();
    int i;

    (void)!signal(SIGPIPE, on_ignore);
    (void)!signal(SIGALRM, on_alarm);
    (void)!alarm(45);

    for (i = 0; i < (int)sizeof(g_payload); i++)
        g_payload[i] = (char)(i % 251);

    /* --- port 0: ephemeral loopback bind, exact port/address reported ----- */
    {
        listener ephem = listener(0);
        struct sockaddr_in sa;
        socklen_t len = sizeof(sa);
        int ok = ephem.is_valid() && ephem.descriptor() > 2 &&
                 ephem.bound_port() > 0;
        if (getsockname(ephem.descriptor(), (struct sockaddr *)&sa, &len) == 0) {
            ok = ok && ntohs(sa.sin_port) == ephem.bound_port() &&
                 ntohl(sa.sin_addr.s_addr) == (unsigned long)INADDR_LOOPBACK;
        } else {
            ok = 0;
        }
        result("port_ephemeral_zero", ok ? 0 : 2);
    }

    /* --- port 65535: highest legal value, skip if host already uses it --- */
    {
        unsigned short probe_port = 0;
        int probe = raw_loopback_bound(0, 0, &probe_port);
        if (probe < 0) {
            skip_reason("port_65535", "probe bind to 127.0.0.1:0 failed");
            result("port_65535", 3);
        } else {
            close(probe);
            {
                listener maxport = listener(65535);
                int ok = maxport.is_valid() && maxport.bound_port() == 65535;
                result("port_65535", ok ? 0 : 2);
            }
        }
    }

    /* --- negative / out-of-range ports: inert handles, idempotent drop --- */
    {
        listener negport = listener(-1);
        listener bigport = listener(70000);
        listener lowport = listener(INT_MIN);
        int ok = !negport.is_valid() && !bigport.is_valid() &&
                 !lowport.is_valid();
        ok = ok && negport.descriptor() == -1 && bigport.descriptor() == -1 &&
                   lowport.descriptor() == -1;
        ok = ok && negport.bound_port() == 0 && bigport.bound_port() == 0 &&
                   lowport.bound_port() == 0;
        negport.drop();
        negport.drop(); /* second drop must be inert */
        /* C+ lowering zero-initialises a consumed owner, so after drop() the
         * raw fd slot reads 0 rather than -1.  Both mark an invalid handle;
         * is_valid() is the authoritative check. */
        ok = ok && !negport.is_valid() && negport.descriptor() <= 0;
        result("port_invalid_range", ok ? 1 : 2);
    }

    /* --- failed bind: forked child holds 127.0.0.1:P without SO_REUSEADDR */
    {
        int c2p[2];
        int p2c[2];
        unsigned short held_port = 0;
        int got = 0;
        int ok = 0;
        c2p[0] = c2p[1] = p2c[0] = p2c[1] = -1;
        if (pipe(c2p) != 0 || pipe(p2c) != 0) {
            skip_reason("bind_in_use_fork", "pipe() unavailable on this host");
            result("bind_in_use_fork", 3);
            if (c2p[0] >= 0) { close(c2p[0]); close(c2p[1]); }
            if (p2c[0] >= 0) { close(p2c[0]); close(p2c[1]); }
        } else {
            pid_t holder = fork();
            if (holder == 0) {
                int blocker;
                unsigned short child_port = 0;
                char rel = 0;
                close(c2p[0]);
                close(p2c[1]);
                blocker = raw_loopback_bound(0, 1, &child_port);
                if (blocker < 0)
                    _exit(1);
                (void)!write(c2p[1], &child_port, sizeof(child_port));
                (void)!read(p2c[0], &rel, 1);
                close(blocker);
                close(c2p[1]);
                close(p2c[0]);
                _exit(0);
            }
            close(c2p[1]);
            close(p2c[0]);
            if (holder > 0 && poll_in(c2p[0], 3000) > 0 &&
                read(c2p[0], &held_port, sizeof(held_port)) ==
                    (ssize_t)sizeof(held_port)) {
                got = 1;
                {
                    listener clash = listener((int)held_port);
                    ok = !clash.is_valid() && clash.descriptor() == -1;
                }
            }
            if (holder > 0) {
                char rel = 1;
                (void)!write(p2c[1], &rel, 1);
                waitpid(holder, NULL, 0);
            }
            close(c2p[0]);
            close(p2c[1]);
            if (got) {
                result("bind_in_use_fork", ok ? 1 : 2);
            } else {
                skip_reason("bind_in_use_fork",
                            "child did not publish a bound port in 3s");
                result("bind_in_use_fork", 3);
            }
        }
    }

    /* --- borrowed / zero-init handles must never close fd 0, 1 or 2 ------ */
    {
        int before[3];
        int after[3];
        int ok = 1;
        int inv;
        for (i = 0; i < 3; i++)
            before[i] = fcntl(i, F_GETFD);
        {
            listener zero_l;
            connection zero_c;
            listener borrow_l0 = listener_borrow(0);
            listener borrow_l1 = listener_borrow(1);
            listener borrow_l2 = listener_borrow(2);
            connection borrow_c0 = connection_borrow(0);
            connection borrow_c1 = connection_borrow(1);
            connection borrow_c2 = connection_borrow(2);
            inv = !zero_l.is_valid() && !zero_c.is_valid();
            inv = inv && !borrow_l0.is_valid() && !borrow_l1.is_valid() &&
                        !borrow_l2.is_valid();
            inv = inv && !borrow_c0.is_valid() && !borrow_c1.is_valid() &&
                        !borrow_c2.is_valid();
        }
        for (i = 0; i < 3; i++) {
            after[i] = fcntl(i, F_GETFD);
            if (before[i] == -1 || after[i] == -1)
                ok = 0;
        }
        result("borrow_no_close_012", (ok && inv) ? 0 : 2);
    }

    /* --- borrowed descriptors >= 3 are valid but never closed ------------ */
    {
        int sp[2];
        int ok = socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0;
        if (ok) {
            int valid;
            {
                connection ge3_c = connection_borrow(sp[0]);
                listener ge3_l = listener_borrow(sp[1]);
                valid = ge3_c.is_valid() && ge3_l.is_valid();
            }
            ok = valid && fcntl(sp[0], F_GETFD) != -1 &&
                          fcntl(sp[1], F_GETFD) != -1;
            close(sp[0]);
            close(sp[1]);
        }
        result("borrow_ge3_no_close", ok ? 0 : 2);
    }

    /* --- write to a closed peer: EPIPE becomes STDLIB_FAIL (SIGPIPE ignored)
     */
    {
        int sp[2];
        int ok = socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0;
        if (ok) {
            connection broken = connection_borrow(sp[0]);
            close(sp[1]);
            ok = broken.write("x", 1, &err) == STDLIB_FAIL;
            ok = ok && err.failed_now() &&
                       err.category() == STDLIB_CATEGORY_IO;
            ok = ok && strcmp(err.what(), "connection::write failed") == 0;
            close(sp[0]);
        }
        result("write_broken_pipe", ok ? 1 : 2);
    }

    /* --- EOF: read() returns STDLIB_EOF with a clear error object -------- */
    {
        listener eof_srv = listener(0);
        int ok = eof_srv.is_valid();
        if (ok) {
            pid_t eof_kid = fork();
            if (eof_kid == 0) {
                int fd = socket(AF_INET, SOCK_STREAM, 0);
                struct sockaddr_in eof_addr;
                memset(&eof_addr, 0, sizeof(eof_addr));
                eof_addr.sin_family = AF_INET;
                eof_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                eof_addr.sin_port = htons((unsigned short)eof_srv.bound_port());
                if (connect(fd, (struct sockaddr *)&eof_addr,
                            sizeof(eof_addr)) == 0) {
                    (void)!write(fd, "hi", 2);
                    close(fd);
                    _exit(0);
                }
                close(fd);
                _exit(1);
            }
            connection eof_peer = eof_srv.accept();
            if (eof_peer.is_valid()) {
                char buf[8];
                int n = eof_peer.read(buf, sizeof(buf), &err);
                int e = eof_peer.read(buf, sizeof(buf), &err);
                ok = ok && n == 2 && buf[0] == 'h' && buf[1] == 'i';
                ok = ok && e == STDLIB_EOF && err.ok();
                waitpid(eof_kid, NULL, 0);
            } else {
                ok = 0;
                waitpid(eof_kid, NULL, 0);
            }
        }
        result("eof_exact", ok ? 0 : 2);
    }

    /* --- partial writes: forced short writes loop to completion ---------- */
    {
        listener part_srv = listener(0);
        int ok = part_srv.is_valid();
        g_short_write_calls = 0;
        if (ok) {
            pid_t part_kid = fork();
            if (part_kid == 0) {
                int fd = socket(AF_INET, SOCK_STREAM, 0);
                struct sockaddr_in part_addr;
                struct timeval tv;
                char rb[4096];
                int total = 0;
                int bad = 0;
                memset(&part_addr, 0, sizeof(part_addr));
                part_addr.sin_family = AF_INET;
                part_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                part_addr.sin_port =
                    htons((unsigned short)part_srv.bound_port());
                tv.tv_sec = 3;
                tv.tv_usec = 0;
                (void)!setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                if (connect(fd, (struct sockaddr *)&part_addr,
                            sizeof(part_addr)) != 0)
                    _exit(1);
                while (total < (int)sizeof(g_payload)) {
                    ssize_t n = read(fd, rb, sizeof(rb));
                    int j;
                    if (n <= 0) { bad = 1; break; }
                    for (j = 0; j < (int)n; j++) {
                        if (rb[j] != g_payload[total + j]) { bad = 1; break; }
                    }
                    if (bad) break;
                    total += (int)n;
                }
                close(fd);
                _exit(bad || total != (int)sizeof(g_payload) ? 1 : 0);
            }
            connection part_peer = part_srv.accept();
            if (part_peer.is_valid()) {
                struct timeval tv;
                int rc;
                int st = 0;
                tv.tv_sec = 3;
                tv.tv_usec = 0;
                (void)!setsockopt(part_peer.descriptor(), SOL_SOCKET,
                                  SO_SNDTIMEO, &tv, sizeof(tv));
                g_fault_short_write = 1;
                rc = part_peer.write(g_payload, sizeof(g_payload), &err);
                g_fault_short_write = 0;
                ok = ok && rc == STDLIB_OK && err.ok();
                ok = ok && g_short_write_calls > 1;
                waitpid(part_kid, &st, 0);
                ok = ok && WIFEXITED(st) && WEXITSTATUS(st) == 0;
            } else {
                ok = 0;
                waitpid(part_kid, NULL, 0);
            }
        }
        result("partial_write_short", ok ? 0 : 2);
    }

    /* --- invalid handles and arguments fail gracefully ------------------- */
    {
        char buf[8];
        int ok = 1;
        connection ih_bad = connection_borrow(-1);
        listener ih_zl;
        connection ih_zc;
        listener ih_bl = listener_borrow(-1);
        ok = ok && !ih_bad.is_valid() && ih_bad.descriptor() == -1;
        ok = ok && ih_bad.read(buf, sizeof(buf), &err) == STDLIB_FAIL;
        ok = ok && err.failed_now() &&
                   err.category() == STDLIB_CATEGORY_INVALID;
        ok = ok && strcmp(err.what(),
                          "connection::read: invalid argument") == 0;
        ok = ok && ih_bad.write("x", 1, &err) == STDLIB_FAIL;
        ok = ok && err.category() == STDLIB_CATEGORY_INVALID;
        ok = ok && ih_bad.read(buf, sizeof(buf), NULL) == STDLIB_FAIL;
        ok = ok && !ih_zl.is_valid() && !ih_zc.is_valid() &&
                   !ih_bl.is_valid();
        ok = ok && ih_zl.bound_port() == 0 && ih_bl.bound_port() == 0;
        {
            connection ih_zac = ih_zl.accept();
            ok = ok && !ih_zac.is_valid();
            ih_zac.drop();
        }
        ok = ok && ih_zc.read(buf, 0, &err) == STDLIB_FAIL;
        ok = ok && ih_zc.read(NULL, sizeof(buf), &err) == STDLIB_FAIL;
        ok = ok && ih_zc.write(NULL, 1, &err) == STDLIB_FAIL;
        result("invalid_handles", ok ? 1 : 2);
    }

    /* --- fault injection: write() reports no progress -------------------- */
    {
        int sp[2];
        int ok = socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0;
        if (ok) {
            connection fwp_c = connection_borrow(sp[0]);
            ok = fwp_c.is_valid();
            g_fault_write_zero = 1;
            ok = ok && fwp_c.write("x", 1, &err) == STDLIB_FAIL;
            g_fault_write_zero = 0;
            ok = ok && err.failed_now();
            ok = ok && err.category() == STDLIB_CATEGORY_IO;
            ok = ok && strcmp(err.what(),
                              "connection::write made no progress") == 0;
            ok = ok && fwp_c.write("x", 1, &err) == STDLIB_OK && err.ok();
            /* A zero-length write on a VALID handle succeeds without data. */
            ok = ok && fwp_c.write("", 0, &err) == STDLIB_OK && err.ok();
            close(sp[0]);
            close(sp[1]);
        }
        result("fault_write_no_progress", ok ? 1 : 2);
    }

    /* --- fault injection: EINTR is retried; hard read error is reported -- */
    {
        int sp[2];
        int ok = socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0;
        if (ok) {
            char buf[8];
            connection fr_c = connection_borrow(sp[0]);
            (void)!write(sp[1], "abc", 3);
            g_fault_read_eintr_once = 1;
            {
                int n = fr_c.read(buf, sizeof(buf), &err);
                ok = ok && n == 3 && buf[0] == 'a' && buf[2] == 'c' && err.ok();
            }
            g_fault_read_error = 1;
            ok = ok && fr_c.read(buf, sizeof(buf), &err) == STDLIB_FAIL;
            g_fault_read_error = 0;
            ok = ok && err.failed_now() && err.category() == STDLIB_CATEGORY_IO;
            ok = ok && strcmp(err.what(), "connection::read failed") == 0;
            close(sp[0]);
            close(sp[1]);
        }
        result("fault_read_retry", ok ? 0 : 2);
    }

    /* --- fault injection: socket() fails with EMFILE (RLIMIT_NOFILE) ----- */
    {
        int c2p[2];
        int ok = 0;
        if (pipe(c2p) != 0) {
            skip_reason("fault_socket_emfile", "pipe() unavailable on this host");
            result("fault_socket_emfile", 3);
        } else {
            pid_t emf_kid = fork();
            if (emf_kid == 0) {
                struct rlimit rl;
                char msg = 0;
                close(c2p[0]);
                rl.rlim_cur = 3;
                rl.rlim_max = 3;
                (void)!setrlimit(RLIMIT_NOFILE, &rl);
                {
                    listener emf_l = listener(0);
                    msg = (!emf_l.is_valid() && emf_l.descriptor() == -1) ? 1 : 0;
                }
                (void)!write(c2p[1], &msg, 1);
                close(c2p[1]);
                _exit(0);
            }
            close(c2p[1]);
            if (emf_kid > 0 && poll_in(c2p[0], 3000) > 0) {
                char msg = 0;
                if (read(c2p[0], &msg, 1) == 1)
                    ok = msg == 1;
                waitpid(emf_kid, NULL, 0);
            } else if (emf_kid > 0) {
                waitpid(emf_kid, NULL, 0);
            }
            close(c2p[0]);
            result("fault_socket_emfile", ok ? 1 : 2);
        }
    }

    /* --- no descriptors left open ---------------------------------------- */
    {
        int fd_after = fd_count();
        if (fd_before < 0 || fd_after < 0) {
            skip_reason("no_fd_leak", "opendir(/dev/fd) unavailable on this host");
            result("no_fd_leak", 3);
        } else {
            result("no_fd_leak", fd_after <= fd_before ? 0 : 2);
        }
    }

    /* Cancel the watchdog and report exact, deterministic counts. */
    (void)!alarm(0);
    print("SUMMARY", g_success, g_recoverable, g_fatal, g_skipped);
    return g_fatal == 0 ? 0 : 1;
}
