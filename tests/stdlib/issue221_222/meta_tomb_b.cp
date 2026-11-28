/* Translation unit B for issue #222: mirrors TU A.  With a per-TU `static`
 * tombstone address, TU B cannot recognise TU A's tombstones (and vice
 * versa), even though both mutate the same weak stdlib_state.meta_tab. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"

static int keys_b[32];

int tu2_phase(void) {
    int i;
    for (i = 0; i < 32; ++i)
        stdlib_meta_record((const void *)&keys_b[i], 8u, 1u);
    for (i = 0; i < 16; ++i)
        stdlib_meta_forget((const void *)&keys_b[i]);
    return (stdlib_state.meta_live == 16u && stdlib_state.meta_used == 32u)
               ? 0 : 1;
}

int tu2_count_tombs(void) {
    size_t i;
    int n = 0;
    for (i = 0; i < stdlib_state.meta_cap; ++i)
        if (stdlib_state.meta_tab[i].ptr == STDLIB_META_TOMB)
            ++n;
    return n;
}
