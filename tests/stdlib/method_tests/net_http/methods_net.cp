/* 100% public-method test for stdlib/net.hp.
 *
 * All 14 callables in the header are invoked by a named case and verified.
 * No external network is used: connection cases use AF_UNIX socketpairs and
 * listener cases bind 127.0.0.1 on an ephemeral port with a local fork() peer.
 * Socket ownership is checked with fcntl(F_GETFD) so drop()/borrow semantics
 * are verified rather than assumed.  Prints "<header> <method> PASS|FAIL"
 * once per method; exits non-zero iff any method fails.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/net.hp"

static int mt_total = 0;
static int mt_passed = 0;
static int mt_failed = 0;

static void mt_report(const char *method, int ok) {
    mt_total++;
    if (ok) mt_passed++; else mt_failed++;
    print("net.hp", method, ok ? "PASS" : "FAIL");
}

int main(void) {
    error err = error();
    int sp[2];
    int ok;
    char buf[32];
    char rbuf[32];
    int status;

    /* 1. connection.connection (the header's internal-by-name ctor) */
    connection direct = connection(-1);
    ok = direct.descriptor() == -1 && !direct.is_valid();
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0) {
        connection owned = connection(sp[0]);
        ok = ok && owned.descriptor() == sp[0] && owned.is_valid();
        owned.drop(); /* closes sp[0]: ownership proven in the drop case too */
        close(sp[1]);
    } else {
        ok = 0;
    }
    mt_report("connection.connection", ok);

    /* 2. connection.is_valid: fd 0/1/2 guards and moved/inert handles */
    connection zero_conn;
    connection borrow0 = connection_borrow(0);
    connection borrow1 = connection_borrow(1);
    connection borrow2 = connection_borrow(2);
    ok = !zero_conn.is_valid() && !borrow0.is_valid() &&
         !borrow1.is_valid() && !borrow2.is_valid();
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0) {
        connection borrowed = connection_borrow(sp[0]);
        ok = ok && borrowed.is_valid();
        close(sp[0]); close(sp[1]);
    } else {
        ok = 0;
    }
    mt_report("connection.is_valid", ok);

    /* 3. connection.descriptor */
    connection neg = connection(-1);
    connection b2fd = connection_borrow(2);
    ok = neg.descriptor() == -1 && zero_conn.descriptor() == 0 &&
         b2fd.descriptor() == 2;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0) {
        connection bfd = connection_borrow(sp[0]);
        ok = ok && bfd.descriptor() == sp[0];
        close(sp[0]); close(sp[1]);
    } else {
        ok = 0;
    }
    mt_report("connection.descriptor", ok);

    /* 4. connection.read: byte count, EOF and invalid-argument refusal */
    ok = 0;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0) {
        connection reader = connection_borrow(sp[0]);
        (void)!write(sp[1], "ping", 4);
        memset(buf, 0, sizeof(buf));
        error_reset(&err);
        ok = reader.read(buf, sizeof(buf) - 1u, &err) == 4 &&
             strcmp(buf, "ping") == 0 && err.ok();
        close(sp[1]);
        error_reset(&err);
        ok = ok && reader.read(buf, sizeof(buf) - 1u, &err) == STDLIB_EOF;
        error_reset(&err);
        ok = ok && reader.read((char *)0, 8u, &err) == STDLIB_FAIL &&
             err.category() == STDLIB_CATEGORY_INVALID;
        error_reset(&err);
        ok = ok && reader.read(buf, 0u, &err) == STDLIB_FAIL &&
             err.category() == STDLIB_CATEGORY_INVALID;
        close(sp[0]);
    }
    mt_report("connection.read", ok);

    /* 5. connection.write: full transfer, zero length and NULL refusal */
    ok = 0;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0) {
        connection writer = connection_borrow(sp[0]);
        error_reset(&err);
        ok = writer.write("pong", 4u, &err) == STDLIB_OK && err.ok();
        memset(rbuf, 0, sizeof(rbuf));
        ok = ok && read(sp[1], rbuf, sizeof(rbuf) - 1u) == 4 &&
             strcmp(rbuf, "pong") == 0;
        error_reset(&err);
        ok = ok && writer.write("x", 0u, &err) == STDLIB_OK;
        error_reset(&err);
        ok = ok && writer.write((const char *)0, 4u, &err) == STDLIB_FAIL &&
             err.category() == STDLIB_CATEGORY_INVALID;
        close(sp[0]); close(sp[1]);
    }
    mt_report("connection.write", ok);

    /* 6. connection.drop: closes owned fd, never a borrowed fd, idempotent */
    ok = 0;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0) {
        connection owned = connection(sp[0]);
        connection borrowed = connection_borrow(sp[1]);
        owned.drop();
        ok = fcntl(sp[0], F_GETFD) == -1 && errno == EBADF &&
             owned.descriptor() <= 0 && !owned.is_valid();
        owned.drop();
        ok = ok && owned.descriptor() <= 0; /* idempotent; C+ zeroes consumed owner */
        borrowed.drop();
        ok = ok && fcntl(sp[1], F_GETFD) != -1 && /* borrowed stays open */
             borrowed.descriptor() <= 0; /* consumed view: C+ zeroes the owner */
        close(sp[1]);
    }
    mt_report("connection.drop", ok);

    /* 7. listener.listener: ephemeral bind and invalid-port inertness */
    listener srv = listener(0);
    listener bad_port = listener(70000);
    listener neg_port = listener(-1);
    ok = srv.is_valid() && srv.descriptor() > 2 && srv.bound_port() > 0;
    ok = ok && !bad_port.is_valid() && bad_port.descriptor() == -1;
    ok = ok && !neg_port.is_valid() && neg_port.descriptor() == -1;
    mt_report("listener.listener", ok);

    /* 8. listener.is_valid: zero-init, fd 0/1/2, valid and invalid ports */
    listener zero_listener;
    listener lb0 = listener_borrow(0);
    listener lb1 = listener_borrow(1);
    listener lb2 = listener_borrow(2);
    ok = !zero_listener.is_valid() && !lb0.is_valid() &&
         !lb1.is_valid() && !lb2.is_valid();
    ok = ok && srv.is_valid() && !bad_port.is_valid();
    mt_report("listener.is_valid", ok);

    /* 9. listener.descriptor */
    ok = srv.descriptor() > 2 && bad_port.descriptor() == -1 &&
         zero_listener.descriptor() == 0 && lb2.descriptor() == 2;
    mt_report("listener.descriptor", ok);

    /* 10. listener.bound_port */
    ok = srv.bound_port() > 0 && bad_port.bound_port() == 0 &&
         zero_listener.bound_port() == 0;
    mt_report("listener.bound_port", ok);

    /* 11. listener.accept: real loopback peer plus invalid-listener refusal */
    ok = 1;
    if (srv.is_valid()) {
        int port = srv.bound_port();
        pid_t child = fork();
        if (child == 0) {
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in peer;
            memset(&peer, 0, sizeof(peer));
            peer.sin_family = AF_INET;
            peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            peer.sin_port = htons((unsigned short)port);
            if (connect(fd, (struct sockaddr *)&peer, sizeof(peer)) == 0) {
                char cbuf[8];
                (void)!write(fd, "a", 1);
                if (read(fd, cbuf, sizeof(cbuf)) == 1 && cbuf[0] == 'b')
                    _exit(0);
            }
            close(fd);
            _exit(1);
        }
        connection peer = srv.accept();
        ok = peer.is_valid() && peer.descriptor() > 2;
        memset(buf, 0, sizeof(buf));
        error_reset(&err);
        ok = ok && peer.read(buf, sizeof(buf) - 1u, &err) == 1 && buf[0] == 'a';
        error_reset(&err);
        ok = ok && peer.write("b", 1u, &err) == STDLIB_OK;
        waitpid(child, &status, 0);
        ok = ok && WIFEXITED(status) && WEXITSTATUS(status) == 0;
        peer.drop();
    } else {
        ok = 0;
    }
    {
        connection none = bad_port.accept();
        ok = ok && !none.is_valid() && none.descriptor() == -1;
    }
    mt_report("listener.accept", ok);

    /* 12. listener.drop: closes owned fd, never a borrowed fd, idempotent */
    ok = 0;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0) {
        listener owned_l = listener(0);
        listener borrowed_l = listener_borrow(sp[0]);
        int lfd = owned_l.descriptor();
        ok = lfd > 2;
        owned_l.drop();
        ok = ok && fcntl(lfd, F_GETFD) == -1 && errno == EBADF &&
             owned_l.descriptor() <= 0 && !owned_l.is_valid();
        owned_l.drop();
        ok = ok && owned_l.descriptor() <= 0; /* idempotent; C+ zeroes consumed owner */
        borrowed_l.drop();
        ok = ok && fcntl(sp[0], F_GETFD) != -1; /* borrowed stays open */
        close(sp[0]); close(sp[1]);
    }
    mt_report("listener.drop", ok);

    /* 13. listener_borrow: shares fd/port but never owns it */
    ok = 0;
    if (srv.is_valid()) {
        listener lb_srv = listener_borrow(srv.descriptor());
        int sfd = srv.descriptor();
        ok = lb_srv.descriptor() == sfd && lb_srv.is_valid() &&
             lb_srv.bound_port() == srv.bound_port();
        lb_srv.drop();
        ok = ok && fcntl(sfd, F_GETFD) != -1; /* original still open */
    }
    {
        listener b1 = listener_borrow(1);
        ok = ok && b1.descriptor() == 1 && !b1.is_valid() && b1.bound_port() == 0;
    }
    mt_report("listener_borrow", ok);

    /* 14. connection_borrow: shares fd but never owns it */
    ok = 0;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0) {
        connection borrowed = connection_borrow(sp[0]);
        ok = borrowed.descriptor() == sp[0] && borrowed.is_valid();
        borrowed.drop();
        ok = ok && fcntl(sp[0], F_GETFD) != -1; /* original still open */
        close(sp[0]); close(sp[1]);
    }
    {
        connection b2 = connection_borrow(2);
        ok = ok && b2.descriptor() == 2 && !b2.is_valid();
    }
    mt_report("connection_borrow", ok);

    srv.drop();
    bad_port.drop();
    neg_port.drop();

    print("METHOD_TEST net.hp", mt_total, mt_passed, mt_failed, 0);
    return mt_failed != 0;
}
