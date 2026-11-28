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

/* Stream-identity caches used for double-close/UAF protection and for the
 * open-stream diagnostic count.
 *
 * Each cache is an open-addressed hash table that grows dynamically (the same
 * policy as the checked allocator's metadata table).  Deletion writes a
 * tombstone (never NULL) so a probe chain stays intact: a lookup must skip
 * tombstones and stop only at a never-used (NULL) slot.  Insertion reuses the
 * first tombstone it passes; when the load INCLUDING tombstones would exceed
 * 75%, the table is grown to the next power of two and every live entry is
 * reinserted (which also clears the tombstones).
 *
 * A closed marker is essential state: it is what makes a second fclose_s()
 * refuse instead of reaching host fclose() on a freed FILE*.  It is therefore
 * never evicted to make room.  Registration is refused (errno = EMFILE) ONLY
 * when growing the table fails for lack of memory; a refused close never
 * calls host fclose(), so the guard itself never leaks a stream. */
#define STREAM_CACHE_INITIAL_SLOTS 512u

typedef struct {
    FILE **slots;       /* NULL = never used, CACHE_TOMBSTONE = deleted */
    size_t cap;         /* power of two, or 0 when unallocated */
    size_t count;       /* live entries */
    size_t tombstones;  /* deleted entries awaiting reuse/compaction */
} stream_table;

static pthread_mutex_t g_io_lock = PTHREAD_MUTEX_INITIALIZER;
static stream_table g_live_streams;    /* zero-initialized */
static stream_table g_closed_streams;  /* zero-initialized */

/* Deletion marker: address of a private, never-dereferenced object.  It can
 * never equal a host-allocated FILE* and is distinct from NULL ("never used"). */
static FILE g_cache_tombstone;
#define CACHE_TOMBSTONE (&g_cache_tombstone)

static size_t hash_stream(const FILE *f, size_t cap) {
    uintptr_t x = (uintptr_t)f;
    x ^= x >> 33;
    x *= (uintptr_t)0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    return (size_t)(x & (cap - 1u));
}

/* Lookup must skip tombstones: stopping at one would hide entries inserted
 * past it and is the root cause of the historical double-close UAF.  Returns
 * t->cap for "not found" (a valid slot index is always < cap). */
static size_t stream_find(const stream_table *t, const FILE *f) {
    size_t i, n;
    if (!f || t->cap == 0) return t->cap;
    i = hash_stream(f, t->cap);
    for (n = 0; n < t->cap; ++n) {
        FILE *v = t->slots[i];
        if (v == NULL) return t->cap;
        if (v == f) return i;
        i = (i + 1u) & (t->cap - 1u);
    }
    return t->cap;
}

/* Grow (and compact) when the load including tombstones would exceed 75%, so
 * a probe sequence always terminates at a NULL slot.  Returns 1 on success,
 * 0 on allocation failure (caller fails closed rather than evicting a marker).
 *
 * Tombstones are NOT a reason to grow.  An open/close loop leaves one live
 * entry and a growing trail of deletion markers; unconditionally doubling here
 * allocated a new table on every threshold crossing, so memory grew without
 * bound until calloc() failed and every later fopen_s/fclose_s was refused
 * with EMFILE.  Rehash at the SAME capacity when the live count is sparse, and
 * only double when the live entries themselves need the room (the allocator's
 * metadata table uses the same rule). */
static int stream_reserve(stream_table *t) {
    size_t used, ncap, k;
    FILE **fresh;
    if (t->cap == 0) {
        ncap = STREAM_CACHE_INITIAL_SLOTS;
    } else {
        used = t->count + t->tombstones;
        if (used + 1u <= t->cap - t->cap / 4u) return 1; /* under 75% load */
        if (t->count > t->cap / 2u) {
            if (t->cap > SIZE_MAX / 2u) return 0;
            ncap = t->cap * 2u;
        } else {
            ncap = t->cap; /* compaction alone drops the tombstones */
        }
    }
    if (ncap > SIZE_MAX / sizeof(*fresh)) return 0;
    fresh = (FILE **)calloc(ncap, sizeof(*fresh));
    if (!fresh) return 0;
    for (k = 0; k < t->cap; ++k) {
        FILE *v = t->slots[k];
        if (v != NULL && v != CACHE_TOMBSTONE) {
            size_t i = hash_stream(v, ncap);
            while (fresh[i] != NULL) i = (i + 1u) & (ncap - 1u);
            fresh[i] = v;
        }
    }
    free(t->slots);
    t->slots = fresh;
    t->cap = ncap;
    t->tombstones = 0u; /* compaction drops all tombstones; count unchanged */
    return 1;
}

/* Returns 1 on success (including already-present), 0 only when the table
 * could not be grown (OOM) -- never because it hit a fixed capacity. */
