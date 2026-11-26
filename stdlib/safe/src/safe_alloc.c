/* Checked allocation with a live-allocation metadata table and standard C signatures.
 * See safe_alloc.h for the ownership model and its honest limits. */
#define _POSIX_C_SOURCE 200112L
#include "../safe_alloc.h"
#include "../safe_log.h"
#include "../safe_stats.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct slot {
    void *ptr;                  /* NULL = empty, TOMB = deleted */
    size_t size;
    size_t elem_size;
    unsigned long long generation;
} slot;

#define TOMB ((void *)(uintptr_t)1)
#define RECENT_TOMB ((void *)(uintptr_t)2)
#define RECENT_SLOTS 1024u

/* Recently-freed addresses are kept in an OPEN-ADDRESSED, generation-tagged
 * table.  A direct-mapped single-pointer cache used to misclassify a genuine
 * double free as an invalid free whenever two freed addresses collided in one
 * bucket: the second free evicted the first.  Linear probing gives each
 * distinct address its own slot until the bounded table fills.  The generation
 * is recorded so a caller (through the generation-aware checked APIs) can tell
 * a stale alias from the current owner of a reissued address. */
typedef struct recent_slot {
    void *ptr;                  /* NULL = empty */
    unsigned long long generation;
} recent_slot;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static slot *g_tab;
static size_t g_cap;            /* power of two, or 0 */
static size_t g_used;           /* live + tombstones */
static size_t g_live;
static size_t g_live_bytes;
static unsigned long long g_gen;
static recent_slot g_recent[RECENT_SLOTS];
static long g_inject = -1;      /* -1 off; else successes still allowed */
static unsigned long g_seq;

/* Stronger 3-round mix; also used by the libc-layer recent cache.  Distribution
 * only: no exported value or layout depends on it. */
static size_t hash_ptr(const void *p) {
    uintptr_t x = (uintptr_t)p;
    x ^= x >> 33;
    x *= (uintptr_t)0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= (uintptr_t)0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return (size_t)x;
}

static size_t recent_index(const void *p) { return hash_ptr(p) & (RECENT_SLOTS - 1u); }

/* All helpers below require g_lock held. */

/* Return the slot index of a recently-freed address, or RECENT_SLOTS when it
 * is not present.  Linear probing skips deletion markers but stops at a
 * genuinely empty slot, so removing a reissued address cannot truncate the
 * probe chain of a colliding neighbor. */
static size_t recent_find(const void *p) {
    size_t mask = RECENT_SLOTS - 1u;
    size_t i, n;
    if (!p || p == TOMB || p == RECENT_TOMB) return RECENT_SLOTS;
    i = recent_index(p);
    for (n = 0; n < RECENT_SLOTS; ++n) {
        if (g_recent[i].ptr == NULL) return RECENT_SLOTS;
        if (g_recent[i].ptr == p) return i;
        i = (i + 1u) & mask;
    }
    return RECENT_SLOTS;
}

static int recent_contains(const void *p) { return recent_find(p) < RECENT_SLOTS; }

static void recent_remove(const void *p) {
    size_t i = recent_find(p);
    if (i < RECENT_SLOTS) {
        /* Tombstone rather than NULL: keep the probe chain intact. */
        g_recent[i].ptr = RECENT_TOMB;
        g_recent[i].generation = 0;
    }
}

static void recent_add(const void *p, unsigned long long generation) {
    size_t mask = RECENT_SLOTS - 1u;
    size_t start = recent_index(p);
    size_t i = start, n;
    if (!p || p == TOMB || p == RECENT_TOMB) return;
    for (n = 0; n < RECENT_SLOTS; ++n) {
        if (g_recent[i].ptr == NULL || g_recent[i].ptr == RECENT_TOMB ||
            g_recent[i].ptr == p) {
            g_recent[i].ptr = (void *)p;
            g_recent[i].generation = generation;
            return;
        }
        i = (i + 1u) & mask;
    }
    /* Table full: overwrite the home bucket.  Bounded diagnostic only. */
    g_recent[start].ptr = (void *)p;
    g_recent[start].generation = generation;
}

