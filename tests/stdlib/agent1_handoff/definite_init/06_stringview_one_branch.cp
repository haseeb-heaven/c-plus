// EXPECT: REJECT
// A `string_view` is declared with no explicit initialization and assigned
// only on the true branch, then read after the join.  Definite initialization
// should reject the path with no view assignment.
//
// NOTE: `string_view` is a by-value class in C+, so it is zero-initialized
// today and this case is currently accepted (silent).
#include "cplus/cplus.h"
#include "stdlib/string.hp"

int g_condition;

int main(void) {
    string_view view; /* no explicit initializer */
    if (g_condition)
        view = string_view("ok", 2);
    return view.size(); /* unbound view on the false path */
}