static int stream_add(stream_table *t, FILE *f) {
    size_t i, first_tomb, n;
    if (!f) return 0;
    if (!stream_reserve(t)) return 0;
    i = hash_stream(f, t->cap);
    first_tomb = t->cap;
    for (n = 0; n < t->cap; ++n) {
        FILE *v = t->slots[i];
        if (v == f) return 1;
        if (v == CACHE_TOMBSTONE) {
            if (first_tomb == t->cap) first_tomb = i;
        } else if (v == NULL) {
            size_t at = (first_tomb != t->cap) ? first_tomb : i;
            if (first_tomb != t->cap) --t->tombstones;
            t->slots[at] = f;
            ++t->count;
            return 1;
        }
        i = (i + 1u) & (t->cap - 1u);
    }
    return 0; /* unreachable: reserve() guarantees at least one NULL slot */
}

static int stream_remove(stream_table *t, FILE *f) {
    size_t i = stream_find(t, f);
    if (i == t->cap) return 0;
    t->slots[i] = CACHE_TOMBSTONE;
    --t->count;
    ++t->tombstones;
    return 1;
}

/* Additive query: is this stream one that cplus_fclose_s already closed (and
 * that cplus_fopen_s has not since re-registered)?  Held under the same lock as
 * the cache itself so a concurrent close/open cannot race the answer. */
int cplus_io_stream_is_closed(const FILE *stream) {
    int closed;
    if (!stream) return 0;
    pthread_mutex_lock(&g_io_lock);
    closed = stream_find(&g_closed_streams, stream) != g_closed_streams.cap;
    pthread_mutex_unlock(&g_io_lock);
    return closed;
}

int cplus_safe_stream_was_closed(const FILE *stream) {
    return cplus_io_stream_is_closed(stream);
}

size_t cplus_safe_open_stream_count(void) {
    size_t live;
    pthread_mutex_lock(&g_io_lock);
    live = g_live_streams.count;
    pthread_mutex_unlock(&g_io_lock);
    return live;
}

/* Additive diagnostic: the live-stream table's slot count.  Exported so a
 * regression test can prove that tombstone churn compacts in place instead of
 * doubling the table without bound; no existing symbol or header changes. */
size_t cplus_safe_stream_cache_capacity(void) {
    size_t cap;
    pthread_mutex_lock(&g_io_lock);
    cap = g_live_streams.cap;
    pthread_mutex_unlock(&g_io_lock);
    return cap;
}

/* Defense-in-depth for the caller-declared `bmax`.  The Annex K count clamp
 * (`count <= bmax/size`) is only as good as the caller's bound; for buffers that
 * came from the checked allocator we have a second, independent source of truth
 * and refuse a bmax larger than the real allocation -- the same rule the format
 * adapters apply to `dmax`.  Stack/static buffers are not tracked by the
 * allocator; they remain protected by the count clamp (and, at the call site,
 * by __builtin_object_size for the unprefixed format-style macros).  A freed
 * buffer is refused even when its size metadata is still cached. */
static int io_check_buf(const char *api, const void *buf, size_t bmax) {
    size_t remaining;
    if (!buf) return 0; /* the existing NULL-argument path owns this case */
    if (cplus_safe_ptr_was_freed(buf)) {
        errno = EINVAL;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, api, "use-after-free detected in I/O buffer");
        cplus_safe_stat_record(api, 0);
        return -1;
    }
    remaining = cplus_safe_remaining_capacity(buf);
    /* (size_t)-1 means "no tracked block contains this address": stack/static
     * buffers have no allocator capacity to enforce here (they remain bounded
     * by the Annex K count clamp).  Never let that sentinel act as a real,
     * unbounded capacity -- that is the safe_format.c sentinel bug. */
    if (remaining != (size_t)-1 && bmax > remaining) {
        errno = EINVAL;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, api, "I/O bmax exceeds remaining heap capacity");
        cplus_safe_stat_record(api, 0);
        return -1;
    }
    return 0;
}

/* Validate the byte request itself instead of silently truncating it.
 *
 * Annex K short I/O is only legitimate when the caller's `bmax` is a bound he
 * deliberately chose BELOW the real capacity (an untracked stack object, or a
 * tracked heap block with extra room).  When `bmax` IS the proven capacity --
 * a tracked allocation's remaining bytes, or the compile-time object size
 * passed by the checked macros -- a `size * count` that exceeds it is a bounds
 * violation, not a short read/write: refuse it with errno = EINVAL and return
 * the API's failure value without touching the host stream.  Out-of-range
 * bmax/size/count values and multiplication overflow are refused as well.
 *
 * Returns 0 when the request may proceed (the caller still applies the Annex K
 * count clamp for a caller-chosen smaller bound), -1 on refusal. */
