/* Regression tests for the review findings: overlapping String append with
 * reallocation, HTTP header lifetime, libuv timer/handle lifetime, and
 * buffer/length validation in File and socket operations. */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "stdlib/file.hp"
#include "stdlib/net.hp"
#include "stdlib/async.hp"
#include "stdlib/http.hp"

int main(void) {
    error err = error();
    char body[256];
    int failures = 0;

    /* 1. Self-append that forces a reallocation. */
    string a = string("ab");
    if (a.append(a.c_str()) != 0)
        failures++;
    if (!a.equals("abab") || a.size() != 4)
        failures++;
    /* Grow until the buffer is exactly full, then self-append again. */
    if (a.append(a.c_str()) != 0)
        failures++;
    print("self-append", a.size(), a.equals("abababab"));

    /* Appending a suffix of the same string must survive reallocation. */
    string b = string("xyz");
    b.reserve(4);                                   /* exactly full after "xyz" */
    if (b.append(b.c_str() + 1) != 0)
        failures++;
    print("suffix-append", b.c_str(), b.equals("xyzyz") ? 1 : 0);

    /* 2. The client must keep its own copy of the header list. */
    http global = http(&err);
    http_client client = http_client(&err);
    {
        http_headers headers = http_headers();
        headers.add("X-Lifetime: copied", &err);
        if (client.set_headers(&headers, &err) != STDLIB_OK)
            failures++;
    }                                               /* headers dropped here */
    /* The client must still own its copied lines after the source died. */
    print("http-headers-lifetime", client.header_lines() == 1, client.valid());
    if (client.header_lines() != 1)
        failures++;

    /* 3. Timer dropped while its close is pending, then the loop keeps going. */
    loop l = loop(&err);
    if (l.valid()) {
        timer t = timer(l.native(), &err);
        if (t.valid()) {
            t.start(NULL, 1, 0, NULL, &err);        /* NULL cb is rejected */
            t.stop(&err);
        }
        t.drop();                                   /* close is async */
        l.run(UV_RUN_NOWAIT, &err);                 /* must not touch freed memory */
        print("timer-lifetime", t.valid() == 0, l.valid());
    }

    /* 4. File and socket validation. */
    file f = file("/tmp/cplus_safety.txt", "w");
    if (f.is_open()) {
        if (f.write(NULL) != 0)
            failures++;
        if (f.write_bytes(NULL, 4, &err) != STDLIB_FAIL)
            failures++;
        if (!err.failed_now())
            failures++;
    }
    file r = file("/tmp/cplus_safety.txt", "r");
    if (r.is_open()) {
        if (r.read(NULL, 8, &err) != STDLIB_FAIL)
            failures++;
        if (r.read(body, 0, &err) != STDLIB_FAIL)
            failures++;
        if (r.read_line(body, 0, &err) != STDLIB_FAIL)
            failures++;
    }

    /* A non-blocking pipe cannot absorb everything: write() must report the
     * short write instead of silently dropping bytes. */
    {
        int fds[2];
        if (pipe(fds) == 0) {
            int flags = fcntl(fds[1], F_GETFL, 0);
            fcntl(fds[1], F_SETFL, flags | O_NONBLOCK);
            connection sink = connection(fds[1]);
            char *chunk = (char *)cplus_alloc(1 << 20);
            memset(chunk, 'z', 1 << 20);
            int st = sink.write(chunk, 1 << 20, &err);
            print("no-silent-partial-write", st == STDLIB_FAIL, err.failed_now());
            sink.drop();
            cplus_release(chunk);
            close(fds[0]);
        }
    }

    print("safety-regressions", failures == 0 ? "clean" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
