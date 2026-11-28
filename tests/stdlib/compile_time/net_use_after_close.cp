// EXPECT: RUNTIME
// Negative: a connection is read after an explicit drop(). C+ does not track
// explicit drop() state on class handles, so this is not provable statically:
// both c+ check --strict and c+ build --strict --sanitize=none accept it.
// The library's dropped handle is inert (fd = -1, owns = 0), so the real defect
// is contained at run time, not rejected at compile time.
// Compile-only probe: no socket is ever bound or connected.
#include "stdlib/net.hp"

int main(void) {
    error err = error();
    char buf[8];

    connection c = connection_borrow(7);
    c.drop();
    (void)c.read(buf, (size_t)sizeof(buf), &err); /* use after close */
    return 0;
}
