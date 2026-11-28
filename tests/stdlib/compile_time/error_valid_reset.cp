// EXPECT: ACCEPT
// Reason: setting an error and then resetting it through both the method
// (clear) and the free helper (error_reset) restores the documented OK state.
#include "cplus/cplus.h"
#include "stdlib/error.hp"

int main(void) {
    error e = error();
    e.set(CPLUS_EXIT_BOUNDS, "bad");

    e.clear();
    if (!e.ok() || e.failed_now() != 0)
        return 1;
    if (e.code() != (int)CPLUS_EXIT_OK ||
        e.category() != STDLIB_CATEGORY_OK)
        return 2;
    if (e.what()[0] != '\0')
        return 3;

    e.set_category(STDLIB_CATEGORY_OWNERSHIP, CPLUS_EXIT_OWNERSHIP, "owned");
    error_reset(&e);
    if (!e.ok() || e.failed_now() != 0)
        return 4;
    if (e.code() != (int)CPLUS_EXIT_OK ||
        e.category() != STDLIB_CATEGORY_OK)
        return 5;
    if (e.what()[0] != '\0')
        return 6;

    error_fail(&e, CPLUS_EXIT_IO, "again");
    error_reset(&e);
    return e.ok() && e.what()[0] == '\0' ? 0 : 7;
}
