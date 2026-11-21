#include "../safe_alloc.h"
#include "../safe_log.h"
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

int malloc_s(void **out, size_t bytes) {
    if (!out) { cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "malloc_s", "null output"); return CPLUS_SAFE_EINVAL; }
    *out = NULL;
    if (bytes == 0) bytes = 1;
    void *p = malloc(bytes);
    if (!p) { cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "malloc_s", "oom"); return CPLUS_SAFE_ENOMEM; }
    *out = p;
    cplus_safe_log(CPLUS_SAFE_LOG_DEBUG, "malloc_s", "ok");
    return CPLUS_SAFE_OK;
}

int calloc_s(void **out, size_t count, size_t size) {
    if (!out) { cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "calloc_s", "null output"); return CPLUS_SAFE_EINVAL; }
    *out = NULL;
    if (count == 0 || size == 0) { count = 1; size = 1; }
    if (size != 0 && count > SIZE_MAX / size) {
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "calloc_s", "overflow");
        return CPLUS_SAFE_EOVERFLOW;
    }
    void *p = calloc(count, size);
    if (!p) { cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "calloc_s", "oom"); return CPLUS_SAFE_ENOMEM; }
    *out = p;
    cplus_safe_log(CPLUS_SAFE_LOG_DEBUG, "calloc_s", "ok");
    return CPLUS_SAFE_OK;
}

int realloc_s(void **inout, size_t bytes) {
    if (!inout) { cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "realloc_s", "null inout"); return CPLUS_SAFE_EINVAL; }
    if (bytes == 0) bytes = 1;
    void *p = realloc(*inout, bytes);
    if (!p) { cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "realloc_s", "oom/failed; owner preserved"); return CPLUS_SAFE_ENOMEM; }
    *inout = p;
    cplus_safe_log(CPLUS_SAFE_LOG_DEBUG, "realloc_s", "ok");
    return CPLUS_SAFE_OK;
}

void free_s(void **inout) {
    if (!inout || !*inout) return;
    free(*inout);
    *inout = NULL;
    cplus_safe_log(CPLUS_SAFE_LOG_DEBUG, "free_s", "ok");
}
