#define _POSIX_C_SOURCE 200112L
#define CPLUS_SAFE_NO_UNPREFIXED_MACROS
#include "../safe_io.h"
#include "../safe_error.h"
#include "../safe_log.h"
#include "../safe_stats.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>

/* Bounded stream-identity caches with fail-closed growth policy.
 *
 * Each cache is a fixed-size open-addressed hash table.  Deletion writes a
 * tombstone (never NULL) so a probe chain stays intact: a lookup must skip
 * tombstones and stop only at a never-used (NULL) slot.  Insertion reuses the
 * first tombstone it passes, and compacts the table when it has no empty slot
 * left, so normal churn does not consume the fixed capacity.
 *
 * Capacity policy (documented): when a table has no free slot AND no
 * tombstone to reclaim, registration is REFUSED (errno = EMFILE) rather than
 * evicting an arbitrary marker.  Evicting a double-close marker could let a
 * second close reach host fclose() on a freed FILE*.  The caller of a refused
 * registration fails the whole operation; no host fclose() is attempted for
 * a stream whose closed marker could not be recorded. */
#define STREAM_CACHE_SLOTS 512u

static pthread_mutex_t g_io_lock = PTHREAD_MUTEX_INITIALIZER;
static FILE *g_live_streams[STREAM_CACHE_SLOTS];
static FILE *g_closed_streams[STREAM_CACHE_SLOTS];

/* Deletion marker: address of a private, never-dereferenced object.  It can
 * never equal a host-allocated FILE* and is distinct from NULL ("never used"). */
static FILE g_cache_tombstone;
#define CACHE_TOMBSTONE (&g_cache_tombstone)

static size_t g_live_count;
static size_t g_live_tombstones;
static size_t g_closed_count;
static size_t g_closed_tombstones;

static size_t hash_stream(const FILE *f) {
    uintptr_t x = (uintptr_t)f;
    x ^= x >> 33;
    x *= (uintptr_t)0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    return (size_t)(x & (STREAM_CACHE_SLOTS - 1u));
}

/* Lookup must skip tombstones: stopping at one would hide entries inserted
 * past it and is the root cause of the historical double-close UAF. */
static size_t stream_find(FILE **table, const FILE *f) {
    size_t mask = STREAM_CACHE_SLOTS - 1u;
    size_t i;
    if (!f) return STREAM_CACHE_SLOTS;
    i = hash_stream(f);
    for (size_t n = 0; n < STREAM_CACHE_SLOTS; ++n) {
        if (table[i] == NULL) return STREAM_CACHE_SLOTS;
        if (table[i] == f) return i;
        i = (i + 1u) & mask;
    }
    return STREAM_CACHE_SLOTS;
}

/* Insert into a table known to contain no tombstones (used when compacting). */
static void stream_insert_raw(FILE **table, FILE *f) {
    size_t mask = STREAM_CACHE_SLOTS - 1u;
    size_t i = hash_stream(f);
    while (table[i] != NULL) i = (i + 1u) & mask;
    table[i] = f;
}

static void stream_compact(FILE **table, size_t *tombstones) {
    FILE *fresh[STREAM_CACHE_SLOTS];
    memset(fresh, 0, sizeof fresh);
    for (size_t i = 0; i < STREAM_CACHE_SLOTS; ++i) {
        FILE *v = table[i];
        if (v != NULL && v != CACHE_TOMBSTONE) stream_insert_raw(fresh, v);
    }
    memcpy(table, fresh, sizeof fresh);
    *tombstones = 0u;
    /* Occupied (live) count is unchanged by compaction. */
}

/* Returns 1 on success (including already-present), 0 when the table is at
 * its documented capacity and the caller must fail closed. */
static int stream_add(FILE **table, size_t *count, size_t *tombstones, FILE *f) {
    size_t mask = STREAM_CACHE_SLOTS - 1u;
    size_t i, first_tomb = STREAM_CACHE_SLOTS;
    if (!f) return 0;
    i = hash_stream(f);
    for (size_t n = 0; n < STREAM_CACHE_SLOTS; ++n) {
        FILE *v = table[i];
        if (v == f) return 1;
        if (v == CACHE_TOMBSTONE) {
            if (first_tomb == STREAM_CACHE_SLOTS) first_tomb = i;
        } else if (v == NULL) {
            size_t at = (first_tomb != STREAM_CACHE_SLOTS) ? first_tomb : i;
            if (first_tomb != STREAM_CACHE_SLOTS) --*tombstones;
            table[at] = f;
            ++*count;
            return 1;
        }
        i = (i + 1u) & mask;
    }
    if (*tombstones != 0u) {
        stream_compact(table, tombstones);
        stream_insert_raw(table, f);
        ++*count;
        return 1;
    }
    return 0; /* full: never overwrite an existing marker */
}

static int stream_remove(FILE **table, size_t *count, size_t *tombstones, FILE *f) {
    size_t i = stream_find(table, f);
    if (i == STREAM_CACHE_SLOTS) return 0;
    table[i] = CACHE_TOMBSTONE;
    --*count;
    ++*tombstones;
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
    if (!stream_add(g_live_streams, &g_live_count, &g_live_tombstones, f)) {
        pthread_mutex_unlock(&g_io_lock);
        fclose(f); /* do not hand out an untrackable stream */
        errno = EMFILE;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "fopen_s", "live table full");
        cplus_safe_stat_record("fopen_s", 0);
        return NULL;
    }
    stream_remove(g_closed_streams, &g_closed_count, &g_closed_tombstones, f);
    pthread_mutex_unlock(&g_io_lock);
    cplus_safe_log(CPLUS_SAFE_LOG_DEBUG, "fopen_s", "ok");
    cplus_safe_stat_record("fopen_s", 1);
    return f;
}

int cplus_fclose_s(FILE *stream) {
    int in_live, in_closed, rc;
    if (!stream) {
        errno = EINVAL;
        cplus_safe_stat_record("fclose_s", 0);
        return EOF;
    }
    pthread_mutex_lock(&g_io_lock);
    in_live = stream_find(g_live_streams, stream) != STREAM_CACHE_SLOTS;
    in_closed = stream_find(g_closed_streams, stream) != STREAM_CACHE_SLOTS;
    /* Refuse whenever a closed marker exists.  In particular an entry in
     * BOTH tables is an inconsistent state and must fail closed rather than
     * fall through to host fclose() on a freed FILE* (the historical UAF). */
    if (in_closed) {
        pthread_mutex_unlock(&g_io_lock);
        errno = EINVAL;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "fclose_s", "double close refused");
        cplus_safe_stat_record("fclose_s", 0);
        return EOF;
    }
    /* Record the close marker BEFORE calling fclose().  If the table is at
     * capacity we refuse the close entirely: leaving no marker would allow a
     * later double close to reach host fclose(). */
    if (!stream_add(g_closed_streams, &g_closed_count, &g_closed_tombstones, stream)) {
        pthread_mutex_unlock(&g_io_lock);
        errno = EMFILE;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "fclose_s", "closed table full");
        cplus_safe_stat_record("fclose_s", 0);
        return EOF;
    }
    if (in_live)
        stream_remove(g_live_streams, &g_live_count, &g_live_tombstones, stream);
    pthread_mutex_unlock(&g_io_lock);
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