static slot *tab_find_addr(uintptr_t addr) {
    size_t mask, i, n;
    if (g_cap == 0 || addr == 0 || addr == (uintptr_t)TOMB) return NULL;
    mask = g_cap - 1;
    i = hash_ptr((const void *)addr) & mask;
    for (n = 0; n < g_cap; ++n) {
        if ((uintptr_t)g_tab[i].ptr == addr) return &g_tab[i];
        if (g_tab[i].ptr == NULL) return NULL;
        i = (i + 1) & mask;
    }
    return NULL;
}

static slot *tab_find(const void *p) { return tab_find_addr((uintptr_t)p); }

/* An interior pointer still lies inside its allocation. The exact-address
 * lookup above is kept for ownership operations, but bounds queries must be
 * able to find the containing block. */
static slot *tab_find_containing(uintptr_t addr) {
  size_t i;
  if (g_cap == 0 || addr == 0)
    return NULL;
  for (i = 0; i < g_cap; ++i) {
    slot *s = &g_tab[i];
    uintptr_t base, end;
    if (s->ptr == NULL || s->ptr == TOMB || s->ptr == RECENT_TOMB)
      continue;
    base = (uintptr_t)s->ptr;
    if (s->size > UINTPTR_MAX - base)
      continue;
    end = base + s->size;
    if (addr >= base && addr < end)
      return s;
  }
  return NULL;
}

/* Make room for one more entry without ever failing afterwards. */
static int tab_reserve_one(void) {
    size_t ncap, k;
    slot *fresh;
    if (g_cap != 0 && (g_used + 1) * 4 <= g_cap * 3) return 0;
    ncap = g_cap ? g_cap : 64;
    if (g_cap != 0 && g_live * 2 >= g_cap) {
        if (ncap > SIZE_MAX / 4 / sizeof(slot)) return -1;
        ncap = g_cap * 2;
    }
    fresh = (slot *)calloc(ncap, sizeof(slot));
    if (!fresh) return -1;
    for (k = 0; k < g_cap; ++k) {
        void *p = g_tab[k].ptr;
        if (p != NULL && p != TOMB) {
            size_t i = hash_ptr(p) & (ncap - 1);
            while (fresh[i].ptr != NULL) i = (i + 1) & (ncap - 1);
            fresh[i] = g_tab[k];
        }
    }
    free(g_tab);
    g_tab = fresh;
    g_cap = ncap;
    g_used = g_live;
    return 0;
}

/* Precondition: tab_reserve_one() succeeded. */
static void tab_insert(void *p, size_t size, size_t elem_size) {
    slot *existing = tab_find(p);
    size_t mask, i;
    if (elem_size == 0) elem_size = 1;
    if (existing) {
        g_live_bytes -= existing->size;
        g_live_bytes += size;
        existing->size = size;
        existing->elem_size = elem_size;
        existing->generation = ++g_gen;
    } else {
        mask = g_cap - 1;
        i = hash_ptr(p) & mask;
        while (g_tab[i].ptr != NULL && g_tab[i].ptr != TOMB) i = (i + 1) & mask;
        if (g_tab[i].ptr == NULL) ++g_used;
        g_tab[i].ptr = p;
        g_tab[i].size = size;
        g_tab[i].elem_size = elem_size;
        g_tab[i].generation = ++g_gen;
        ++g_live;
        g_live_bytes += size;
    }
    recent_remove(p);
}

static void tab_remove(slot *s) {
    void *p = s->ptr;
    unsigned long long generation = s->generation;
    g_live_bytes -= s->size;
    --g_live;
    recent_add(p, generation);
    s->ptr = TOMB;
    s->size = 0;
    s->elem_size = 0;
    s->generation = 0;
}

/* Returns 1 when injection says this allocation must fail. */
static int inject_fail_locked(void) {
    ++g_seq;
    if (g_inject < 0) return 0;
    if (g_inject == 0) return 1;
    --g_inject;
    return 0;
}

static void *finish_alloc(void *p, size_t size, size_t elem_size, const char *api) {
    pthread_mutex_lock(&g_lock);
    if (tab_reserve_one() != 0) {
        pthread_mutex_unlock(&g_lock);
        free(p);
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, api, "bookkeeping oom");
        cplus_safe_stat_record(api, 0);
        errno = ENOMEM;
        return NULL;
    }
    tab_insert(p, size, elem_size);
    pthread_mutex_unlock(&g_lock);
    cplus_safe_log(CPLUS_SAFE_LOG_DEBUG, api, "ok");
    cplus_safe_stat_record(api, 1);
    return p;
}

