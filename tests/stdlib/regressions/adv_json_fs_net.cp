/* Adversarial regression family: malformed JSON, filesystem errors, and
 * local-only network disconnects/partial I/O.
 *
 * Self-checking. CASE classification:
 *   0 = SUCCESS      exact contract result observed
 *   1 = RECOVERABLE  expected rejection handled without crash/hang/leak
 *   2 = FATAL        contract violation (crash, corruption, hang, fd leak)
 *   3 = SKIP         not deterministic/portable here (reason printed)
 *
 * All paths are PID-unique; all sockets are AF_INET/INADDR_LOOPBACK or AF_UNIX
 * socketpairs, so no external server is contacted. Every wait is bounded and a
 * SIGALRM watchdog _exit(90)s if a syscall ever blocks unexpectedly. Owning
 * locals use unique names because C+ RAII lowering tracks owned variables by
 * name within a function. Final line is ADV_JSON_FS_NET; exit non-zero on any
 * FATAL case.
 */
#define _GNU_SOURCE
#include "stdlib/error.hp"
#include "stdlib/json.hp"
#include "stdlib/file.hp"
#include "stdlib/fs.hp"
#include "stdlib/net.hp"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* --- deterministic syscall fault injection ------------------------------- */
/* Transparent when all flags are clear (the common path). */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
static int g_fault_short_write = 0;
static int g_fault_write_zero = 0;
static int g_short_write_calls = 0;

ssize_t write(int fd, const void *buf, size_t n) {
    size_t chunk = n;
    if (g_fault_write_zero && fd >= 0)
        return 0;
    if (g_fault_short_write && chunk > 512)
        chunk = 512;
    if (g_fault_short_write)
        g_short_write_calls++;
    return syscall(SYS_write, fd, buf, chunk);
}
#pragma clang diagnostic pop

static void on_ignore(int sig) {
    (void)sig;
}

static void on_watchdog(int sig) {
    static const char msg[] = "FATAL watchdog timeout (adv_json_fs_net)\n";
    (void)sig;
    (void)!write(2, msg, sizeof(msg) - 1);
    _exit(90);
}

static int g_pass = 0;
static int g_recover = 0;
static int g_fatal = 0;
static int g_skip = 0;

static void result(const char *name, int cls) {
    print("CASE", name, cls);
    if (cls == 0)
        g_pass++;
    else if (cls == 1)
        g_recover++;
    else if (cls == 2)
        g_fatal++;
    else
        g_skip++;
}

static void skip_reason(const char *name, const char *why) {
    print("SKIP", name, why);
}

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

/* ------------------------------------------------------------------ paths */
static char g_base[256];
static char g_dir[256];
static char g_file[256];
static char g_missing[256];

/* ------------------------------------------------------------------- JSON */
static const char *const g_bad_json[] = {
    "",
    "{",
    "{oops",
    "{\"a\":",
    "{\"a\":1",
    "[1,2",
    "[1,2,",
    "\"unterminated",
    "tru",
    "nul",
    "fals",
    "01",
    "{\"a\":1,}",
    "{\"a\":1 \"b\":2}",
    "[",
    "}",
    "{\"a\":[1,{\"b\":2}"
};
static char g_first_msg[32][192];

