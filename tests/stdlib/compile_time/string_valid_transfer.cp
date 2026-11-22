// EXPECT: ACCEPT
// Reason: cplus_move transfers String ownership exactly once; the source is zeroed and both cleanups are safe.
#include "cplus/cplus.h"
#include "stdlib/string.hp"

int main(void) {
    string a = string("hello");
    string b = cplus_move(a);
    return b.size() == 5 ? 0 : 1;
}