static int should_fail(void) {
    int f;
    pthread_mutex_lock(&g_lock);
    f = inject_fail_locked();
    pthread_mutex_unlock(&g_lock);
    return f;
}

void *malloc_s(size_t size) {
    void *p;
    size_t req = size == 0 ? 1u : size;
    if (size > RSIZE_MAX_MEM) {
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "malloc_s", "size exceeds RSIZE_MAX_MEM");
        cplus_safe_stat_record("malloc_s", 0);
        errno = EINVAL;
        return NULL;
    }
    if (should_fail()) {
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "malloc_s", "injected failure");
        cplus_safe_stat_record("malloc_s", 0);
        errno = ENOMEM;
        return NULL;
    }
    p = malloc(req);
    if (!p) {
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "malloc_s", "oom");
        cplus_safe_stat_record("malloc_s", 0);
        errno = ENOMEM;
        return NULL;
    }
    return finish_alloc(p, req, 1u, "malloc_s");
}

void *calloc_s(size_t count, size_t size) {
    void *p;
    size_t req_count = count;
    size_t req_size = size;
    size_t total;
    size_t elem;
    if (req_count == 0 || req_size == 0) {
        req_count = 1u;
        req_size = 1u;
    }
    if (req_count > SIZE_MAX / req_size || req_count * req_size > RSIZE_MAX_MEM) {
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "calloc_s", "overflow");
        cplus_safe_stat_record("calloc_s", 0);
        errno = EOVERFLOW;
        return NULL;
    }
    total = req_count * req_size;
    elem = req_size;
    if (should_fail()) {
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "calloc_s", "injected failure");
        cplus_safe_stat_record("calloc_s", 0);
        errno = ENOMEM;
        return NULL;
    }
    p = calloc(req_count, req_size);
    if (!p) {
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "calloc_s", "oom");
        cplus_safe_stat_record("calloc_s", 0);
        errno = ENOMEM;
        return NULL;
    }
    return finish_alloc(p, total, elem, "calloc_s");
}

void *aligned_alloc_s(size_t alignment, size_t size) {
    void *p = NULL;
    size_t req = size == 0 ? 1u : size;
    if (alignment < sizeof(void *) || (alignment & (alignment - 1u)) != 0 ||
        size > RSIZE_MAX_MEM) {
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "aligned_alloc_s", "bad alignment/size");
        cplus_safe_stat_record("aligned_alloc_s", 0);
        errno = EINVAL;
        return NULL;
    }
    if (should_fail()) {
        cplus_safe_stat_record("aligned_alloc_s", 0);
        errno = ENOMEM;
        return NULL;
    }
    if (posix_memalign(&p, alignment, req) != 0 || !p) {
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "aligned_alloc_s", "oom");
        cplus_safe_stat_record("aligned_alloc_s", 0);
        errno = ENOMEM;
        return NULL;
    }
    return finish_alloc(p, req, 1u, "aligned_alloc_s");
}