static void json_family(void) {
    error err = error();
    const int count = (int)(sizeof(g_bad_json) / sizeof(g_bad_json[0]));
    int i;
    int round;
    int ok = 1;
    char buf[192];

    /* Deterministic inputs, repeated parse + double drop. Error text must be
     * byte-identical on every round (deterministic inputs -> deterministic
     * diagnostics), so a parse/free leak or state leak is observable. */
    for (round = 0; round < 30; round++) {
        for (i = 0; i < count; i++) {
            json bad = json(g_bad_json[i], &err);
            snprintf(buf, sizeof(buf), "%s", err.what());
            if (round == 0)
                snprintf(g_first_msg[i], sizeof(g_first_msg[i]), "%s", buf);
            ok = ok && !bad.valid() && err.failed_now();
            ok = ok && err.category() == STDLIB_CATEGORY_INVALID;
            ok = ok && strcmp(buf, g_first_msg[i]) == 0;
            bad.drop();
            bad.drop();
            {
                json_view orphan = bad.root();
                int out = 0;
                size_t len = 0;
                char *text = NULL;
                json_view orphan_member = orphan.member("x");
                json_view orphan_item = orphan.at(0);
                ok = ok && !orphan.valid() && orphan.size() == 0;
                ok = ok && !orphan.is_object() && !orphan.is_array();
                ok = ok && orphan.get_int(&out, &err) == STDLIB_FAIL;
                ok = ok && !orphan_member.valid() && !orphan_item.valid();
                text = orphan.write(&len);
                ok = ok && text == NULL;
            }
        }
    }
    result("json_malformed_repeated_parse_free", ok ? 0 : 2);
    print("CHECK json_bad_inputs", count);

    /* Valid document: typed accessors, serialise, json_free, double drop. */
    {
        static const char good[] =
            "{\"n\":42,\"s\":\"ok\",\"arr\":[1,2,3],\"b\":true}";
        json doc = json(good, &err);
        ok = doc.valid() && err.ok();
        if (doc.valid()) {
            json_view root = doc.root();
            json_view view_n = root.member("n");
            json_view view_b = root.member("b");
            json_view view_s = root.member("s");
            json_view view_arr = root.member("arr");
            json_view view_arr_1 = view_arr.at(1);
            json_view view_missing = root.member("nope");
            int n = 0;
            int flag = 0;
            const char *s = NULL;
            double d = 0;
            size_t len = 0;
            char *text = root.write(&len);
            ok = ok && root.is_object() && root.size() == 4;
            ok = ok && view_n.get_int(&n, &err) == STDLIB_OK && n == 42;
            ok = ok && view_b.get_bool(&flag, &err) == STDLIB_OK && flag == 1;
            ok = ok && view_s.get_string(&s, &err) == STDLIB_OK &&
                       s != NULL && strcmp(s, "ok") == 0;
            ok = ok && view_n.get_double(&d, &err) == STDLIB_OK && d == 42.0;
            ok = ok && view_arr.is_array() && view_arr.size() == 3;
            ok = ok && view_arr_1.get_int(&n, &err) == STDLIB_OK && n == 2;
            ok = ok && text != NULL && len > 0;
            if (text != NULL)
                json_free(text);
            ok = ok && !view_missing.valid();
        }
        doc.drop();
        doc.drop();
        result("json_valid_roundtrip", ok ? 0 : 2);
    }

    /* Builder: write, reparse, free buffer, then use-after-drop fails cleanly. */
    {
        json_builder builder = json_builder();
        size_t len = 0;
        char *text = NULL;
        ok = builder.valid();
        ok = ok && builder.add_int("i", 7, &err) == STDLIB_OK;
        ok = ok && builder.add_double("d", 2.5, &err) == STDLIB_OK;
        ok = ok && builder.add_bool("b", 1, &err) == STDLIB_OK;
        ok = ok && builder.add_string("s", "txt", &err) == STDLIB_OK;
        text = builder.write(&len);
        ok = ok && text != NULL && len > 0;
        if (text != NULL) {
            json rebuilt = json(text, &err);
            ok = ok && rebuilt.valid();
            rebuilt.drop();
            json_free(text);
        }
        builder.drop();
        builder.drop();
        ok = ok && builder.write(&len) == NULL;
        ok = ok && builder.add_int("i", 1, &err) == STDLIB_FAIL;
        result("json_builder_lifecycle", ok ? 0 : 2);
    }

    /* Missing file: deterministic PID-unique path, never created. */
    {
        json absent = json_parse_file(g_missing, &err);
        ok = !absent.valid() && err.failed_now();
        ok = ok && err.category() == STDLIB_CATEGORY_IO;
        absent.drop();
        absent.drop();
        ok = ok && !absent.valid();
        result("json_parse_missing_file", ok ? 0 : 2);
    }
}

