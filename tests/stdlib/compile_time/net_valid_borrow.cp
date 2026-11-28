// EXPECT: ACCEPT
// Positive control: borrowed listener/connection handles plus a tracked
// borrow used strictly before the owner's release. No socket is bound.
#include "stdlib/net.hp"

int main(void) {
    listener l = listener_borrow(9);
    connection c = connection_borrow(11);

    connection *view = &c;
    int fd_before = view->descriptor();
    int valid = view->is_valid();

    l.drop();
    c.drop();

    return (fd_before == 11 && valid) ? 0 : 0;
}
