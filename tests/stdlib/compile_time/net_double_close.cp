// EXPECT: RUNTIME
// Negative: a connection is released twice. C+ treats class drop() as a plain
// method, not a tracked release, so the double release is not provable at
// compile time: both c+ check --strict and c+ build --strict --sanitize=none
// accept it. net.hp makes the second drop() inert (fd = -1, owns = 0), so no
// descriptor is closed twice at run time.
// Compile-only probe: no socket is ever bound or connected.
#include "stdlib/net.hp"

int main(void) {
    connection c = connection_borrow(7);
    c.drop();
    c.drop(); /* second release */
    return 0;
}