/* --------------------------------------------------------------------- FS */
static void fs_family(void) {
    error err = error();
    int ok = 1;
    char badtmpl[32];
    int i;

    snprintf(badtmpl, sizeof(badtmpl), "no_stars");
    ok = fs_mkdtemp(badtmpl, sizeof(badtmpl), &err) == STDLIB_FAIL;
    ok = ok && err.failed_now() && err.category() == STDLIB_CATEGORY_INVALID;
    ok = ok && fs_mkdtemp(NULL, 32, &err) == STDLIB_FAIL;
    ok = ok && fs_mkdtemp(badtmpl, 3, &err) == STDLIB_FAIL;
    result("fs_mkdtemp_invalid_template", ok ? 1 : 2);

    /* Missing path: every query is false, every fallible op reports IO. */
    ok = !fs_exists(g_missing) && !fs_is_file(g_missing) &&
         !fs_is_dir(g_missing);
    ok = ok && fs_size(g_missing, &err) == -1 && err.failed_now() &&
               err.category() == STDLIB_CATEGORY_IO;
    for (i = 0; i < 3; i++) {
        ok = ok && fs_remove(g_missing, &err) == STDLIB_FAIL;
        ok = ok && err.failed_now() && err.category() == STDLIB_CATEGORY_IO;
    }
    ok = ok && fs_rename(g_missing, g_base, &err) == STDLIB_FAIL;
    ok = ok && fs_size(NULL, &err) == -1 && err.category() == STDLIB_CATEGORY_INVALID;
    ok = ok && fs_remove(NULL, &err) == STDLIB_FAIL;
    ok = ok && fs_mkdir(NULL, &err) == STDLIB_FAIL;
    result("fs_missing_path_errors", ok ? 1 : 2);

    /* PID-unique directory, then a file inside it. */
    ok = fs_mkdtemp(g_dir, sizeof(g_dir), &err) == STDLIB_OK;
    ok = ok && fs_is_dir(g_dir) && !fs_is_file(g_dir);
    if (ok) {
        /* mkdtemp rewrote the XXXXXX suffix; the file path must be rebuilt
         * from the final directory name, not the original template. */
        snprintf(g_file, sizeof(g_file), "%s/data.txt", g_dir);
        file created = file(g_file, "w");
        ok = ok && created.is_open();
        if (created.is_open())
            (void)!created.write("hello");
        created.drop();
        created.drop();
        ok = ok && fs_is_file(g_file) && !fs_is_dir(g_file);
        ok = ok && fs_size(g_file, &err) == 5 && err.ok();
        result("fs_create_and_size", ok ? 0 : 2);
    } else {
        skip_reason("fs_create_and_size", "fs_mkdtemp failed on this host");
        result("fs_create_and_size", 3);
    }

    /* Invalid modes: mkdir where a file exists, unlink a directory, and a
     * mkdtemp template that no longer has its XXXXXX suffix. */
    {
        int mode_ok = fs_mkdir(g_file, &err) == STDLIB_FAIL;
        mode_ok = mode_ok && err.failed_now() && err.category() == STDLIB_CATEGORY_IO;
        mode_ok = mode_ok && fs_remove(g_dir, &err) == STDLIB_FAIL;
        mode_ok = mode_ok && err.failed_now() && err.category() == STDLIB_CATEGORY_IO;
        mode_ok = mode_ok && fs_is_dir(g_dir); /* failed unlink kept the dir */
        mode_ok = mode_ok && fs_mkdtemp(g_dir, sizeof(g_dir), &err) == STDLIB_FAIL;
        mode_ok = mode_ok && err.category() == STDLIB_CATEGORY_INVALID;
        result("fs_invalid_mode", mode_ok ? 1 : 2);
    }

    /* Permission denial is deterministic only for an unprivileged user. */
    if (geteuid() == 0) {
        skip_reason("fs_permission_denied", "running as root; chmod denial not deterministic");
        result("fs_permission_denied", 3);
    } else {
        int perm_ok = chmod(g_file, 0000) == 0;
        if (perm_ok) {
            file denied = file(g_file, "r");
            perm_ok = !denied.is_open();
            denied.drop();
        }
        (void)!chmod(g_file, 0644);
        result("fs_permission_denied", perm_ok ? 0 : 2);
    }

    /* Repeated cleanup: first removal succeeds, subsequent ones fail with IO
     * and the path is gone. Directory removal uses rmdir directly. */
    {
        int clean_ok = fs_remove(g_file, &err) == STDLIB_OK && err.ok();
        clean_ok = clean_ok && !fs_exists(g_file);
        clean_ok = clean_ok && fs_remove(g_file, &err) == STDLIB_FAIL;
        clean_ok = clean_ok && err.category() == STDLIB_CATEGORY_IO;
        clean_ok = clean_ok && rmdir(g_dir) == 0;
        clean_ok = clean_ok && !fs_exists(g_dir);
        clean_ok = clean_ok && rmdir(g_dir) != 0;
        clean_ok = clean_ok && !fs_is_dir(g_dir) && !fs_is_file(g_dir);
        result("fs_repeated_cleanup", clean_ok ? 0 : 2);
    }
}

