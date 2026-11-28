/* TU B / main of the multi-TU recursion-guard probe.
 *
 * The callback lives in error_hub_multi_cb.cp (TU A); the outer error_fail is
 * issued here (TU B). Under the documented per-thread guard the callback must
 * run exactly once and the nested report must be dropped. The program prints
 * "MULTI_TU_GUARD <calls> <overflow>" and exits 0 only for 1 / 0.
 *
 * Owned file: tests/stdlib/system_safety/error_hub_multi_main.cp
 */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"

extern int ehm_cb_calls;
extern int ehm_overflow;
void ehm_install(error *target);

int main(void) {
    error ehm_slot = error();
    stdlib_set_error_callback((stdlib_error_callback)0);
    stdlib_alloc_fail_reset();
    stdlib_telemetry_reset();
    stdlib_telemetry_set_enabled(0);

    ehm_install(&ehm_slot);
    error_fail(&ehm_slot, CPLUS_EXIT_IO, "outer-from-tu-b");

    print("MULTI_TU_GUARD", ehm_cb_calls, ehm_overflow);
    print("MULTI_TU_FINAL", ehm_slot.failed_now(), ehm_slot.code());
    stdlib_set_error_callback((stdlib_error_callback)0);
    return (ehm_cb_calls == 1 && ehm_overflow == 0) ? 0 : 1;
}
