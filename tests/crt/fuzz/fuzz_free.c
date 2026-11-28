#include <stddef.h>
#include <stdint.h>
#include "safe_alloc.h"
#include "safe_log.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    int local = 0;
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    for (size_t i = 0; i < size; ++i) {
        uint8_t op = data[i] % 4u;
        if (op == 0) {
            void *p = malloc_s((size_t)data[i] + 1u);
            (void)free_checked_s(p);
            (void)free_checked_s(p); /* double free attempt */
        } else if (op == 1) {
            (void)free_checked_s(&local);
        } else if (op == 2) {
            (void)free_checked_s(NULL);
        } else {
            char *p = (char *)malloc_s(32);
            if (p) { (void)free_checked_s(p + 1); free_s(p); }
        }
    }
    return 0;
}
#ifdef CPLUS_FUZZ_SMOKE_MAIN
int main(void) {
    uint8_t d[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    return LLVMFuzzerTestOneInput(d, sizeof(d));
}
#endif
