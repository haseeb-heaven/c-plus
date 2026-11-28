// EXPECT: RUNTIME
// Reason: a string_view borrows the String's buffer; using it after drop() dangles, but the compiler cannot prove view/owner lifetimes, so a runtime test must cover it.
#include "cplus/cplus.h"
#include "stdlib/string.hp"

int main(void) {
    string s = string("hello");
    string_view v = s.to_view();
    s.drop();
    return v.data()[0];
}
