/* C+ hardened allocation (bundled, selectively linked).
 * Real implementation using the platform allocator backend (posix malloc/free)
 * plus a thread-safe allocation metadata side-table (byte_size, elem_size,
 * generation), overflow checks, double/invalid-free refusal, safe length
 * queries, and ownership transfer.
 * This file is compiled into libcplus_hardened; it does not replace the host
 * system CRT unless explicitly linked/installed by the user. */
#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../../log/log.h"

typedef struct cplus_alloc_meta {
    size_t byte_size;
    size_t elem_size;
    size_t elem_count;
    unsigned long long generation;
    int is_live;
} cplus_alloc_meta;

typedef struct meta_slot {
    void *ptr;                  /* NULL = empty, META_TOMB = deleted */
    size_t byte_size;
    size_t elem_size;
    unsigned long long generation;
} meta_slot;

typedef struct recent_slot {
    void *ptr;
    unsigned long long generation;
} recent_slot;

#define META_TOMB ((void *)(uintptr_t)1)
#define META_RECENT_CAP 1024u

static pthread_mutex_t g_meta_lock = PTHREAD_MUTEX_INITIALIZER;
static meta_slot *g_tab = NULL;
static size_t g_cap = 0;        /* power of two, or 0 */
static size_t g_used = 0;       /* live + tombstones */
static size_t g_live = 0;
static size_t g_live_bytes = 0;
static unsigned long long g_gen = 0;
static unsigned long g_double_frees = 0;
static unsigned long g_invalid_frees = 0;
static long g_inject = -1;      /* -1 off; else successes remaining */
static recent_slot g_recent[META_RECENT_CAP];

static size_t meta_hash(const void *p) {
    uintptr_t x = (uintptr_t)p;
    x ^= x >> 33;
    x *= (uintptr_t)0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= (uintptr_t)0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return (size_t)x;
}

static size_t meta_recent_idx(const void *p) {
    return meta_hash(p) & (META_RECENT_CAP - 1u);
}

/* Recently-freed addresses are kept in an OPEN-ADDRESSED table. A direct-mapped
 * single-slot cache used to misclassify a real double free as an invalid free
 * whenever two freed pointers collided in the same bucket. Linear probing
 * gives each distinct address its own slot until the table fills. */
static size_t recent_find(const void *p) {
    size_t mask = META_RECENT_CAP - 1u;
    size_t i = meta_recent_idx(p);
    if (!p) return META_RECENT_CAP;
    for (size_t n = 0; n < META_RECENT_CAP; ++n) {
        if (g_recent[i].ptr == NULL) return META_RECENT_CAP;
        if (g_recent[i].ptr == p) return i;
        i = (i + 1u) & mask;
    }
    return META_RECENT_CAP;
}
static int recent_contains(const void *p) { return recent_find(p) < META_RECENT_CAP; }
static void recent_remove(const void *p) {
    size_t i = recent_find(p);
    if (i < META_RECENT_CAP) { g_recent[i].ptr = NULL; g_recent[i].generation = 0; }
}
static void recent_add(const void *p, unsigned long long generation) {
    size_t mask = META_RECENT_CAP - 1u;
    size_t start = meta_recent_idx(p);
    size_t i = start;
    if (!p) return;
    for (size_t n = 0; n < META_RECENT_CAP; ++n) {
        if (g_recent[i].ptr == NULL || g_recent[i].ptr == p) {
            g_recent[i].ptr = (void *)p;
            g_recent[i].generation = generation;
            return;
        }
        i = (i + 1u) & mask;
    }
    /* Table full: overwrite the bucket's first slot (bounded diagnostic only). */
    g_recent[start].ptr = (void *)p;
    g_recent[start].generation = generation;
}

/* All helpers below require g_meta_lock held. */

static meta_slot *meta_find_addr(uintptr_t addr) {
    size_t mask, i, n;
    if (g_cap == 0 || addr == 0 || addr == (uintptr_t)META_TOMB)
        return NULL;
    mask = g_cap - 1u;
    i = meta_hash((const void *)addr) & mask;
    for (n = 0; n < g_cap; ++n) {
        if ((uintptr_t)g_tab[i].ptr == addr)
            return &g_tab[i];
        if (g_tab[i].ptr == NULL)
            return NULL;
        i = (i + 1u) & mask;
    }
    return NULL;
}

static meta_slot *meta_find(const void *p) {
    return meta_find_addr((uintptr_t)p);
}

