// EXPECT: ACCEPT
// Positive control: normal owned connection lifecycle. The handle is only
// used while live, released exactly once, and never used again afterwards.
// Compile-only probe: no socket is ever bound or connected.
#include "stdlib/net.hp"

int main(void) {
    error err = error();
    char buf[16];

    connection c = connection_borrow(7);
    if (c.is_valid()) {
        int n = c.read(buf, (size_t)sizeof(buf), &err);
        if (n > 0)
            (void)c.write(buf, (size_t)n, &err);
    }
    c.drop();
    return 0;
}
