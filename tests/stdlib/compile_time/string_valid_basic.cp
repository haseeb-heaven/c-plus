// EXPECT: ACCEPT
// Reason: constructing, reading and automatically dropping an owned String is a valid lifetime.
#include "cplus/cplus.h"
#include "stdlib/string.hp"

int main(void) {
    string s = string("hello");
    if (s.size() != 5)
        return 1;
    return s.at(0) == 'h' ? 0 : 1;
}