static int meta_reserve_one(void) {
    size_t ncap, k;
    meta_slot *fresh;
    if (g_cap != 0 && (g_used + 1u) * 4u <= g_cap * 3u)
        return 0;
    ncap = g_cap ? g_cap : 64u;
    if (g_cap != 0 && g_live * 2u >= g_cap) {
        if (ncap > SIZE_MAX / 4u / sizeof(meta_slot))
            return -1;
        ncap = g_cap * 2u;
    }
    fresh = (meta_slot *)calloc(ncap, sizeof(meta_slot));
    if (!fresh)
        return -1;
    for (k = 0; k < g_cap; ++k) {
        void *p = g_tab[k].ptr;
        if (p != NULL && p != META_TOMB) {
            size_t i = meta_hash(p) & (ncap - 1u);
            while (fresh[i].ptr != NULL)
                i = (i + 1u) & (ncap - 1u);
            fresh[i] = g_tab[k];
        }
    }
    free(g_tab);
    g_tab = fresh;
    g_cap = ncap;
    g_used = g_live;
    return 0;
}

/* Precondition: meta_reserve_one() succeeded. */
static unsigned long long meta_insert(void *p, size_t byte_size, size_t elem_size) {
    meta_slot *existing = meta_find(p);
    size_t mask, i;
    unsigned long long gen = ++g_gen;
    if (elem_size == 0)
        elem_size = 1u;
    if (existing != NULL) {
        /* Handle rare case where an external raw free() released p before reuse. */
        g_live_bytes -= existing->byte_size;
        g_live_bytes += byte_size;
        existing->byte_size = byte_size;
        existing->elem_size = elem_size;
        existing->generation = gen;
    } else {
        mask = g_cap - 1u;
        i = meta_hash(p) & mask;
        while (g_tab[i].ptr != NULL && g_tab[i].ptr != META_TOMB)
            i = (i + 1u) & mask;
        if (g_tab[i].ptr == NULL)
            ++g_used;
        g_tab[i].ptr = p;
        g_tab[i].byte_size = byte_size;
        g_tab[i].elem_size = elem_size;
        g_tab[i].generation = gen;
        ++g_live;
        g_live_bytes += byte_size;
    }
    /* A fresh cplus allocation at a recycled address is live again, so it is
     * no longer a "recently freed" address. */
    recent_remove(p);
    return gen;
}

static void meta_remove(meta_slot *s) {
    g_live_bytes -= s->byte_size;
    --g_live;
    recent_add(s->ptr, s->generation);
    s->ptr = META_TOMB;
    s->byte_size = 0;
    s->elem_size = 0;
    s->generation = 0;
}

static int inject_fail_locked(void) {
    if (g_inject < 0)
        return 0;
    if (g_inject == 0)
        return 1;
    --g_inject;
    return 0;
}

static int should_fail(void) {
    int f;
    pthread_mutex_lock(&g_meta_lock);
    f = inject_fail_locked();
    pthread_mutex_unlock(&g_meta_lock);
    return f;
}

void *cplus_malloc(size_t size) {
    size_t actual;
    void *p;
    cplus_libc_log("malloc", "enter");
    actual = (size == 0) ? 1u : size;
    if (should_fail()) {
        errno = ENOMEM;
        return NULL;
    }
    p = malloc(actual);
    if (!p) {
        errno = ENOMEM;
        return NULL;
    }
    pthread_mutex_lock(&g_meta_lock);
    if (meta_reserve_one() != 0) {
        pthread_mutex_unlock(&g_meta_lock);
        free(p);
        errno = ENOMEM;
        return NULL;
    }
    meta_insert(p, actual, 1u);
    pthread_mutex_unlock(&g_meta_lock);
    return p;
}

void *cplus_calloc(size_t nmemb, size_t size) {
    size_t total, elem;
    void *p;
    cplus_libc_log("calloc", "enter");
    if (nmemb != 0 && size > SIZE_MAX / nmemb) {
        errno = ENOMEM;
        return NULL;
    }
    if (nmemb == 0 || size == 0) {
        total = 1u;
        elem = 1u;
    } else {
        total = nmemb * size;
        elem = size;
    }
    if (should_fail()) {
        errno = ENOMEM;
        return NULL;
    }
    p = calloc(1u, total);
    if (!p) {
        errno = ENOMEM;
        return NULL;
    }
    pthread_mutex_lock(&g_meta_lock);
    if (meta_reserve_one() != 0) {
        pthread_mutex_unlock(&g_meta_lock);
        free(p);
        errno = ENOMEM;
        return NULL;
    }
    meta_insert(p, total, elem);
    pthread_mutex_unlock(&g_meta_lock);
    return p;
}

