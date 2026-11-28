#define _POSIX_C_SOURCE 200112L
#define CPLUS_SAFE_NO_UNPREFIXED_MACROS
#include "../safe_io.h"
#include "../safe_alloc.h"
#include "../safe_error.h"
#include "../safe_log.h"
#include "../safe_runtime.h"
#include "../safe_stats.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
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

/* The closed-stream cache is a fixed-size ring: once it fills, the oldest
 * marker at the hash slot is evicted. Refusing every further fclose() leaves
 * real FILE* handles open forever (permanent EMFILE), which is worse than
 * bounding double-close detection to the most recent 512 addresses. */
static void stream_add_evict(FILE **table, FILE *f) {
    table[hash_stream(f) & (STREAM_CACHE_SLOTS - 1u)] = f;
}

static int stream_remove(FILE **table, size_t *count, size_t *tombstones, FILE *f) {
    size_t i = stream_find(table, f);
    if (i == STREAM_CACHE_SLOTS) return 0;
    table[i] = CACHE_TOMBSTONE;
    --*count;
    ++*tombstones;
    return 1;
}

/* Additive query: is this stream one that cplus_fclose_s already closed (and
 * that cplus_fopen_s has not since re-registered)?  Held under the same lock as
 * the cache itself so a concurrent close/open cannot race the answer. */
int cplus_io_stream_is_closed(const FILE *stream) {
    int closed;
    if (!stream) return 0;
    pthread_mutex_lock(&g_io_lock);
    closed = stream_find(g_closed_streams, stream) != STREAM_CACHE_SLOTS;
    pthread_mutex_unlock(&g_io_lock);
    return closed;
}

int cplus_safe_stream_was_closed(const FILE *stream) {
    return cplus_io_stream_is_closed(stream);
}

size_t cplus_safe_open_stream_count(void) {
    size_t live;
    pthread_mutex_lock(&g_io_lock);
    live = g_live_count;
    pthread_mutex_unlock(&g_io_lock);
    return live;
}

/* Observable bound for the stream-identity caches.  Registration refuses when
 * this many slots are unavailable, so open/close churn can never grow the
 * table (issue #191).  Additive diagnostic; no public header change. */
size_t cplus_safe_stream_cache_capacity(void) { return STREAM_CACHE_SLOTS; }

/* Defense-in-depth for the caller-declared `bmax`.  The Annex K count clamp
 * (`count <= bmax/size`) is only as good as the caller's bound; for buffers that
 * came from the checked allocator we have a second, independent source of truth
 * and refuse a bmax larger than the real allocation -- the same rule the format
 * adapters apply to `dmax`.  Stack/static buffers are not tracked by the
 * allocator; they remain protected by the count clamp (and, at the call site,
 * by __builtin_object_size for the unprefixed format-style macros).  A freed
 * buffer is refused even when its size metadata is still cached. */
static int io_check_buf(const char *api, const void *buf, size_t bmax) {
    size_t tracked;
    if (!buf) return 0; /* the existing NULL-argument path owns this case */
    if (cplus_safe_ptr_was_freed(buf)) {
        errno = EINVAL;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, api, "use-after-free detected in I/O buffer");
        cplus_safe_stat_record(api, 0);
        return -1;
    }
    tracked = cplus_safe_ptr_remaining(buf);
    if (tracked > 0 && bmax > tracked) {
        errno = EINVAL;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, api, "I/O bmax exceeds tracked buffer capacity");
        cplus_safe_stat_record(api, 0);
        return -1;
    }
    return 0;
}

/* Refuse a stream that the checked layer already closed: fread/fwrite on it
 * would reach the host with a freed FILE*.  Returns -1 (caller maps that to the
 * API's defined failure value), 0 when the stream is still usable/untracked. */
static int io_check_stream(const char *api, FILE *stream) {
    if (!stream) return 0; /* the existing NULL-argument path owns this case */
    if (cplus_io_stream_is_closed(stream)) {
        errno = EINVAL;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, api, "checked stream was closed (use-after-close)");
        cplus_safe_stat_record(api, 0);
        return -1;
    }
    return 0;
}

/* Annex K constraint: size * count must fit in the caller-declared bmax, and
 * bmax itself must be a representable SafeCRT object size.  Clamping count used
 * to turn an out-of-bounds request into a successful short transfer (silent
 * truncation / data loss, issue #191).  Fail the whole operation closed instead
 * so no byte reaches the stream and the caller sees a hard error. */
static int io_check_bounds(const char *api, size_t bmax, size_t size, size_t count) {
    if (count > SIZE_MAX / size) {
        errno = EOVERFLOW;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, api, "fread/fwrite size * count overflow");
        cplus_safe_stat_record(api, 0);
        return -1;
    }
    if (bmax > RSIZE_MAX_MEM || count > bmax / size) {
        errno = EINVAL;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, api, "requested byte count exceeds bmax");
        cplus_safe_stat_record(api, 0);
        return -1;
    }
    return 0;
}

