/* Translation unit A for issue #222: records and frees half of its keys,
 * writing tombstones into the shared metadata table.  It also counts the
 * tombstones it can recognise, which must include those written by TU B. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"

static int keys_a[32];

int tu1_phase(void) {
    int i;
    for (i = 0; i < 32; ++i)
        stdlib_meta_record((const void *)&keys_a[i], (size_t)(i + 1), 1u);
    for (i = 0; i < 16; ++i)
        stdlib_meta_forget((const void *)&keys_a[i]);
    return (stdlib_state.meta_live == 16u && stdlib_state.meta_used == 32u)
               ? 0 : 1;
}

int tu1_count_tombs(void) {
    size_t i;
    int n = 0;
    for (i = 0; i < stdlib_state.meta_cap; ++i)
        if (stdlib_state.meta_tab[i].ptr == STDLIB_META_TOMB)
            ++n;
    return n;
}
