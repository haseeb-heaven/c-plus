#include "../safe_alloc.h"
#include "../safe_log.h"
#include "../safe_stats.h"
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

int malloc_s(void **out, size_t bytes) {
    if (!out) { cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "malloc_s", "null output"); cplus_safe_stat_record("malloc_s", 0); return CPLUS_SAFE_EINVAL; }
    *out = NULL;
    if (bytes == 0) bytes = 1;
    void *p = malloc(bytes);
    if (!p) { cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "malloc_s", "oom"); cplus_safe_stat_record("malloc_s", 0); return CPLUS_SAFE_ENOMEM; }
    *out = p;
    cplus_safe_log(CPLUS_SAFE_LOG_DEBUG, "malloc_s", "ok");
    cplus_safe_stat_record("malloc_s", 1);
    return CPLUS_SAFE_OK;
}

int calloc_s(void **out, size_t count, size_t size) {
    if (!out) { cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "calloc_s", "null output"); cplus_safe_stat_record("calloc_s", 0); return CPLUS_SAFE_EINVAL; }
    *out = NULL;
    if (count == 0 || size == 0) { count = 1; size = 1; }
    if (size != 0 && count > SIZE_MAX / size) {
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "calloc_s", "overflow");
        cplus_safe_stat_record("calloc_s", 0);
        return CPLUS_SAFE_EOVERFLOW;
    }
    void *p = calloc(count, size);
    if (!p) { cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "calloc_s", "oom"); cplus_safe_stat_record("calloc_s", 0); return CPLUS_SAFE_ENOMEM; }
    *out = p;
    cplus_safe_log(CPLUS_SAFE_LOG_DEBUG, "calloc_s", "ok");
    cplus_safe_stat_record("calloc_s", 1);
    return CPLUS_SAFE_OK;
}

int realloc_s(void **inout, size_t bytes) {
    if (!inout) { cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "realloc_s", "null inout"); cplus_safe_stat_record("realloc_s", 0); return CPLUS_SAFE_EINVAL; }
    if (bytes == 0) bytes = 1;
    void *p = realloc(*inout, bytes);
    if (!p) { cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "realloc_s", "oom/failed; owner preserved"); cplus_safe_stat_record("realloc_s", 0); return CPLUS_SAFE_ENOMEM; }
    *inout = p;
    cplus_safe_log(CPLUS_SAFE_LOG_DEBUG, "realloc_s", "ok");
    cplus_safe_stat_record("realloc_s", 1);
    return CPLUS_SAFE_OK;
}

void free_s(void **inout) {
    if (!inout || !*inout) { cplus_safe_stat_record("free_s", 1); return; }
    free(*inout);
    *inout = NULL;
    cplus_safe_log(CPLUS_SAFE_LOG_DEBUG, "free_s", "ok");
    cplus_safe_stat_record("free_s", 1);
}