void *cplus_realloc(void *ptr, size_t size) {
    size_t actual, old_bytes = 0, old_elem = 1u;
    uintptr_t old_addr;
    unsigned long long old_gen = 0;
    void *p;
    cplus_libc_log("realloc", "enter");
    if (ptr == NULL)
        return cplus_malloc(size);
    actual = (size == 0) ? 1u : size;
    old_addr = (uintptr_t)ptr;
    pthread_mutex_lock(&g_meta_lock);
    {
        meta_slot *s = meta_find_addr(old_addr);
        if (!s) {
            if (recent_contains(ptr)) ++g_double_frees;
            else
                ++g_invalid_frees;
            pthread_mutex_unlock(&g_meta_lock);
            errno = EINVAL;
            return NULL;
        }
        old_bytes = s->byte_size;
        old_elem = s->elem_size ? s->elem_size : 1u;
        old_gen = s->generation;
        if (inject_fail_locked() || meta_reserve_one() != 0) {
            pthread_mutex_unlock(&g_meta_lock);
            errno = ENOMEM;
            return NULL;
        }
    }
    pthread_mutex_unlock(&g_meta_lock);
    p = realloc(ptr, actual);
    if (!p) {
        errno = ENOMEM;
        return NULL;
    }
    pthread_mutex_lock(&g_meta_lock);
    {
        meta_slot *s = meta_find_addr(old_addr);
        /* ABA guard: the host realloc() ran with the lock released, so another
         * thread may have freed the old block and allocated a NEW live block at
         * the same address. Only a slot with the ORIGINAL generation belongs to
         * this realloc. Never remove a newer slot (that would silently strip
         * another thread's live metadata). */
        if (s != NULL && s->generation == old_gen && old_gen != 0) {
            if ((uintptr_t)p == old_addr) {
                g_live_bytes -= old_bytes;
                g_live_bytes += actual;
                s->byte_size = actual;
                s->generation = ++g_gen;
            } else {
                meta_remove(s);
                meta_insert(p, actual, old_elem);
            }
        } else {
            meta_insert(p, actual, old_elem);
        }
    }
    pthread_mutex_unlock(&g_meta_lock);
    return p;
}

int cplus_free_checked(void **inout) {
    void *ptr;
    int release = 0;
    cplus_libc_log("free", "enter");
    if (inout == NULL) {
        errno = EINVAL;
        return EINVAL;
    }
    ptr = *inout;
    if (ptr == NULL)
        return 0;
    pthread_mutex_lock(&g_meta_lock);
    {
        meta_slot *s = meta_find(ptr);
        if (s != NULL) {
            meta_remove(s);
            release = 1;
        } else if (recent_contains(ptr)) {
            ++g_double_frees;
        } else {
            ++g_invalid_frees;
        }
    }
    pthread_mutex_unlock(&g_meta_lock);
    *inout = NULL;
    if (release) {
        free(ptr);
        return 0;
    }
    errno = EINVAL;
    return EINVAL;
}

void cplus_free(void *ptr) {
    void *tmp = ptr;
    cplus_libc_log("free", "enter");
    if (ptr == NULL)
        return;
    (void)cplus_free_checked(&tmp);
}

int cplus_alloc_get_meta(const void *ptr, cplus_alloc_meta *out) {
    meta_slot *s;
    if (out != NULL) {
        out->byte_size = 0;
        out->elem_size = 0;
        out->elem_count = 0;
        out->generation = 0;
        out->is_live = 0;
    }
    if (ptr == NULL || out == NULL) {
        errno = EINVAL;
        return EINVAL;
    }
    pthread_mutex_lock(&g_meta_lock);
    s = meta_find(ptr);
    if (s != NULL) {
        size_t es = s->elem_size ? s->elem_size : 1u;
        out->byte_size = s->byte_size;
        out->elem_size = es;
        out->elem_count = s->byte_size / es;
        out->generation = s->generation;
        out->is_live = 1;
    }
    pthread_mutex_unlock(&g_meta_lock);
    if (!out->is_live) {
        errno = EINVAL;
        return EINVAL;
    }
    return 0;
}

