// EXPECT: REJECT
// Reason: reading through an uninitialized String owner is a C+ uninitialized read.
#include "cplus/cplus.h"
#include "stdlib/string.hp"

int main(void) {
    /* A by-value `string s;` is zero-initialised by C+; a managed handle to a
     * String is the case where the owner really is uninitialised. */
    managed string *s;
    return s->at(0);
}
