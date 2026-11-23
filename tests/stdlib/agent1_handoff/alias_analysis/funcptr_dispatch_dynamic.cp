/* P0 sound alias analysis — case 4: array of function pointers with a dynamic
 * index (known thin evidence — docs/libc/api_status_matrix.json, row
 * cplus_at_quick_exit: "Handler function-pointer validity and
 * registration-capacity limits are caller responsibility").
 * EXPECT: REJECT (P0 contract; currently ACCEPT — compiler capability gap).
 *
 * REQUESTED: REJECT with a tagged [CPLUS-SAFE-BOUNDS] dynamic guard on the
 * subscript, emitted before the function-pointer load when the extent is known
 * and the index is not statically provable in range
 * (docs/libc/DISPATCH_BOUNDS_HANDOFF.md).
 *
 * This is a compiler requirement: a plain compile-time check has no library
 * mitigation to lean on. A UBSan abort in a built binary is NOT C+ enforcement.
 */
#include <stddef.h>
#include "cplus/cplus.h"

typedef void (*handler_fn)(void);

static void op_nop(void) { }
static void op_read(void) { }
static void op_exit(void) { }

int main(void) {
    handler_fn dispatch[] = { op_nop, op_read, op_exit };
    volatile size_t cmd = 5556; /* not statically provable in [0, 3) */
    dispatch[cmd]();
    return 0;
}
