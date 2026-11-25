/* TU A of the multi-TU recursion-guard probe.
 *
 * Installs a callback, defined here, that re-enters error_fail() from THIS
 * translation unit. TU B raises the outer failure. error.hp documents "a
 * per-thread re-entrancy guard drops reports raised from inside the callback";
 * that contract is translation-unit independent, so a nested report must be
 * dropped regardless of which TU issued the outer call.
 *
 * Owned file: tests/stdlib/system_safety/error_hub_multi_cb.cp
 */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"

int ehm_cb_calls = 0;
int ehm_overflow = 0;
static int ehm_depth = 0;
static error *ehm_target = (error *)0;

static void ehm_cb(int category, cplus_exit_code code, const char *message) {
    (void)category;
    (void)code;
    (void)message;
    ehm_cb_calls++;
    if (ehm_depth != 0) {
        ehm_overflow++;
        return;
    }
    ehm_depth++;
    if (ehm_target != (error *)0)
        error_fail(ehm_target, CPLUS_EXIT_RESOURCE, "nested-from-tu-a");
    ehm_depth--;
}

void ehm_install(error *target) {
    ehm_target = target;
    stdlib_set_error_callback(ehm_cb);
}
