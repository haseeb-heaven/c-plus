// EXPECT: REJECT
// Negative: the moved-from connection is used after cplus_move() transferred
// its ownership. C+ rejects direct reuse of a zeroed, moved-from owner.
// Compile-only probe: no socket is ever bound or connected.
#include "stdlib/net.hp"

int main(void) {
    error err = error();
    char buf[8];

    connection a = connection_borrow(7);
    connection b = cplus_move(a);
    (void)b.descriptor();
    (void)a.read(buf, (size_t)sizeof(buf), &err); /* moved-from handle reused */
    return 0;
}