int cplus_alloc_is_live(const void *ptr) {
    int live;
    pthread_mutex_lock(&g_meta_lock);
    live = meta_find(ptr) != NULL;
    pthread_mutex_unlock(&g_meta_lock);
    return live;
}

size_t cplus_alloc_size(const void *ptr) {
    size_t bytes = 0;
    meta_slot *s;
    pthread_mutex_lock(&g_meta_lock);
    s = meta_find(ptr);
    if (s != NULL)
        bytes = s->byte_size;
    pthread_mutex_unlock(&g_meta_lock);
    return bytes;
}

size_t cplus_alloc_elem_size(const void *ptr) {
    size_t elem = 0;
    meta_slot *s;
    pthread_mutex_lock(&g_meta_lock);
    s = meta_find(ptr);
    if (s != NULL)
        elem = s->elem_size;
    pthread_mutex_unlock(&g_meta_lock);
    return elem;
}

size_t cplus_alloc_length(const void *ptr) {
    size_t count = 0;
    meta_slot *s;
    pthread_mutex_lock(&g_meta_lock);
    s = meta_find(ptr);
    if (s != NULL) {
        size_t es = s->elem_size ? s->elem_size : 1u;
        count = s->byte_size / es;
    }
    pthread_mutex_unlock(&g_meta_lock);
    return count;
}

int cplus_alloc_query_length(const void *ptr, size_t elem_size, size_t *out_count) {
    meta_slot *s;
    int ok = 0;
    if (out_count != NULL)
        *out_count = 0;
    if (ptr == NULL || out_count == NULL) {
        errno = EINVAL;
        return EINVAL;
    }
    pthread_mutex_lock(&g_meta_lock);
    s = meta_find(ptr);
    if (s != NULL) {
        size_t es = elem_size > 0 ? elem_size : (s->elem_size ? s->elem_size : 1u);
        *out_count = s->byte_size / es;
        ok = 1;
    }
    pthread_mutex_unlock(&g_meta_lock);
    if (!ok) {
        errno = EINVAL;
        return EINVAL;
    }
    return 0;
}

int cplus_alloc_check_generation(const void *ptr, unsigned long long generation) {
    meta_slot *s;
    int match = 0;
    if (ptr == NULL || generation == 0)
        return 0;
    pthread_mutex_lock(&g_meta_lock);
    s = meta_find(ptr);
    if (s != NULL && s->generation == generation)
        match = 1;
    pthread_mutex_unlock(&g_meta_lock);
    return match;
}

int cplus_transfer_ownership(void **dst, void **src) {
    meta_slot *s;
    if (dst == NULL || src == NULL || *src == NULL || *dst != NULL || dst == src) {
        errno = EINVAL;
        return EINVAL;
    }
    pthread_mutex_lock(&g_meta_lock);
    s = meta_find(*src);
    if (s == NULL) {
        pthread_mutex_unlock(&g_meta_lock);
        errno = EINVAL;
        return EINVAL;
    }
    s->generation = ++g_gen;
    *dst = *src;
    *src = NULL;
    pthread_mutex_unlock(&g_meta_lock);
    return 0;
}

size_t cplus_alloc_live_count(void) {
    size_t n;
    pthread_mutex_lock(&g_meta_lock);
    n = g_live;
    pthread_mutex_unlock(&g_meta_lock);
    return n;
}

size_t cplus_alloc_live_bytes(void) {
    size_t n;
    pthread_mutex_lock(&g_meta_lock);
    n = g_live_bytes;
    pthread_mutex_unlock(&g_meta_lock);
    return n;
}

unsigned long cplus_alloc_double_free_count(void) {
    unsigned long n;
    pthread_mutex_lock(&g_meta_lock);
    n = g_double_frees;
    pthread_mutex_unlock(&g_meta_lock);
    return n;
}

unsigned long cplus_alloc_invalid_free_count(void) {
    unsigned long n;
    pthread_mutex_lock(&g_meta_lock);
    n = g_invalid_frees;
    pthread_mutex_unlock(&g_meta_lock);
    return n;
}

void cplus_alloc_fail_after(long count) {
    pthread_mutex_lock(&g_meta_lock);
    g_inject = count < 0 ? -1 : count; /* negative disables injection */
    pthread_mutex_unlock(&g_meta_lock);
}

void cplus_alloc_fail_reset(void) {
    pthread_mutex_lock(&g_meta_lock);
    g_inject = -1;
    pthread_mutex_unlock(&g_meta_lock);
}
