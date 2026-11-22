#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "safe_io.h"
#include "safe_error.h"
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    const char *path = "/tmp/cplus_fuzz_stdio.tmp";
    FILE *f = cplus_fopen_s(path, "wb+");
    if (f == NULL) return 0;
    (void)cplus_fwrite_s(data, size, 1, size, f);
    rewind(f);
    uint8_t buf[64];
    (void)cplus_fread_s(buf, sizeof(buf), 1, size < sizeof(buf) ? size : sizeof(buf), f);
    cplus_fclose_s(f);
    unlink(path);
    return 0;
}
#ifdef CPLUS_FUZZ_SMOKE_MAIN
int main(void) { uint8_t d[4] = {1, 2, 3, 4}; return LLVMFuzzerTestOneInput(d, 4); }
#endif
