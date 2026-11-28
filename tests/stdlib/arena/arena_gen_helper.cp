/* Cross-TU probe for the arena generation counter (C4 regression).
 * Linked as a separate object into arena_reset_overflow; if the counter were
 * a TU-local static, this call would advance a second, independent counter. */
#include <stdint.h>
#include "cplus/cplus.h"
#include "stdlib/arena.hp"

uint64_t arena_helper_next_generation(void) {
    return stdlib_arena_next_generation();
}
