#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "../../log/log.h"
#include "stdio_internal.h"

#define CPLUS_STDIO_TRACK_CAP 256u
/* The closed table is deliberately larger than the old 256-entry ring: a
 * correctly sized open-addressed table plus tombstones keeps double-close
 * markers across ordinary churn instead of silently wrapping. */
#define CPLUS_STDIO_CLOSED_CAP 512u

static pthread_mutex_t g_stdio_lock = PTHREAD_MUTEX_INITIALIZER;
/* Live streams registered by the wrapped creators are kept in a compact
 * linear array (swap-with-last deletion); it is not open addressed. */
static FILE *g_live_streams[CPLUS_STDIO_TRACK_CAP];
static size_t g_live_stream_count = 0;

/* Closed streams are kept in a fixed-size open-addressed table.  Deletion
 * writes a tombstone, never NULL, so probe chains survive a close followed by
 * a reopen at the same bucket; a plain 256-entry ring silently wrapped and
 * forgot the oldest double-close markers.  Capacity policy: when the table
 * has neither a free slot nor a reclaimable tombstone, cplus_fclose() refuses
 * the close (errno = EMFILE) instead of evicting an arbitrary marker, which
 * could re-enable a second host fclose() on a freed FILE*. */
static FILE *g_closed_streams[CPLUS_STDIO_CLOSED_CAP];
static FILE g_closed_tombstone; /* never dereferenced; distinct from any FILE* */
#define CLOSED_TOMBSTONE (&g_closed_tombstone)
static size_t g_closed_count;
static size_t g_closed_tombstones;

static size_t closed_hash(const FILE *f) {
    uintptr_t x = (uintptr_t)f;
    x ^= x >> 33;
    x *= (uintptr_t)0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    return (size_t)(x & (CPLUS_STDIO_CLOSED_CAP - 1u));
}

static size_t closed_find(const FILE *stream) {
    size_t mask = CPLUS_STDIO_CLOSED_CAP - 1u;
    size_t i;
    if (!stream) return CPLUS_STDIO_CLOSED_CAP;
    i = closed_hash(stream);
    for (size_t n = 0; n < CPLUS_STDIO_CLOSED_CAP; ++n) {
        if (g_closed_streams[i] == NULL) return CPLUS_STDIO_CLOSED_CAP;
        if (g_closed_streams[i] == stream) return i;
        i = (i + 1u) & mask;
    }
    return CPLUS_STDIO_CLOSED_CAP;
}

static void closed_insert_raw(FILE *stream) {
    size_t mask = CPLUS_STDIO_CLOSED_CAP - 1u;
    size_t i = closed_hash(stream);
    while (g_closed_streams[i] != NULL) i = (i + 1u) & mask;
    g_closed_streams[i] = stream;
}

static void closed_compact(void) {
    FILE *fresh[CPLUS_STDIO_CLOSED_CAP];
    memset(fresh, 0, sizeof fresh);
    for (size_t i = 0; i < CPLUS_STDIO_CLOSED_CAP; ++i) {
        FILE *v = g_closed_streams[i];
        if (v != NULL && v != CLOSED_TOMBSTONE) {
            size_t mask = CPLUS_STDIO_CLOSED_CAP - 1u;
            size_t j = closed_hash(v);
            while (fresh[j] != NULL) j = (j + 1u) & mask;
            fresh[j] = v;
        }
    }
    memcpy(g_closed_streams, fresh, sizeof fresh);
    g_closed_tombstones = 0u;
    /* g_closed_count is unchanged by compaction. */
}

