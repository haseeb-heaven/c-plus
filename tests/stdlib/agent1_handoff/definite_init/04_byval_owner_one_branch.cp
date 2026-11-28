// EXPECT: REJECT
// A class-owned object (by-value RAII `string`) is explicitly initialized on
// only one branch, then consumed after the join.  Definite-initialization
// analysis should require an initializer on every path that reaches the read.
//
// NOTE: C+ zero-initializes by-value class objects, so today this read is
// memory-safe and the compiler stays silent.  This case measures whether a
// definite-init rule for class-typed owners is enforced.
#include "cplus/cplus.h"
#include "stdlib/string.hp"

int g_condition;

int main(void) {
    string owner;
    if (g_condition)
        owner = string("initialized");
    return owner.size(); /* owner never explicitly initialized on false path */
}
