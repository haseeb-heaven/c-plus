// EXPECT: ACCEPT
// Reason: constructing an error, setting it through the public API and
// reading its status/category/message is a valid lifetime with no misuse.
#include "cplus/cplus.h"
#include "stdlib/error.hp"

int main(void) {
    error e = error();
    if (!e.ok() || e.failed_now() != 0)
        return 1;
    if (e.code() != (int)CPLUS_EXIT_OK || e.category() != STDLIB_CATEGORY_OK)
        return 2;

    e.set(CPLUS_EXIT_IO, "io failed");
    if (e.ok() || !e.failed_now())
        return 3;
    if (e.code() != (int)CPLUS_EXIT_IO)
        return 4;
    if (e.category() != STDLIB_CATEGORY_IO)
        return 5;
    if (e.what()[0] != 'i')
        return 6;

    e.set_category(STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS, "range");
    if (e.category() != STDLIB_CATEGORY_RANGE ||
        e.code() != (int)CPLUS_EXIT_BOUNDS)
        return 7;
    return 0;
}