/* Returns 1 on success (including already-present), 0 at documented capacity. */
static int closed_add(FILE *stream) {
    size_t mask = CPLUS_STDIO_CLOSED_CAP - 1u;
    size_t i, first_tomb = CPLUS_STDIO_CLOSED_CAP;
    if (!stream) return 0;
    i = closed_hash(stream);
    for (size_t n = 0; n < CPLUS_STDIO_CLOSED_CAP; ++n) {
        FILE *v = g_closed_streams[i];
        if (v == stream) return 1;
        if (v == CLOSED_TOMBSTONE) {
            if (first_tomb == CPLUS_STDIO_CLOSED_CAP) first_tomb = i;
        } else if (v == NULL) {
            size_t at = (first_tomb != CPLUS_STDIO_CLOSED_CAP) ? first_tomb : i;
            if (first_tomb != CPLUS_STDIO_CLOSED_CAP) --g_closed_tombstones;
            g_closed_streams[at] = stream;
            ++g_closed_count;
            return 1;
        }
        i = (i + 1u) & mask;
    }
    if (g_closed_tombstones != 0u) {
        closed_compact();
        closed_insert_raw(stream);
        ++g_closed_count;
        return 1;
    }
    return 0;
}

static void closed_remove(FILE *stream) {
    size_t i = closed_find(stream);
    if (i == CPLUS_STDIO_CLOSED_CAP) return;
    g_closed_streams[i] = CLOSED_TOMBSTONE;
    --g_closed_count;
    ++g_closed_tombstones;
}

/* Public-to-this-source: used by stdio3.c for tmpfile/fdopen/freopen too. */
void cplus_stdio_register(FILE *stream) {
    if (!stream) return;
    pthread_mutex_lock(&g_stdio_lock);
    /* A new object at a recycled address must clear any stale closed marker;
     * otherwise the first close of the new stream is falsely rejected. */
    closed_remove(stream);
    int found = 0;
    for (size_t i = 0; i < g_live_stream_count; ++i) {
        if (g_live_streams[i] == stream) { found = 1; break; }
    }
    if (!found && g_live_stream_count < CPLUS_STDIO_TRACK_CAP) {
        g_live_streams[g_live_stream_count++] = stream;
    }
    pthread_mutex_unlock(&g_stdio_lock);
}

FILE *cplus_fopen(const char *path, const char *mode) {
    cplus_libc_log("fopen","enter");
    if (!path || !mode) { errno = EINVAL; return NULL; }
    FILE *fp = fopen(path, mode);
    if (fp) cplus_stdio_register(fp);
    return fp;
}

int cplus_fclose(FILE *stream) {
    cplus_libc_log("fclose","enter");
    if (!stream) { errno = EINVAL; return EOF; }
    pthread_mutex_lock(&g_stdio_lock);
    int was_live = 0;
    for (size_t i = 0; i < g_live_stream_count; ++i) {
        if (g_live_streams[i] == stream) {
            g_live_streams[i] = g_live_streams[--g_live_stream_count];
            was_live = 1;
            break;
        }
    }
    if (closed_find(stream) != CPLUS_STDIO_CLOSED_CAP) {
        /* Second close of an address already recorded as closed. */
        if (was_live) {
            /* Inconsistent state: restore liveness and fail closed. */
            g_live_streams[g_live_stream_count++] = stream;
        }
        pthread_mutex_unlock(&g_stdio_lock);
        errno = EBADF;
        return EOF;
    }
    if (!closed_add(stream)) {
        /* Capacity policy: refusing to record the marker is safer than
         * evicting one and letting a later double close reach host fclose. */
        if (was_live) g_live_streams[g_live_stream_count++] = stream;
        pthread_mutex_unlock(&g_stdio_lock);
        errno = EMFILE;
        return EOF;
    }
    pthread_mutex_unlock(&g_stdio_lock);
    return fclose(stream);
}

size_t cplus_fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    cplus_libc_log("fread","enter");
    if (!stream || (size > 0 && nmemb > 0 && !ptr)) { errno = EINVAL; return 0; }
    if (size > 0 && nmemb > SIZE_MAX / size) { errno = EOVERFLOW; return 0; }
    return fread(ptr, size, nmemb, stream);
}

