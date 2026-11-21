#include "../safe_io.h"
#include "../safe_error.h"
#include "../safe_log.h"
#include "../safe_stats.h"
#include <errno.h>

int cplus_fopen_s(FILE **out, const char *name, const char *mode) {
    if (!out) { cplus_safe_stat_record("fopen_s", 0); return CPLUS_SAFE_EINVAL; }
    *out = NULL;
    if (!name || !mode) { cplus_safe_stat_record("fopen_s", 0); return CPLUS_SAFE_EINVAL; }
    FILE *f = fopen(name, mode);
    if (!f) { cplus_safe_log(CPLUS_SAFE_LOG_WARN, "fopen_s", "open failed"); cplus_safe_stat_record("fopen_s", 0); return errno ? errno : CPLUS_SAFE_EINVAL; }
    *out = f;
    cplus_safe_log(CPLUS_SAFE_LOG_DEBUG, "fopen_s", "ok");
    cplus_safe_stat_record("fopen_s", 1);
    return CPLUS_SAFE_OK;
}
int cplus_fclose_s(FILE **inout) {
    if (!inout || !*inout) { cplus_safe_stat_record("fclose_s", 0); return CPLUS_SAFE_EINVAL; }
    int rc = fclose(*inout);
    *inout = NULL;
    if (rc != 0) { cplus_safe_stat_record("fclose_s", 0); return errno ? errno : CPLUS_SAFE_EINVAL; }
    cplus_safe_log(CPLUS_SAFE_LOG_DEBUG, "fclose_s", "ok");
    cplus_safe_stat_record("fclose_s", 1);
    return CPLUS_SAFE_OK;
}
size_t cplus_fread_s(void *buf, size_t bmax, size_t size, size_t count, FILE *stream) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "fread_s", "enter");
    if (!buf || !stream || size == 0 || count == 0) { cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "fread_s", "invalid args"); cplus_safe_stat_record("fread_s", 0); return 0; }
    if (count > bmax / size) count = bmax / size;
    size_t r = fread(buf, size, count, stream);
    cplus_safe_log(CPLUS_SAFE_LOG_DEBUG, "fread_s", "ok");
    cplus_safe_stat_record("fread_s", 1);
    return r;
}
size_t cplus_fwrite_s(const void *buf, size_t bmax, size_t size, size_t count, FILE *stream) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "fwrite_s", "enter");
    if (!buf || !stream || size == 0 || count == 0) { cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "fwrite_s", "invalid args"); cplus_safe_stat_record("fwrite_s", 0); return 0; }
    if (count > bmax / size) count = bmax / size;
    size_t r = fwrite(buf, size, count, stream);
    cplus_safe_log(CPLUS_SAFE_LOG_DEBUG, "fwrite_s", "ok");
    cplus_safe_stat_record("fwrite_s", 1);
    return r;
}