static int io_check_request(const char *api, const void *buf, size_t bmax,
                            size_t size, size_t count, size_t known_bmax) {
    size_t remaining;
    if (bmax > RSIZE_MAX_MEM || size > RSIZE_MAX_MEM || count > RSIZE_MAX_MEM) {
        errno = EINVAL;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, api, "bmax/size/count exceeds RSIZE_MAX_MEM");
        cplus_safe_stat_record(api, 0);
        return -1;
    }
    if (count > SIZE_MAX / size) {
        errno = EOVERFLOW;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, api, "size * count overflow");
        cplus_safe_stat_record(api, 0);
        return -1;
    }
    if (count <= bmax / size) return 0;
    remaining = cplus_safe_remaining_capacity(buf);
    if (remaining == (size_t)-1 || bmax < remaining) {
        if (known_bmax == (size_t)-1 || bmax < known_bmax) return 0;
    }
    errno = EINVAL;
    cplus_safe_log(CPLUS_SAFE_LOG_ERROR, api, "requested byte count exceeds bmax");
    cplus_safe_stat_record(api, 0);
    return -1;
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
    if (!stream_add(&g_live_streams, f)) {
        pthread_mutex_unlock(&g_io_lock);
        fclose(f); /* do not hand out an untrackable stream */
        errno = EMFILE;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "fopen_s", "live table growth failed");
        cplus_safe_stat_record("fopen_s", 0);
        return NULL;
    }
    stream_remove(&g_closed_streams, f);
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
    in_live = stream_find(&g_live_streams, stream) != g_live_streams.cap;
    in_closed = stream_find(&g_closed_streams, stream) != g_closed_streams.cap;
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
    /* Record the close marker BEFORE calling fclose().  The table grows on
     * demand; only a failed allocation can stop us, and then we refuse the
     * close entirely so no later double close can reach host fclose(). */
    if (!stream_add(&g_closed_streams, stream)) {
        pthread_mutex_unlock(&g_io_lock);
        errno = EMFILE;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "fclose_s", "closed table growth failed");
        cplus_safe_stat_record("fclose_s", 0);
        return EOF;
    }
    if (in_live) stream_remove(&g_live_streams, stream);
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
    if (io_check_request("fread_s", buf, bmax, size, count, (size_t)-1) < 0) return 0;
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
    if (io_check_buf("fwrite_s", buf, bmax) < 0 || io_check_stream("fwrite_s", stream) < 0) return 0;
    if (io_check_request("fwrite_s", buf, bmax, size, count, (size_t)-1) < 0) return 0;
    if (count > bmax / size) count = bmax / size;
    r = fwrite(buf, size, count, stream);
    cplus_safe_log(CPLUS_SAFE_LOG_DEBUG, "fwrite_s", "ok");
    cplus_safe_stat_record("fwrite_s", 1);
    return r;
}

static int io_checked_guard(const char *api, const void *buf, size_t req_bytes,
                            size_t known_bmax, const FILE *stream) {
    size_t remaining, tracked, cap;
    int known;
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
    /* Resolve a PROVABLE capacity.  Remaining bytes of the tracked block
     * cover interior pointers, including a genuine one-past-the-end address
     * (cap == 0).  (size_t)-1 is explicitly "untracked" and must fall through
     * to the base-size / caller-supplied bound instead of being used as an
     * unbounded capacity (the safe_format.c sentinel bug).  Keep "known but
     * zero" distinct from "unknown": a proven capacity of 0 must reject any
     * positive request, while unknown capacity keeps the pre-existing policy
     * of deferring to the Annex K count clamp. */
    remaining = cplus_safe_remaining_capacity(buf);
    tracked = cplus_safe_ptr_size(buf);
    if (remaining != (size_t)-1) {
        cap = remaining;
        known = 1;
    } else if (tracked > 0) {
        cap = tracked;
        known = 1;
    } else if (known_bmax != (size_t)-1) {
        cap = known_bmax;
        known = 1;
    } else {
        cap = 0;
        known = 0;
    }
    if (known && req_bytes > cap) {
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
    if (count > SIZE_MAX / size) {
        errno = EOVERFLOW;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "fread_s", "size * count overflow");
        cplus_safe_stat_record("fread_s", 0);
        return 0;
    }
    if (io_checked_guard("fread_s", buf, bmax, known_bmax, stream) < 0) return 0;
    if (io_check_request("fread_s", buf, bmax, size, count, known_bmax) < 0) return 0;
    if (count > bmax / size) count = bmax / size;
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
    if (count > SIZE_MAX / size) {
        errno = EOVERFLOW;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "fwrite_s", "size * count overflow");
        cplus_safe_stat_record("fwrite_s", 0);
        return 0;
    }
    if (io_checked_guard("fwrite_s", buf, bmax, known_bmax, stream) < 0) return 0;
    if (io_check_request("fwrite_s", buf, bmax, size, count, known_bmax) < 0) return 0;
    if (count > bmax / size) count = bmax / size;
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
