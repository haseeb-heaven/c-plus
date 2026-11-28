// EXPECT: REJECT
// Cross-function lifetime (P0): make_view() returns a string_view that borrows
// the heap buffer of a local `string`.  The automatic string_drop at function
// exit releases that buffer, so the caller's view dangles.  The compiler must
// reject the escape at the return, not merely observe a bad read at runtime.
#include "cplus/cplus.h"
#include "stdlib/string.hp"

static string_view make_view(void) {
    string s = string("hello");
    return s.to_view();        /* borrow of local s escapes the function */
}

int main(void) {
    string_view v = make_view();
    return v.data()[0];        /* reads the released buffer */
}