/* -------------------------------------------------------------------- NET */
static char g_payload[4096];

static int raw_loopback_bound(unsigned short *port_out) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    socklen_t len;
    if (fd < 0)
        return -1;
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
    return fd;
}

static void net_family(void) {
    error err = error();
    int sentinel = -1;
    int fd_before = -1;
    int i;
    int ok;

    sentinel = open("/dev/null", O_RDONLY);
    fd_before = fd_count(); /* after the sentinel, so it is not counted as a leak */
    for (i = 0; i < (int)sizeof(g_payload); i++)
        g_payload[i] = (char)(i % 251);

    /* Out-of-range ports: inert handles, idempotent drop, no fd consumed. */
    {
        listener neg_port = listener(-1);
        listener big_port = listener(70000);
        listener low_port = listener(INT_MIN);
        ok = !neg_port.is_valid() && !big_port.is_valid() && !low_port.is_valid();
        ok = ok && neg_port.descriptor() == -1 && big_port.descriptor() == -1 &&
                   low_port.descriptor() == -1;
        ok = ok && neg_port.bound_port() == 0 && big_port.bound_port() == 0 &&
                   low_port.bound_port() == 0;
        neg_port.drop();
        neg_port.drop();
        big_port.drop();
        low_port.drop();
        result("net_invalid_ports", ok ? 1 : 2);
    }

    /* Ephemeral loopback bind reports the real port. */
    {
        listener ephem = listener(0);
        ok = ephem.is_valid() && ephem.descriptor() > 2 && ephem.bound_port() > 0;
        result("net_ephemeral_bind", ok ? 0 : 2);
        ephem.drop();
        ephem.drop();
    }

    /* AF_UNIX peer close: read -> EOF, write -> EPIPE reported as IO failure. */
    {
        int sp[2];
        int pair_ok = socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0;
        if (pair_ok) {
            connection peer = connection_borrow(sp[0]);
            char rb[8];
            close(sp[1]);
            ok = peer.is_valid() && peer.read(rb, sizeof(rb), &err) == STDLIB_EOF;
            ok = ok && err.ok();
            ok = ok && peer.write("x", 1, &err) == STDLIB_FAIL;
            ok = ok && err.failed_now() && err.category() == STDLIB_CATEGORY_IO;
            peer.drop();
            peer.drop();
            /* A borrowed handle never closes: the descriptor stays valid
             * until this test closes it explicitly. */
            ok = ok && fcntl(sp[0], F_GETFD) != -1;
            close(sp[0]);
            result("net_afunix_disconnect", ok ? 1 : 2);
        } else {
            skip_reason("net_afunix_disconnect", "socketpair(AF_UNIX) unavailable");
            result("net_afunix_disconnect", 3);
        }
    }

    /* Loopback disconnect: exact bytes then clean EOF after peer close. */
    {
        listener eof_srv = listener(0);
        int srv_ok = eof_srv.is_valid();
        if (srv_ok) {
            pid_t kid = fork();
            if (kid == 0) {
                int fd = socket(AF_INET, SOCK_STREAM, 0);
                struct sockaddr_in a;
                memset(&a, 0, sizeof(a));
                a.sin_family = AF_INET;
                a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                a.sin_port = htons((unsigned short)eof_srv.bound_port());
                if (connect(fd, (struct sockaddr *)&a, sizeof(a)) == 0) {
                    (void)!write(fd, "hi", 2);
                    close(fd);
                    _exit(0);
                }
                close(fd);
                _exit(1);
            }
            connection peer = eof_srv.accept();
            if (peer.is_valid()) {
                char rb[8];
                int n = peer.read(rb, sizeof(rb), &err);
                int e = peer.read(rb, sizeof(rb), &err);
                ok = n == 2 && rb[0] == 'h' && rb[1] == 'i';
                ok = ok && e == STDLIB_EOF && err.ok();
                waitpid(kid, NULL, 0);
                peer.drop(); /* owning accept() result: must be closed once */
                peer.drop();
            } else {
                ok = 0;
                waitpid(kid, NULL, 0);
            }
            result("net_loopback_disconnect", ok ? 0 : 2);
        } else {
            skip_reason("net_loopback_disconnect", "loopback listener(0) unavailable");
            result("net_loopback_disconnect", 3);
        }
        eof_srv.drop();
        eof_srv.drop();
    }

    /* Partial writes: interposed short writes must still deliver every byte. */
    {
        listener part_srv = listener(0);
        int srv_ok = part_srv.is_valid();
        if (srv_ok) {
            pid_t kid = fork();
            if (kid == 0) {
                int fd = socket(AF_INET, SOCK_STREAM, 0);
                struct sockaddr_in a;
                struct timeval tv;
                char rb[512];
                int total = 0;
                int bad = 0;
                memset(&a, 0, sizeof(a));
                a.sin_family = AF_INET;
                a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                a.sin_port = htons((unsigned short)part_srv.bound_port());
                tv.tv_sec = 5;
                tv.tv_usec = 0;
                (void)!setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0)
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
            connection peer = part_srv.accept();
            if (peer.is_valid()) {
                struct timeval tv;
                int st = 0;
                tv.tv_sec = 5;
                tv.tv_usec = 0;
                (void)!setsockopt(peer.descriptor(), SOL_SOCKET, SO_SNDTIMEO,
                                  &tv, sizeof(tv));
                g_short_write_calls = 0;
                g_fault_short_write = 1;
                ok = peer.write(g_payload, sizeof(g_payload), &err) == STDLIB_OK;
                g_fault_short_write = 0;
                ok = ok && err.ok() && g_short_write_calls > 1;
                waitpid(kid, &st, 0);
                ok = ok && WIFEXITED(st) && WEXITSTATUS(st) == 0;
                peer.drop(); /* owning accept() result: must be closed once */
                peer.drop();
                result("net_partial_write", ok ? 0 : 2);
            } else {
                waitpid(kid, NULL, 0);
                result("net_partial_write", 3);
            }
        } else {
            skip_reason("net_partial_write", "loopback listener(0) unavailable");
            result("net_partial_write", 3);
        }
        part_srv.drop();
        part_srv.drop();
    }

    /* Zero-progress write is a recoverable IO error; recovery is immediate. */
    {
        int sp[2];
        int pair_ok = socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0;
        if (pair_ok) {
            connection peer = connection_borrow(sp[0]);
            ok = peer.is_valid();
            g_fault_write_zero = 1;
            ok = ok && peer.write("x", 1, &err) == STDLIB_FAIL;
            g_fault_write_zero = 0;
            ok = ok && err.failed_now() && err.category() == STDLIB_CATEGORY_IO;
            ok = ok && peer.write("x", 1, &err) == STDLIB_OK && err.ok();
            peer.drop();
            close(sp[1]);
            close(sp[0]);
            result("net_write_no_progress", ok ? 1 : 2);
        } else {
            skip_reason("net_write_no_progress", "socketpair(AF_UNIX) unavailable");
            result("net_write_no_progress", 3);
        }
    }

    /* Invalid handles and arguments fail without touching process fds 0-2. */
    {
        char rb[8];
        connection bad_conn = connection_borrow(-1);
        listener zero_listener;
        connection zero_conn;
        listener bad_listener = listener_borrow(-1);
        int before[3];
        int after[3];
        int j;
        ok = !bad_conn.is_valid() && bad_conn.descriptor() == -1;
        ok = ok && bad_conn.read(rb, sizeof(rb), &err) == STDLIB_FAIL;
        ok = ok && err.category() == STDLIB_CATEGORY_INVALID;
        ok = ok && bad_conn.write("x", 1, &err) == STDLIB_FAIL;
        ok = ok && err.category() == STDLIB_CATEGORY_INVALID;
        ok = ok && bad_conn.read(NULL, sizeof(rb), &err) == STDLIB_FAIL;
        ok = ok && bad_conn.write(NULL, 1, &err) == STDLIB_FAIL;
        ok = ok && !zero_listener.is_valid() && !zero_conn.is_valid();
        ok = ok && !bad_listener.is_valid() && bad_listener.bound_port() == 0;
        {
            connection invalid_accept = zero_listener.accept();
            ok = ok && !invalid_accept.is_valid();
            invalid_accept.drop();
        }
        for (j = 0; j < 3; j++)
            before[j] = fcntl(j, F_GETFD);
        bad_conn.drop();
        bad_conn.drop();
        zero_listener.drop();
        zero_conn.drop();
        bad_listener.drop();
        for (j = 0; j < 3; j++)
            after[j] = fcntl(j, F_GETFD);
        for (j = 0; j < 3; j++)
            ok = ok && before[j] != -1 && after[j] != -1;
        result("net_invalid_handles", ok ? 1 : 2);
    }

    /* Owning handles: drop twice, sentinel descriptor must stay open. */
    {
        int sp[2];
        listener owning_listener = listener(0);
        int pair_ok = socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0;
        ok = owning_listener.is_valid();
        if (pair_ok) {
            connection owning_conn = connection(sp[0]);
            ok = ok && owning_conn.is_valid();
            owning_conn.drop();
            owning_conn.drop();
            ok = ok && owning_conn.descriptor() == -1 && !owning_conn.is_valid();
            close(sp[1]);
        } else {
            ok = 0;
        }
        owning_listener.drop();
        owning_listener.drop();
        ok = ok && owning_listener.descriptor() == -1 &&
                   !owning_listener.is_valid();
        ok = ok && sentinel >= 3 && fcntl(sentinel, F_GETFD) != -1;
        result("net_repeated_close", ok ? 0 : 2);
    }

    /* No leaked descriptors across the whole family. */
    {
        int fd_after = fd_count();
        if (fd_before < 0 || fd_after < 0) {
            skip_reason("net_no_fd_leak", "opendir(/dev/fd) unavailable");
            result("net_no_fd_leak", 3);
        } else {
            result("net_no_fd_leak", fd_after <= fd_before ? 0 : 2);
            print("CHECK fd_before_after", fd_before, fd_after);
        }
    }

    if (sentinel >= 3)
        close(sentinel);
}

int main(void) {
    (void)!signal(SIGPIPE, on_ignore);
    (void)!signal(SIGALRM, on_watchdog);
    (void)!alarm(25);

    snprintf(g_base, sizeof(g_base), "/tmp/cplus_adv_jsfsnet_%ld", (long)getpid());
    snprintf(g_dir, sizeof(g_dir), "/tmp/cplus_adv_jsfsnet_%ld_XXXXXX", (long)getpid());
    snprintf(g_file, sizeof(g_file), "%s/data.txt", g_dir);
    snprintf(g_missing, sizeof(g_missing), "%s_missing.json", g_base);

    json_family();
    fs_family();
    net_family();

    (void)!alarm(0);
    print("SUMMARY", g_pass, g_recover, g_fatal, g_skip);
    print("RESULT", g_fatal == 0 ? "PASS" : "FAIL", g_fatal);
    print("ADV_JSON_FS_NET");
    return g_fatal == 0 ? 0 : 1;
}
