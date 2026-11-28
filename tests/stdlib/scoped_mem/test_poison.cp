/* Deterministic regression for scope release:
 *   debug   -> popped bytes are exactly 0xDD (stale reads fail immediately)
 *   release -> popped bytes are untouched and the rewind is one store.
 * Built twice by run.sh (default and -DNDEBUG); prints POISON_TEST <fails>.
 */
#include "cplus/cplus.h"
#include "stdlib/datastack.hp"
#include "stdlib/scoped_mem.hp"

int main(void) {
    error e = error();
    stdlib_datastack ds;
    stdlib_scratch_mark m;
    unsigned char *p;
    int poison;
    int fails = 0;
    size_t i;

    stdlib_set_error_callback((stdlib_error_callback)0);
    poison = stdlib_datastack_poison_enabled();
    if (stdlib_datastack_init(&ds, 1024u, &e) != STDLIB_OK) return 2;

    /* Explicit mark/leave path. */
    m = stdlib_scratch_enter(&ds);
    p = (unsigned char *)stdlib_datastack_alloc(&ds, 64u, 16u, &e);
    if (p == (unsigned char *)0) fails++;
    else {
        for (i = 0; i < 64u; i++) p[i] = 0x5Au;
        stdlib_scratch_leave(&ds, m);
        if (ds.top != m.saved_top) fails++;
        for (i = 0; i < 64u; i++)
            if (p[i] != (poison ? (unsigned char)0xDDu : (unsigned char)0x5Au)) fails++;
    }

    /* RAII scope guard path. */
    {
        stdlib_scratch_scope guard = stdlib_scratch_scope(&ds);
        unsigned char *q = (unsigned char *)stdlib_datastack_alloc(&ds, 32u, 8u, &e);
        if (q == (unsigned char *)0) fails++;
        else {
            for (i = 0; i < 32u; i++) q[i] = 0x11u;
            guard.drop();
            if (ds.top != ds.base) fails++;
            for (i = 0; i < 32u; i++)
                if (q[i] != (poison ? (unsigned char)0xDDu : (unsigned char)0x11u)) fails++;
        }
    }

    stdlib_datastack_destroy(&ds, &e);
    print("POISON_TEST", fails);
    return fails ? 1 : 0;
}
