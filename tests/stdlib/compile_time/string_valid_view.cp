// EXPECT: ACCEPT
// Reason: a string_view is valid while its owning String is still alive.
#include "cplus/cplus.h"
#include "stdlib/string.hp"

int main(void) {
    string s = string("hello");
    string_view v = s.to_view();
    return v.size() == 5 && v.equals("hello") ? 0 : 1;
}
