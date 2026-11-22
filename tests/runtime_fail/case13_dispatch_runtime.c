/* Required Agent-1 test: runtime-controlled function-pointer dispatch.
 * volatile prevents constant folding, so the index is not statically known.
 * Expected: a C+ runtime bounds check aborts with a C+ diagnostic BEFORE the
 * function-pointer load. Currently only Clang UBSan detects it (see
 * docs/libc/DISPATCH_BOUNDS_HANDOFF.md). */
#include <stdio.h>
typedef void (*handler_fn)(void);
static void op_nop(void) { puts("NOP"); }
static void op_read(void) { puts("READ"); }
static void op_exit(void) { puts("EXIT"); }
int main(void) {
    handler_fn dispatch[] = { op_nop, op_read, op_exit };
    volatile size_t cmd = 5556;
    dispatch[cmd]();
    return 0;
}
