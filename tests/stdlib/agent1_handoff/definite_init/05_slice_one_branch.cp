// EXPECT: REJECT
// A borrowed `slice` is declared with no explicit initialization and assigned
// only on the true branch, then its size() is read after the join.  Definite
// initialization should reject every path that reaches the read without a
// bound slice value.
//
// NOTE: `slice` is a by-value class in C+, so it is zero-initialized today and
// this case is currently accepted (silent).
#include "cplus/cplus.h"
#include "stdlib/vector.hp"

int g_condition;

int main(void) {
    slice view; /* no explicit initializer */
    vector v = vector();
    v.push_back(1);
    if (g_condition)
        view = v.view();
    return view.size(); /* unbound slice on the false path */
}