FILE *cplus_fopen_s(const char *name, const char *mode) {
    FILE *f;
    if (!name || !mode) {
        errno = EINVAL;
        cplus_safe_stat_record("fopen_s", 0);
        return NULL;
    }
    if (cplus_safe_ptr_was_freed(name) || cplus_safe_ptr_was_freed(mode)) {
        errno = EINVAL;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "fopen_s", "use-after-free detected in name/mode");
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
    if (!stream_add(g_closed_streams, &g_closed_count, &g_closed_tombstones,
                    stream)) {
        stream_add_evict(g_closed_streams, stream);
        /* count stays at capacity; the evicted marker is replaced, so no
         * double-close marker is lost for the stream being closed now. */
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
    if (io_check_buf("fread_s", buf, bmax) < 0 || io_check_stream("fread_s", stream) < 0) return 0;
    if (io_check_bounds("fread_s", bmax, size, count) < 0) return 0;
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
    if (io_check_buf("fwrite_s", buf, bmax) < 0 || io_check_stream("fwrite_s", stream) < 0) return 0;
    if (io_check_bounds("fwrite_s", bmax, size, count) < 0) return 0;
    r = fwrite(buf, size, count, stream);
    cplus_safe_log(CPLUS_SAFE_LOG_DEBUG, "fwrite_s", "ok");
    cplus_safe_stat_record("fwrite_s", 1);
    return r;
}

static int io_checked_guard(const char *api, const void *buf, size_t req_bytes,
                            size_t known_bmax, const FILE *stream) {
    size_t tracked;
    size_t cap;
    if (cplus_io_stream_is_closed(stream)) {
        errno = EBADF;
        cplus_safe_stat_record(api, 0);
        cplus_runtime_violation(api, "use-after-close detected on FILE*");
        return -1;
    }
    if (cplus_safe_ptr_was_freed(buf)) {
        errno = EINVAL;
        cplus_safe_stat_record(api, 0);
        cplus_runtime_violation(api, "use-after-free detected in I/O buffer");
        return -1;
    }
    tracked = cplus_safe_ptr_size(buf);
    cap = tracked > 0 ? tracked : (known_bmax != (size_t)-1 ? known_bmax : 0);
    if (cap > 0 && req_bytes > cap) {
        errno = ERANGE;
        cplus_safe_stat_record(api, 0);
        cplus_runtime_violation(api, "requested I/O size exceeds buffer capacity");
        return -1;
    }
    return 0;
}

char *cplus_fgets_s(char *buf, size_t bmax, int n, FILE *stream) {
    return cplus_fgets_s_checked(buf, bmax, n, stream);
}

char *cplus_fgets_s_checked(char *buf, size_t known_bmax, int n, FILE *stream) {
    char *r;
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "fgets_s", "enter");
    if (!buf || !stream || n <= 0) {
        errno = EINVAL;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "fgets_s", "invalid args");
        cplus_safe_stat_record("fgets_s", 0);
        return NULL;
    }
    if (io_checked_guard("fgets_s", buf, (size_t)n, known_bmax, stream) < 0) return NULL;
    r = fgets(buf, n, stream);
    if (!r) {
        buf[0] = '\0';
        cplus_safe_log(CPLUS_SAFE_LOG_WARN, "fgets_s", "EOF or read error");
        cplus_safe_stat_record("fgets_s", 0);
        return NULL;
    }
    cplus_safe_log(CPLUS_SAFE_LOG_DEBUG, "fgets_s", "ok");
    cplus_safe_stat_record("fgets_s", 1);
    return r;
}

size_t cplus_fread_s_checked(void *buf, size_t known_bmax, size_t bmax,
                             size_t size, size_t count, FILE *stream) {
    size_t r;
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "fread_s", "enter");
    if (!buf || !stream || size == 0 || count == 0) {
        errno = EINVAL;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "fread_s", "invalid args");
        cplus_safe_stat_record("fread_s", 0);
        return 0;
    }
    if (io_checked_guard("fread_s", buf, bmax, known_bmax, stream) < 0) return 0;
    if (io_check_bounds("fread_s", bmax, size, count) < 0) return 0;
    r = fread(buf, size, count, stream);
    cplus_safe_log(CPLUS_SAFE_LOG_DEBUG, "fread_s", "ok");
    cplus_safe_stat_record("fread_s", 1);
    return r;
}

size_t cplus_fwrite_s_checked(const void *buf, size_t known_bmax, size_t bmax,
                              size_t size, size_t count, FILE *stream) {
    size_t r;
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "fwrite_s", "enter");
    if (!buf || !stream || size == 0 || count == 0) {
        errno = EINVAL;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "fwrite_s", "invalid args");
        cplus_safe_stat_record("fwrite_s", 0);
        return 0;
    }
    if (io_checked_guard("fwrite_s", buf, bmax, known_bmax, stream) < 0) return 0;
    if (io_check_bounds("fwrite_s", bmax, size, count) < 0) return 0;
    r = fwrite(buf, size, count, stream);
    cplus_safe_log(CPLUS_SAFE_LOG_DEBUG, "fwrite_s", "ok");
    cplus_safe_stat_record("fwrite_s", 1);
    return r;
}

int cplus_getenv_s(size_t *len, char *value, size_t valuesz, const char *name) {
    const char *v;
    size_t n;
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "getenv_s", "enter");
    if (!name || (!value && valuesz > 0)) {
        errno = EINVAL;
        cplus_safe_stat_record("getenv_s", 0);
        return EINVAL;
    }
    if (value && cplus_safe_ptr_was_freed(value)) {
        errno = EINVAL;
        cplus_safe_stat_record("getenv_s", 0);
        cplus_runtime_violation("getenv_s", "use-after-free detected in output buffer");
        return EINVAL;
    }
    v = getenv(name);
    if (!v) {
        if (len) *len = 0;
        if (value && valuesz > 0) value[0] = '\0';
        cplus_safe_stat_record("getenv_s", 1);
        return 0;
    }
    n = strlen(v);
    if (len) *len = n;
    if (!value || valuesz <= n) {
        if (value && valuesz > 0) value[0] = '\0';
        errno = ERANGE;
        cplus_safe_stat_record("getenv_s", 0);
        return ERANGE;
    }
    memcpy(value, v, n + 1);
    cplus_safe_stat_record("getenv_s", 1);
    return 0;
}
