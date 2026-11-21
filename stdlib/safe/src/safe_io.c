#include "../safe_io.h"
#include "../safe_error.h"
#include "../safe_log.h"
#include <errno.h>

int cplus_fopen_s(FILE **out, const char *name, const char *mode) {
    if (!out) return CPLUS_SAFE_EINVAL;
    *out = NULL;
    if (!name || !mode) return CPLUS_SAFE_EINVAL;
    FILE *f = fopen(name, mode);
    if (!f) { cplus_safe_log(CPLUS_SAFE_LOG_WARN, "fopen_s", "open failed"); return errno ? errno : CPLUS_SAFE_EINVAL; }
    *out = f;
    cplus_safe_log(CPLUS_SAFE_LOG_DEBUG, "fopen_s", "ok");
    return CPLUS_SAFE_OK;
}
int cplus_fclose_s(FILE **inout) {
    if (!inout || !*inout) return CPLUS_SAFE_EINVAL;
    int rc = fclose(*inout);
    *inout = NULL;
    if (rc != 0) return errno ? errno : CPLUS_SAFE_EINVAL;
    cplus_safe_log(CPLUS_SAFE_LOG_DEBUG, "fclose_s", "ok");
    return CPLUS_SAFE_OK;
}
size_t cplus_fread_s(void *buf, size_t bmax, size_t size, size_t count, FILE *stream) {
    if (!buf || !stream || size == 0 || count == 0) return 0;
    if (count > bmax / size) count = bmax / size;
    return fread(buf, size, count, stream);
}
size_t cplus_fwrite_s(const void *buf, size_t bmax, size_t size, size_t count, FILE *stream) {
    if (!buf || !stream || size == 0 || count == 0) return 0;
    if (count > bmax / size) count = bmax / size;
    return fwrite(buf, size, count, stream);
}
