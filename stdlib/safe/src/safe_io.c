#define _POSIX_C_SOURCE 200112L
#define CPLUS_SAFE_NO_UNPREFIXED_MACROS
#include "../safe_io.h"
#include "../safe_error.h"
#include "../safe_log.h"
#include "../safe_stats.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>

#define STREAM_CACHE_SLOTS 512u

static pthread_mutex_t g_io_lock = PTHREAD_MUTEX_INITIALIZER;
static FILE *g_live_streams[STREAM_CACHE_SLOTS];
static FILE *g_closed_streams[STREAM_CACHE_SLOTS];

static size_t hash_stream(const FILE *f) {
    uintptr_t x = (uintptr_t)f;
    x ^= x >> 33;
    x *= (uintptr_t)0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    return (size_t)(x & (STREAM_CACHE_SLOTS - 1u));
}

/* Open addressing: a single-slot-per-hash cache misclassified a real double
 * close as an invalid free whenever two streams collided. Linear probing gives
 * each stream its own slot until the table is full. */
static size_t stream_find(FILE **table, const FILE *f) {
    size_t mask = STREAM_CACHE_SLOTS - 1u;
    size_t i = hash_stream(f);
    if (!f) return STREAM_CACHE_SLOTS;
    for (size_t n = 0; n < STREAM_CACHE_SLOTS; ++n) {
        if (table[i] == NULL) return STREAM_CACHE_SLOTS;
        if (table[i] == f) return i;
        i = (i + 1u) & mask;
    }
    return STREAM_CACHE_SLOTS;
}
static void stream_add(FILE **table, FILE *f) {
    size_t mask = STREAM_CACHE_SLOTS - 1u;
    size_t start = hash_stream(f), i = start;
    if (!f) return;
    for (size_t n = 0; n < STREAM_CACHE_SLOTS; ++n) {
        if (table[i] == NULL || table[i] == f) { table[i] = f; return; }
        i = (i + 1u) & mask;
    }
    table[start] = f; /* full: bounded diagnostic only */
}
static int stream_remove(FILE **table, FILE *f) {
    size_t i = stream_find(table, f);
    if (i == STREAM_CACHE_SLOTS) return 0;
    table[i] = NULL;
    return 1;
}

FILE *cplus_fopen_s(const char *name, const char *mode) {
    FILE *f;
    if (!name || !mode) {
        errno = EINVAL;
        cplus_safe_stat_record("fopen_s", 0);
        return NULL;
    }
    f = fopen(name, mode);
    if (!f) {
        cplus_safe_log(CPLUS_SAFE_LOG_WARN, "fopen_s", "open failed");
        cplus_safe_stat_record("fopen_s", 0);
        return NULL;
    }
    pthread_mutex_lock(&g_io_lock);
    stream_remove(g_closed_streams, f);
    stream_add(g_live_streams, f);
    pthread_mutex_unlock(&g_io_lock);
    cplus_safe_log(CPLUS_SAFE_LOG_DEBUG, "fopen_s", "ok");
    cplus_safe_stat_record("fopen_s", 1);
    return f;
}

int cplus_fclose_s(FILE *stream) {
    int was_live = 0;
    int already_closed = 0;
    int rc;
    if (!stream) {
        errno = EINVAL;
        cplus_safe_stat_record("fclose_s", 0);
        return EOF;
    }
    pthread_mutex_lock(&g_io_lock);
    was_live = stream_remove(g_live_streams, stream);
    if (!was_live && stream_find(g_closed_streams, stream) < STREAM_CACHE_SLOTS)
        already_closed = 1;
    if (!already_closed)
        stream_add(g_closed_streams, stream);
    pthread_mutex_unlock(&g_io_lock);
    if (already_closed && !was_live) {
        errno = EINVAL;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "fclose_s", "double close refused");
        cplus_safe_stat_record("fclose_s", 0);
        return EOF;
    }
    rc = fclose(stream);
    if (rc != 0) {
        cplus_safe_stat_record("fclose_s", 0);
        return rc;
    }
    cplus_safe_log(CPLUS_SAFE_LOG_DEBUG, "fclose_s", "ok");
    cplus_safe_stat_record("fclose_s", 1);
    return 0;
}

size_t cplus_fread_s(void *buf, size_t bmax, size_t size, size_t count, FILE *stream) {
    size_t r;
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "fread_s", "enter");
    if (!buf || !stream || size == 0 || count == 0) {
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "fread_s", "invalid args");
        cplus_safe_stat_record("fread_s", 0);
        return 0;
    }
    if (count > bmax / size) count = bmax / size;
    r = fread(buf, size, count, stream);
    cplus_safe_log(CPLUS_SAFE_LOG_DEBUG, "fread_s", "ok");
    cplus_safe_stat_record("fread_s", 1);
    return r;
}

size_t cplus_fwrite_s(const void *buf, size_t bmax, size_t size, size_t count, FILE *stream) {
    size_t r;
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "fwrite_s", "enter");
    if (!buf || !stream || size == 0 || count == 0) {
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "fwrite_s", "invalid args");
        cplus_safe_stat_record("fwrite_s", 0);
        return 0;
    }
    if (count > bmax / size) count = bmax / size;
    r = fwrite(buf, size, count, stream);
    cplus_safe_log(CPLUS_SAFE_LOG_DEBUG, "fwrite_s", "ok");
    cplus_safe_stat_record("fwrite_s", 1);
    return r;
}