void *realloc_s(void *ptr, size_t size) {
    void *p;
    size_t req = size == 0 ? 1u : size;
    size_t old_size = 0, old_elem = 1u;
    uintptr_t old_addr;
    unsigned long long old_gen = 0;
    if (size > RSIZE_MAX_MEM) {
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "realloc_s", "size exceeds RSIZE_MAX_MEM; owner preserved");
        cplus_safe_stat_record("realloc_s", 0);
        errno = EINVAL;
        return NULL;
    }
    if (ptr == NULL) {
        return malloc_s(size);
    }
    old_addr = (uintptr_t)ptr;
    pthread_mutex_lock(&g_lock);
    {
        slot *s = tab_find_addr(old_addr);
        if (!s) {
            pthread_mutex_unlock(&g_lock);
            cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "realloc_s", "pointer not owned by this allocator; left untouched");
            cplus_safe_stat_record("realloc_s", 0);
            errno = EINVAL;
            return NULL;
        }
        old_size = s->size;
        old_elem = s->elem_size ? s->elem_size : 1u;
        old_gen = s->generation;
    }
    if (inject_fail_locked()) {
        pthread_mutex_unlock(&g_lock);
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "realloc_s", "injected failure; owner preserved");
        cplus_safe_stat_record("realloc_s", 0);
        errno = ENOMEM;
        return NULL;
    }
    /* Reserve table room first so nothing can fail after realloc succeeds. */
    if (tab_reserve_one() != 0) {
        pthread_mutex_unlock(&g_lock);
        cplus_safe_stat_record("realloc_s", 0);
        errno = ENOMEM;
        return NULL;
    }
    pthread_mutex_unlock(&g_lock);
    p = realloc(ptr, req);
    if (!p) {
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "realloc_s", "oom/failed; owner preserved");
        cplus_safe_stat_record("realloc_s", 0);
        errno = ENOMEM;
        return NULL;
    }
    pthread_mutex_lock(&g_lock);
    {
        slot *s = tab_find_addr(old_addr);
        /* ABA guard: host realloc() ran without the lock. Only a slot with
         * the ORIGINAL generation belongs to this realloc; never remove a
         * newer block that another thread allocated at the recycled address. */
        if (s && s->generation == old_gen && old_gen != 0) {
            if ((uintptr_t)p == old_addr) {
                g_live_bytes -= old_size;
                g_live_bytes += req;
                s->size = req;
                s->generation = ++g_gen;
            } else {
                tab_remove(s);
                tab_insert(p, req, old_elem);
            }
        } else {
            tab_insert(p, req, old_elem);
        }
    }
    pthread_mutex_unlock(&g_lock);
    cplus_safe_log(CPLUS_SAFE_LOG_DEBUG, "realloc_s", "ok");
    cplus_safe_stat_record("realloc_s", 1);
    return p;
}

int free_checked_s(void *ptr) {
    int rc = CPLUS_SAFE_OK;
    int release = 0;
    if (!ptr) {
        cplus_safe_stat_record("free_checked_s", 1);
        return CPLUS_SAFE_OK;
    }
    pthread_mutex_lock(&g_lock);
    {
        slot *s = tab_find(ptr);
        if (s) {
            tab_remove(s);
            release = 1;
        } else if (recent_contains(ptr)) {
            rc = CPLUS_SAFE_EINVAL;
            cplus_safe_stat_record("double_free", 0);
        } else {
            rc = CPLUS_SAFE_EINVAL;
            cplus_safe_stat_record("invalid_free", 0);
        }
    }
    pthread_mutex_unlock(&g_lock);
    if (release) {
        free(ptr);
        cplus_safe_log(CPLUS_SAFE_LOG_DEBUG, "free_s", "ok");
    } else {
        errno = EINVAL;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "free_s", "double/invalid free refused");
    }
    cplus_safe_stat_record("free_checked_s", rc == CPLUS_SAFE_OK);
    return rc;
}

void free_s(void *ptr) {
    int rc;
    if (!ptr) {
        cplus_safe_stat_record("free_s", 1);
        return;
    }
    rc = free_checked_s(ptr);
    cplus_safe_stat_record("free_s", rc == CPLUS_SAFE_OK);
}

/* Generation-aware free.  Refuses a stale alias whose address was reissued to
 * a newer live block: the pointer is only released when it is live AND its
 * current generation matches the one the caller captured.  A refusal never
 * touches the live table, the payload, or the caller's pointer.  Additive: the
 * legacy free_s/free_checked_s behavior is unchanged. */
int free_checked_generation_s(void *ptr, unsigned long long generation) {
    int release = 0;
    int rc = CPLUS_SAFE_EINVAL;
    if (!ptr || generation == 0) {
        errno = EINVAL;
        cplus_safe_stat_record("free_checked_generation_s", 0);
        return CPLUS_SAFE_EINVAL;
    }
    pthread_mutex_lock(&g_lock);
    {
        slot *s = tab_find(ptr);
        if (s && s->generation == generation) {
            tab_remove(s);
            release = 1;
            rc = CPLUS_SAFE_OK;
        } else if (s) {
            /* Live, but the caller's generation is stale: do not free the
             * current owner of a recycled address (F-01). */
            cplus_safe_stat_record("stale_free", 0);
        } else if (recent_contains(ptr)) {
            cplus_safe_stat_record("double_free", 0);
        } else {
            cplus_safe_stat_record("invalid_free", 0);
        }
    }
    pthread_mutex_unlock(&g_lock);
    if (release) {
        free(ptr);
        cplus_safe_log(CPLUS_SAFE_LOG_DEBUG, "free_checked_generation_s", "ok");
    } else {
        errno = EINVAL;
        cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "free_checked_generation_s",
                       "refused: generation mismatch / not live");
    }
    cplus_safe_stat_record("free_checked_generation_s", rc == CPLUS_SAFE_OK);
    return rc;
}

