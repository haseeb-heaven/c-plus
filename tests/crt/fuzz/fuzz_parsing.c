#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "safe_string.h"
#include "safe_log.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    char buf[128];
    char *ctx = NULL;
    rsize_t dmax;
    size_t n = size < sizeof(buf) - 1 ? size : sizeof(buf) - 1;
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    memcpy(buf, data, n);
    buf[n] = '\0';
    dmax = n + 1;
    while (cplus_strtok_s(ctx ? NULL : buf, &dmax, ",;:\t ", &ctx) != NULL) {
    }
    return 0;
}
#ifdef CPLUS_FUZZ_SMOKE_MAIN
int main(void) {
    const char *s = "one,two;three:four";
    return LLVMFuzzerTestOneInput((const uint8_t *)s, strlen(s));
}
#endif