size_t cplus_fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    cplus_libc_log("fwrite","enter");
    if (!stream || (size > 0 && nmemb > 0 && !ptr)) { errno = EINVAL; return 0; }
    if (size > 0 && nmemb > SIZE_MAX / size) { errno = EOVERFLOW; return 0; }
    return fwrite(ptr, size, nmemb, stream);
}

int cplus_vsnprintf(char *str, size_t size, const char *fmt, va_list ap) {
    cplus_libc_log("vsnprintf","enter");
    if (!fmt || (size > 0 && !str)) { errno = EINVAL; return -1; }
    return vsnprintf(str, size, fmt, ap);
}

int cplus_snprintf(char *str, size_t size, const char *fmt, ...) {
    cplus_libc_log("snprintf","enter");
    if (!fmt || (size > 0 && !str)) { errno = EINVAL; return -1; }
    va_list ap; va_start(ap, fmt); int r = vsnprintf(str, size, fmt, ap); va_end(ap); return r;
}

int cplus_printf(const char *fmt, ...) {
    cplus_libc_log("printf","enter");
    if (!fmt) { errno = EINVAL; return -1; }
    va_list ap; va_start(ap, fmt); int r = vprintf(fmt, ap); va_end(ap); return r;
}

int cplus_fprintf(FILE *stream, const char *fmt, ...) {
    cplus_libc_log("fprintf","enter");
    if (!stream || !fmt) { errno = EINVAL; return -1; }
    va_list ap; va_start(ap, fmt); int r = vfprintf(stream, fmt, ap); va_end(ap); return r;
}

int cplus_scanf(const char *fmt, ...) {
    cplus_libc_log("scanf","enter");
    if (!fmt) { errno = EINVAL; return EOF; }
    va_list ap; va_start(ap, fmt); int r = vscanf(fmt, ap); va_end(ap); return r;
}

int cplus_fscanf(FILE *stream, const char *fmt, ...) {
    cplus_libc_log("fscanf","enter");
    if (!stream || !fmt) { errno = EINVAL; return EOF; }
    va_list ap; va_start(ap, fmt); int r = vfscanf(stream, fmt, ap); va_end(ap); return r;
}

int cplus_sprintf(char *str, const char *fmt, ...) {
    cplus_libc_log("sprintf","enter");
    if (!str || !fmt) { errno = EINVAL; return -1; }
    va_list ap; va_start(ap, fmt); int r = vsprintf(str, fmt, ap); va_end(ap); return r;
}

int cplus_vsprintf(char *str, const char *fmt, va_list ap) {
    cplus_libc_log("vsprintf","enter");
    if (!str || !fmt) { errno = EINVAL; return -1; }
    return vsprintf(str, fmt, ap);
}

int cplus_sscanf(const char *str, const char *fmt, ...) {
    cplus_libc_log("sscanf","enter");
    if (!str || !fmt) { errno = EINVAL; return EOF; }
    va_list ap; va_start(ap, fmt); int r = vsscanf(str, fmt, ap); va_end(ap); return r;
}

int cplus_vsscanf(const char *str, const char *fmt, va_list ap) {
    cplus_libc_log("vsscanf","enter");
    if (!str || !fmt) { errno = EINVAL; return EOF; }
    return vsscanf(str, fmt, ap);
}

int cplus_vfprintf(FILE *stream, const char *fmt, va_list ap) {
    cplus_libc_log("vfprintf","enter");
    if (!stream || !fmt) { errno = EINVAL; return -1; }
    return vfprintf(stream, fmt, ap);
}

int cplus_vprintf(const char *fmt, va_list ap) {
    cplus_libc_log("vprintf","enter");
    if (!fmt) { errno = EINVAL; return -1; }
    return vprintf(fmt, ap);
}

int cplus_vfscanf(FILE *stream, const char *fmt, va_list ap) {
    cplus_libc_log("vfscanf","enter");
    if (!stream || !fmt) { errno = EINVAL; return EOF; }
    return vfscanf(stream, fmt, ap);
}
