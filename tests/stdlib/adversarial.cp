/* Adversarial resource-safety tests: double drop, moved-from handles,
 * borrowed handles, zero-initialised handles, partial initialisation,
 * overflow and failure cleanup.  Every case must be inert, not fatal. */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "stdlib/vector.hp"
#include "stdlib/file.hp"
#include "stdlib/thread.hp"
#include "stdlib/net.hp"
#include "stdlib/json.hp"
#include "stdlib/image.hp"
#include "stdlib/http.hp"
#include "stdlib/async.hp"
#include "stdlib/window.hp"
#include "stdlib/graphics.hp"
#include "stdlib/gui.hp"

static int failures = 0;

static void check(int condition, const char *what) {
    if (!condition) {
        failures++;
        print("FAIL", what);
    }
}

int main(void) {
    error err = error();
    int sentinel = open("/dev/null", O_RDONLY);
    check(sentinel >= 3, "sentinel fd opened");

    /* ---- string: use after drop must be an empty string, not a crash ---- */
    string s = string("hello");
    s.drop();
    char buf[16];
    check(s.size() == 0, "dropped string size");
    check(s.c_str() != NULL && s.c_str()[0] == '\0', "dropped string c_str");
    check(s.equals("") == 1, "dropped string equals empty");
    check(s.compare("x") == -1, "dropped string compare");
    check(s.find("x") == -1, "dropped string find");
    check(s.at(0) == '\0', "dropped string at");
    check(s.append("again") == 0, "append after drop re-initialises");
    check(s.equals("again") == 1, "string usable again after drop");
    check(s.substr(0, 2, buf, sizeof(buf)) == 0 && strcmp(buf, "ag") == 0,
          "substr after drop+append");
    check(s.field(',', 0, buf, sizeof(buf)) == 0, "field after reuse");
    s.drop();
    s.drop();                                                    /* double drop */

    /* ---- vector: reuse after drop, checked access stays in range ------ */
    vector v = vector();
    check(v.push_back(1) == 0 && v.size() == 1, "vector push");
    v.drop();
    v.drop();                                                    /* double drop */
    check(v.size() == 0, "vector size after drop");
    check(v.at(0) == 0, "vector at after drop");
    check(v.pop_back(NULL) == -1, "vector pop NULL out");
    check(v.push_back(7) == 0 && v.at(0) == 7, "vector reusable after drop");
    check(v.reserve(SIZE_MAX / 2) == -1, "vector reserve overflow rejected");

    /* ---- file: double drop, moved handle, borrowed stdout ------------ */
    file f = file("/tmp/cplus_adversarial.txt", "w");
    check(f.is_open(), "file opened");
    f.drop();
    f.drop();                                                    /* double drop */
    check(!f.is_open(), "file closed after drop");
    f.write("ignored");
    check(f.read(buf, sizeof(buf), &err) == STDLIB_FAIL && err.failed_now(),
          "file read after drop reports");

    file original = file("/tmp/cplus_adversarial.txt", "r");
    file moved = cplus_move(original);
    check(!original.is_open(), "moved-from file is inert");
    check(moved.is_open(), "moved-to file owns the handle");
    check(fcntl(sentinel, F_GETFD) != -1, "sentinel survives a move");

    file console = file_borrow(stdout);
    check(console.is_open(), "borrowed stdout valid");
    /* dropped at scope exit: must not close stdout */

    /* ---- mutex/guard: zero-initialised and double drop ---------------- */
    mutex m = mutex();
    check(m.is_valid(), "mutex valid");
    {
        guard g = guard(&m);
        check(g.holds(), "guard holds");
        g.unlock();
        g.drop();
    }
    m.drop();
    m.drop();                                                    /* double drop */
    check(!m.is_valid(), "mutex invalid after drop");
    guard none = guard(NULL);
    check(!none.holds(), "null guard holds nothing");

    /* ---- sockets: zero-init must not close fd 0 or 1/2 ---------------- */
    listener zero_listener;
    connection zero_connection;
    check(!zero_listener.is_valid(), "zero listener invalid");
    check(!zero_connection.is_valid(), "zero connection invalid");
    zero_listener.drop();
    zero_connection.drop();
    check(fcntl(0, F_GETFD) != -1, "stdin still open after zero-init drops");
    check(fcntl(1, F_GETFD) != -1, "stdout still open after zero-init drops");
    check(fcntl(sentinel, F_GETFD) != -1, "sentinel still open");

    listener server = listener(0);
    if (server.is_valid()) {
        listener borrowed = listener_borrow(server.descriptor());
        borrowed.drop();                       /* must not close the owner */
        check(server.is_valid(), "borrow drop leaves owner valid");
        server.drop();
        server.drop();                                          /* double drop */
        check(!server.is_valid(), "listener invalid after drop");
    }

    /* ---- json: failed parse cleanup, double drop, moved-from view ----- */
    json bad = json("{oops", &err);
    check(!bad.valid() && err.failed_now(), "failed parse reports");
    bad.drop();
    bad.drop();                                                  /* double drop */
    json_view missing_root = bad.root();
    check(!missing_root.valid(), "dropped doc has no root");
    check(missing_root.size() == 0, "dropped doc view size");
    check(missing_root.get_int(NULL, &err) == STDLIB_FAIL,
          "null out is rejected");

    json_builder builder = json_builder();
    builder.add_int("a", 1, &err);
    builder.drop();
    builder.drop();                                              /* double drop */
    check(builder.write(NULL) == NULL, "builder after drop writes nothing");

    /* ---- image: overflow and partial-init cleanup --------------------- */
    image huge = image_from_pixels((const unsigned char *)"x", 0x7fffffff,
                                   0x7fffffff, 4, &err);
    check(!huge.valid() && err.failed_now(), "image size overflow rejected");
    image none_img = image("/nonexistent/x.png", 0, &err);
    check(!none_img.valid(), "missing image invalid");
    none_img.drop();
    none_img.drop();                                             /* double drop */

    /* ---- http: after drop --------------------------------------------- */
    http global = http(&err);
    http_headers headers = http_headers();
    headers.add("X-A: 1", &err);
    http_client client = http_client(&err);
    client.drop();
    check(!client.valid(), "http client invalid after drop");
    check(client.status() == 0 && client.bytes() == 0, "status after drop");
    check(client.get("file:///dev/null", buf, sizeof(buf), &err) != STDLIB_OK,
          "request after drop reports");
    headers.drop();
    headers.drop();                                              /* double drop */

    /* ---- async: timer dropped twice, loop dropped after its timers ---- */
    loop l = loop(&err);
    if (l.valid()) {
        timer t = timer(l.native(), &err);
        if (t.valid()) {
            t.stop(&err);
            t.drop();
            t.drop();                                            /* double drop */
        }
        check(!t.valid(), "timer invalid after drop");
    }

    /* ---- sdl/gui: drop without frames, then double drop --------------- */
    sdl core = sdl(SDL_INIT_VIDEO, &err);
    if (core.valid()) {
        window w = window("adv", 32, 32, SDL_WINDOW_HIDDEN, &err);
        renderer r = renderer(w.native(), "software", &err);
        gui ui = gui(w.native(), r.native(), &err);
        if (ui.valid()) {
            ui.drop();
            ui.drop();                                           /* double drop */
            check(!ui.valid(), "gui invalid after drop");
        }
        r.drop();
        r.drop();                                                /* double drop */
        w.drop();
        w.drop();                                                /* double drop */
    }
    core.drop();
    core.drop();                                                 /* double drop */

    check(fcntl(sentinel, F_GETFD) != -1, "sentinel survived every case");
    close(sentinel);

    print("adversarial", failures == 0 ? "clean" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