/* Remaining writable bytes from ptr to the end of its containing live
 * block. Returns (size_t)-1 when ptr is in no tracked block; 0 at a genuine
 * one-past-the-end address. An address that is both the end of one block and
 * the base of an adjacent live block is treated as LIVE with that block's
 * full capacity (exact base matches take precedence). */
size_t cplus_safe_remaining_capacity(const void *ptr) {
    size_t k;
    uintptr_t p = (uintptr_t)ptr;
    if (!ptr) return (size_t)-1;
    pthread_mutex_lock(&g_lock);
    for (k = 0; k < g_cap; ++k) {
        void *base = g_tab[k].ptr;
        if (base != NULL && base != TOMB && (uintptr_t)base == p) {
            size_t cap = g_tab[k].size;
            pthread_mutex_unlock(&g_lock);
            return cap;
        }
    }
    for (k = 0; k < g_cap; ++k) {
        void *base = g_tab[k].ptr;
        uintptr_t b;
        if (base == NULL || base == TOMB) continue;
        b = (uintptr_t)base;
        if (p > b && p <= b + g_tab[k].size) {
            size_t cap = (size_t)(b + g_tab[k].size - p);
            pthread_mutex_unlock(&g_lock);
            return cap;
        }
    }
    pthread_mutex_unlock(&g_lock);
    return (size_t)-1;
}
int cplus_safe_alloc_get_meta(const void *ptr, cplus_safe_alloc_meta *out) {
    slot *s;
    if (out) {
        out->byte_size = 0;
        out->elem_size = 0;
        out->elem_count = 0;
        out->generation = 0;
        out->is_live = 0;
    }
    if (!ptr || !out) return CPLUS_SAFE_EINVAL;
    pthread_mutex_lock(&g_lock);
    s = tab_find(ptr);
    if (s) {
        size_t es = s->elem_size ? s->elem_size : 1u;
        out->byte_size = s->size;
        out->elem_size = es;
        out->elem_count = s->size / es;
        out->generation = s->generation;
        out->is_live = 1;
    }
    pthread_mutex_unlock(&g_lock);
    return out->is_live ? CPLUS_SAFE_OK : CPLUS_SAFE_EINVAL;
}

int cplus_safe_ptr_is_live(const void *ptr) {
    int live;
    pthread_mutex_lock(&g_lock);
    live = tab_find(ptr) != NULL;
    pthread_mutex_unlock(&g_lock);
    return live;
}

int cplus_safe_ptr_was_freed(const void *ptr) {
    int freed;
    if (!ptr) return 0;
    pthread_mutex_lock(&g_lock);
    freed = (tab_find(ptr) == NULL) && recent_contains(ptr);
    pthread_mutex_unlock(&g_lock);
    return freed;
}

size_t cplus_safe_ptr_size(const void *ptr) {
    size_t n = 0;
    slot *s;
    pthread_mutex_lock(&g_lock);
    s = tab_find(ptr);
    if (s) n = s->size;
    pthread_mutex_unlock(&g_lock);
    return n;
}

size_t cplus_safe_ptr_remaining(const void *ptr) {
  size_t n = 0;
  slot *s;
  uintptr_t addr = (uintptr_t)ptr;
  pthread_mutex_lock(&g_lock);
  s = tab_find_containing(addr);
  if (s && addr >= (uintptr_t)s->ptr)
    n = s->size - (size_t)(addr - (uintptr_t)s->ptr);
  pthread_mutex_unlock(&g_lock);
  return n;
}

size_t cplus_safe_ptr_elem_size(const void *ptr) {
    size_t es = 0;
    slot *s;
    pthread_mutex_lock(&g_lock);
    s = tab_find(ptr);
    if (s) es = s->elem_size;
    pthread_mutex_unlock(&g_lock);
    return es;
}

size_t cplus_safe_ptr_count(const void *ptr) {
    size_t n = 0;
    slot *s;
    pthread_mutex_lock(&g_lock);
    s = tab_find(ptr);
    if (s) {
        size_t es = s->elem_size ? s->elem_size : 1u;
        n = s->size / es;
    }
    pthread_mutex_unlock(&g_lock);
    return n;
}

