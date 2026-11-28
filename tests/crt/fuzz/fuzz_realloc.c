#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "safe_alloc.h"
#include "safe_error.h"
#include "safe_log.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    void *p = NULL;
    size_t i;
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    for (i = 0; i < size; ++i) {
        size_t next = (i % 7 == 0) ? SIZE_MAX : ((size_t)data[i] * 17u);
        void *np = realloc_s(p, next);
        if (np != NULL) p = np;
    }
    free_s(p);
    return 0;
}
#ifdef CPLUS_FUZZ_SMOKE_MAIN
int main(void) {
    uint8_t d[16] = {1, 0, 128, 3, 255, 4, 2, 0, 10, 20, 30, 40, 50, 60, 70, 80};
    return LLVMFuzzerTestOneInput(d, sizeof(d));
}
#endif
