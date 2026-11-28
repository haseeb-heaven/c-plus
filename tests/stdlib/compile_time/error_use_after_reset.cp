// EXPECT: RUNTIME
// Reason: error_reset()/clear() clear the fields but leave the error object
// alive, so reading it after a reset is memory-safe. The compiler has no
// "reset state" for the lightweight error class and cannot reject a stale-state
// misuse. Cover this pattern in a runtime/behavioral test instead.
#include "cplus/cplus.h"
#include "stdlib/error.hp"

int main(void) {
    error e = error();
    e.set(CPLUS_EXIT_IO, "boom");
    const char *cached = e.what();

    e.clear(); /* reset: object stays valid, message buffer becomes empty */
    if (e.ok() == 0 || e.failed_now() != 0)
        return 1;
    if (e.code() != (int)CPLUS_EXIT_OK)
        return 2;
    if (e.category() != STDLIB_CATEGORY_OK)
        return 3;

    error_reset(&e); /* idempotent reset is also valid */
    if (cached[0] != '\0') /* cached pointer still aliases e.text */
        return 4;
    return 0;
}
