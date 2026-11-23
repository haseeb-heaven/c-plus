/* Required Agent-1 test: runtime-controlled function-pointer dispatch.
 * volatile prevents constant folding, so the index is not statically known.
 * Expected: a C+ runtime bounds check aborts with a C+ diagnostic BEFORE the
 * function-pointer load. Currently only Clang UBSan detects it (see
 * docs/libc/DISPATCH_BOUNDS_HANDOFF.md). */
#include <stdio.h>
#include <stdlib.h>
typedef void (*handler_fn)(void);
static void op_nop(void) { puts("NOP"); }
static void op_read(void) { puts("READ"); }
static void op_exit(void) { puts("EXIT"); }
int main(void) {
    handler_fn dispatch[] = { op_nop, op_read, op_exit };
    /* Derive the index from a runtime environment value so the compiler
     * cannot constant-fold it; default keeps the original 5556 probe. */
    const char *env = getenv("CPLUS_DISPATCH_INDEX");
    volatile size_t cmd = env != NULL ? (size_t)strtoul(env, NULL, 10) : 5556u;
    dispatch[cmd]();
    return 0;
}