int cplus_safe_query_length(const void *ptr, size_t elem_size, size_t *out_count) {
    slot *s;
    int ok = 0;
    if (out_count) *out_count = 0;
    if (!ptr || !out_count) return CPLUS_SAFE_EINVAL;
    pthread_mutex_lock(&g_lock);
    s = tab_find(ptr);
    if (s) {
        size_t es = elem_size > 0 ? elem_size : (s->elem_size ? s->elem_size : 1u);
        *out_count = s->size / es;
        ok = 1;
    }
    pthread_mutex_unlock(&g_lock);
    return ok ? CPLUS_SAFE_OK : CPLUS_SAFE_EINVAL;
}

int cplus_safe_ptr_check_generation(const void *ptr, unsigned long long generation) {
    slot *s;
    int match = 0;
    if (!ptr || generation == 0) return 0;
    pthread_mutex_lock(&g_lock);
    s = tab_find(ptr);
    if (s && s->generation == generation) match = 1;
    pthread_mutex_unlock(&g_lock);
    return match;
}

size_t cplus_safe_alloc_live_count(void) {
    size_t n;
    pthread_mutex_lock(&g_lock);
    n = g_live;
    pthread_mutex_unlock(&g_lock);
    return n;
}

size_t cplus_safe_alloc_live_bytes(void) {
    size_t n;
    pthread_mutex_lock(&g_lock);
    n = g_live_bytes;
    pthread_mutex_unlock(&g_lock);
    return n;
}

int cplus_safe_transfer_ownership(void **dst, void **src) {
    slot *s;
    if (!dst || !src || !*src || *dst != NULL || dst == src) {
        cplus_safe_stat_record("transfer_owner", 0);
        return CPLUS_SAFE_EINVAL;
    }
    pthread_mutex_lock(&g_lock);
    s = tab_find(*src);
    if (!s) {
        pthread_mutex_unlock(&g_lock);
        cplus_safe_stat_record("transfer_owner", 0);
        return CPLUS_SAFE_EINVAL;
    }
    s->generation = ++g_gen;
    *dst = *src;
    *src = NULL;
    pthread_mutex_unlock(&g_lock);
    cplus_safe_stat_record("transfer_owner", 1);
    return CPLUS_SAFE_OK;
}

/* Generation-aware ownership transfer.  Identical to
 * cplus_safe_transfer_ownership except that it additionally requires the
 * source slot's generation to equal `generation`.  A stale alias at a reissued
 * address is refused with EINVAL and leaves both pointers and metadata
 * untouched.  Additive: the legacy transfer function is unchanged. */
int cplus_safe_transfer_ownership_generation(void **dst, void **src,
                                            unsigned long long generation) {
    slot *s;
    if (!dst || !src || !*src || *dst != NULL || dst == src || generation == 0) {
        errno = EINVAL;
        cplus_safe_stat_record("transfer_owner_generation", 0);
        return CPLUS_SAFE_EINVAL;
    }
    pthread_mutex_lock(&g_lock);
    s = tab_find(*src);
    if (!s || s->generation != generation) {
        pthread_mutex_unlock(&g_lock);
        errno = EINVAL;
        cplus_safe_stat_record("stale_transfer", 0);
        return CPLUS_SAFE_EINVAL;
    }
    s->generation = ++g_gen;
    *dst = *src;
    *src = NULL;
    pthread_mutex_unlock(&g_lock);
    cplus_safe_stat_record("transfer_owner_generation", 1);
    return CPLUS_SAFE_OK;
}

void cplus_safe_alloc_fail_after(long count) {
    pthread_mutex_lock(&g_lock);
    g_inject = count < 0 ? 0 : count;
    pthread_mutex_unlock(&g_lock);
}

void cplus_safe_alloc_fail_reset(void) {
    pthread_mutex_lock(&g_lock);
    g_inject = -1;
    g_seq = 0;
    pthread_mutex_unlock(&g_lock);
}

unsigned long cplus_safe_alloc_seq(void) {
    unsigned long n;
    pthread_mutex_lock(&g_lock);
    n = g_seq;
    pthread_mutex_unlock(&g_lock);
    return n;
}
