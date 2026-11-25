#include <stddef.h>
#include <stdint.h>
#include "safe_error.h"
#include "safe_log.h"
#include "safe_memory.h"
#include "safe_stats.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    char d[16];
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    for (size_t i = 0; i < size; ++i) {
        (void)cplus_memcpy_s(i % 2 ? d : NULL, sizeof(d), "abc", (size_t)(data[i] % 32u));
        (void)cplus_safe_set_constraint_mode((cplus_safe_constraint_mode)(data[i] % 5u));
        cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    }
    cplus_safe_stat snap[32];
    (void)cplus_safe_stats_dump(snap, 32);
    return 0;
}
#ifdef CPLUS_FUZZ_SMOKE_MAIN
int main(void) {
    uint8_t d[8] = {0, 1, 2, 17, 255, 4, 8, 12};
    return LLVMFuzzerTestOneInput(d, sizeof(d));
}
#endif
