#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include "safe_io.h"
#include "safe_log.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    const char *path = "/tmp/cplus_fuzz_life.tmp";
    FILE *f = cplus_fopen_s(path, "wb+");
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    if (f) {
        char rb[16] = {0};
        (void)cplus_fwrite_s(data, size, 1, size, f);
        rewind(f);
        (void)cplus_fread_s(rb, sizeof(rb), 1, sizeof(rb), f);
        (void)cplus_fclose_s(f);
        (void)cplus_fclose_s(f); /* double close under fuzz */
        unlink(path);
    }
    return 0;
}
#ifdef CPLUS_FUZZ_SMOKE_MAIN
int main(void) {
    uint8_t d[6] = {'a','b','c','d','e','f'};
    return LLVMFuzzerTestOneInput(d, sizeof(d));
}
#endif
