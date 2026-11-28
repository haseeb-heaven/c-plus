// EXPECT: REJECT
// Reason: using a tracked borrow of a String after cplus_move() transfers its owner is use-after-move.
#include "cplus/cplus.h"
#include "stdlib/string.hp"

int main(void) {
    string a = string("hello");
    string *borrowed = &a;
    string b = cplus_move(a);
    return borrowed->size() == b.size() ? 0 : 1;
}
