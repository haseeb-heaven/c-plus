#include <stddef.h>
#include <stdint.h>
#include "safe_alloc.h"
#include "safe_log.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    void *a = NULL, *b = NULL;
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    for (size_t i = 0; i < size; ++i) {
        if (!a) a = malloc_s((size_t)(data[i] % 64u) + 1u);
        (void)cplus_safe_transfer_ownership(&b, &a);
        (void)cplus_safe_ptr_is_live(b);
        if (b && (data[i] & 1u)) { free_s(b); b = NULL; }
    }
    free_s(a);
    free_s(b);
    return 0;
}
#ifdef CPLUS_FUZZ_SMOKE_MAIN
int main(void) {
    uint8_t d[6] = {10, 20, 31, 40, 51, 60};
    return LLVMFuzzerTestOneInput(d, sizeof(d));
}
#endif
