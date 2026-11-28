/* Safe variant of cve_like/case_13.c -- out-of-bounds jump-table dispatch.
 *
 * Default build:  C+ SafeCRT layer  (stdlib/safe/safe_c.h)
 * -DPLAIN_LIBC:   plain hardened libc layer (cplus_libc_safe.h)
 *
 * WHY THIS IS NOT FIXABLE BY LIBC
 * -------------------------------
 * The original does `d[cmd]()` with an untrusted `cmd` and no bounds check.
 * Any cmd >= 3 reads a function pointer outside the 3-entry stack table and
 * calls garbage. No allocator or memory wrapper can make a raw indirect call
 * through an attacker-chosen index safe: the CALLER must bound `cmd` before
 * dispatch. That check lives in this file and does not rely on libc at all.
 *
 * Example:  printf '99\n' | ./case_13_safe   -> rejected, exit 0.
 */
#ifdef PLAIN_LIBC
#include "cplus_libc_safe.h"
#else
#include "stdlib/safe/safe_c.h"
#endif
#include <stddef.h>
#include <stdio.h>

typedef void (*fn)(void);

static void op_nop(void)  { puts("NOP"); }
static void op_read(void) { puts("READ"); }
static void op_exit(void) { puts("EXIT"); }

int main(void) {
    fn d[] = { op_nop, op_read, op_exit };
    const size_t count = sizeof(d) / sizeof(d[0]); /* 3 */
    size_t cmd = 0;

    printf("cmd: ");
    if (scanf("%zu", &cmd) != 1) {
        printf("OUTCOME: no command supplied; nothing dispatched (safe)\n");
        return 0;
    }

    /* Caller-side bounds check: require cmd < 3 before touching the table.
     * `count` is derived from the table itself so the two cannot drift. */
    if (cmd >= 3 || cmd >= count) {
        printf("OUTCOME: rejected cmd=%zu (>= 3); no out-of-bounds dispatch (safe)\n", cmd);
        return 0;
    }

    d[cmd](); /* reachable only for cmd in [0,3): proven in-bounds */
    printf("OUTCOME: dispatched cmd=%zu within [0,3) (safe)\n", cmd);
    return 0;
}
