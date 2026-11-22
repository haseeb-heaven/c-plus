// EXPECT: REJECT
// Reason: a NULL file* is not a valid borrow target.  Direct member access
//         through that borrow is a null-pointer lifetime violation and is
//         rejected at compile time with a tagged [CPLUS-LIFETIME] diagnostic.
// Related gap: a borrow of a live local file that escapes its function is
//              also rejected, but with an untagged error and is therefore
//              reported as PARTIAL by the strict harness.
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"

int main(void) {
    file *borrowed = 0;
    return borrowed->stream != NULL;
}
