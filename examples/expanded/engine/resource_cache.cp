/* resource_cache.cp -- deterministic game resource manager.
 *
 * NOT a game engine: a small refcounting model that loads, acquires, uses and
 * releases assets by string id.  Per-slot metadata (state + generation) lets
 * the manager refuse double-release and use-after-release instead of touching
 * freed memory.  All failures are reported through the recoverable error* API.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/expanded/engine/resource_cache.cp --strict \
 *       --sanitize=none -I. -o examples/expanded/engine/.build/resource_cache
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "stdlib/collections.hp"

#define MAX_ASSETS 8
#define ID_MAX 32
#define PAYLOAD_BYTES 8
#define HANDLE_STRIDE 16

#define STATE_EMPTY 0
#define STATE_LIVE 1
#define STATE_RELEASED 2

typedef struct {
    char id[ID_MAX];
    int state;            /* STATE_EMPTY / STATE_LIVE / STATE_RELEASED */
    int refs;
    int gen;              /* incremented on every acquire */
    unsigned char *payload;
} Asset;

typedef struct {
    strmap *index;        /* asset id -> slot number */
    Asset assets[MAX_ASSETS];
    int used;             /* high-water mark: slots [0, used) were allocated */
} Cache;

static int valid_id(const char *id, error *err) {
    size_t n;
    err->clear();
    if (id == NULL) {
        err->set_category(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                          "null asset id");
        return -1;
    }
    n = strlen(id);
    if (n == 0 || n >= ID_MAX) {
        err->set_category(STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                          "asset id length out of range");
        return -1;
    }
    return 0;
}

static Asset *asset_at(Cache *c, int slot) {
    if (slot < 0 || slot >= MAX_ASSETS) return NULL;
    return &c->assets[slot];
}

static void report(const char *label, error *err) {
    print("REFUSE", label, "ok=", err->ok(),
          "category=", stdlib_category_name(err->category()),
          "code=", err->code(), "msg=", err->what());
}

/* Load an asset by string id.  Returns the slot number or -1 on failure. */
static int cache_load(Cache *c, const char *id, error *err) {
    int slot;
    int length;
    Asset *a;
    if (valid_id(id, err) != 0) return -1;
    if (strmap_contains(c->index, id)) {
        err->set_category(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_OWNERSHIP,
                          "asset id is already loaded");
        return -1;
    }
    for (slot = 0; slot < MAX_ASSETS; ++slot) {
        a = asset_at(c, slot);
        if (a == NULL) return -1;
        if (a->state == STATE_EMPTY) break;
    }
    if (slot >= MAX_ASSETS) {
        err->set_category(STDLIB_CATEGORY_RANGE, CPLUS_EXIT_RESOURCE,
                          "cache is full");
        return -1;
    }
    a = asset_at(c, slot);
    if (a == NULL) return -1;
    length = (int)strlen(id);
    a->payload = (unsigned char *)stdlib_alloc(PAYLOAD_BYTES);
    if (a->payload == NULL) {
        err->set_category(STDLIB_CATEGORY_MEMORY, CPLUS_EXIT_RESOURCE,
                          "asset payload allocation failed");
        return -1;
    }
    /* Deterministic payload derived only from the id length and index. */
    {
        int k;
        for (k = 0; k < PAYLOAD_BYTES; ++k)
            a->payload[k] = (unsigned char)((length + k * 7) & 0xff);
    }
    snprintf(a->id, sizeof(a->id), "%s", id);
    a->state = STATE_LIVE;
    a->refs = 0;
    a->gen = 0;
    if (slot >= c->used) c->used = slot + 1;
    if (strmap_put(c->index, id, slot) != 0) {
        stdlib_release(a->payload);
        a->payload = NULL;
        a->state = STATE_EMPTY;
        err->set_category(STDLIB_CATEGORY_MEMORY, CPLUS_EXIT_RESOURCE,
                          "asset index insert failed");
        return -1;
    }
    print("LOAD id=", id, "slot=", slot);
    return slot;
}

/* Acquire a reference.  Returns a generation-tagged handle or -1. */
static int cache_acquire(Cache *c, const char *id, error *err) {
    int slot = 0;
    Asset *a;
    if (valid_id(id, err) != 0) return -1;
    if (strmap_get(c->index, id, &slot) != 0) {
        err->set_category(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_OWNERSHIP,
                          "asset id is not loaded");
        return -1;
    }
    a = asset_at(c, slot);
    if (a == NULL || a->state != STATE_LIVE) {
        err->set_category(STDLIB_CATEGORY_LIFECYCLE, CPLUS_EXIT_USE_AFTER_FREE,
                          "asset metadata is not live");
        return -1;
    }
    if (a->gen >= HANDLE_STRIDE - 1) {
        err->set_category(STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                          "handle generation exhausted");
        return -1;
    }
    ++a->refs;
    ++a->gen;
    print("ACQUIRE id=", id, "handle=", slot * HANDLE_STRIDE + a->gen,
          "refs=", a->refs);
    return slot * HANDLE_STRIDE + a->gen;
}

/* Resolve a handle to its slot with full metadata validation. */
static Asset *resolve(Cache *c, int handle, error *err, const char *op) {
    int slot = handle / HANDLE_STRIDE;
    int gen = handle % HANDLE_STRIDE;
    Asset *a = asset_at(c, slot);
    err->clear();
    if (a == NULL || slot >= c->used) {
        err->set_category(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_OWNERSHIP,
                          "handle slot is outside the cache");
        return NULL;
    }
    if (a->state == STATE_EMPTY) {
        err->set_category(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_OWNERSHIP,
                          "handle was never loaded");
        return NULL;
    }
    if (a->state == STATE_RELEASED) {
        err->set_category(STDLIB_CATEGORY_LIFECYCLE, CPLUS_EXIT_USE_AFTER_FREE,
                          op);
        return NULL;
    }
    if (gen != a->gen) {
        err->set_category(STDLIB_CATEGORY_LIFECYCLE, CPLUS_EXIT_USE_AFTER_FREE,
                          "stale handle generation");
        return NULL;
    }
    return a;
}

static int cache_use(Cache *c, int handle, error *err) {
    Asset *a = resolve(c, handle, err, "use after release");
    if (a == NULL) return -1;
    if (a->payload == NULL) {
        err->set_category(STDLIB_CATEGORY_LIFECYCLE, CPLUS_EXIT_USE_AFTER_FREE,
                          "asset payload is gone");
        return -1;
    }
    print("USE id=", a->id, "byte0=", (int)a->payload[0]);
    return 0;
}

static int cache_release(Cache *c, int handle, error *err) {
    Asset *a = resolve(c, handle, err, "double release");
    if (a == NULL) return -1;
    if (a->refs <= 0) {
        err->set_category(STDLIB_CATEGORY_OWNERSHIP, CPLUS_EXIT_OWNERSHIP,
                          "reference count underflow");
        return -1;
    }
    --a->refs;
    if (a->refs > 0) {
        print("RELEASE id=", a->id, "handle=", handle, "refs=", a->refs);
        return 0;
    }
    /* Last reference: retire the metadata and free through the checked
     * allocator before removing the id from the index. */
    {
        int slot = handle / HANDLE_STRIDE;
        a->state = STATE_RELEASED;
        stdlib_release(a->payload);
        a->payload = NULL;
        strmap_remove(c->index, a->id);
        print("UNLOAD id=", a->id, "slot=", slot);
    }
    return 0;
}

int main(void) {
    strmap index = strmap();
    Cache c;
    error err = error();
    string tex = string("tex");
    string snd = string("snd");
    string derived = string("asset_");
    int h1, h2, h3;

    c.index = &index;
    c.used = 0;
    {
        int i;
        for (i = 0; i < MAX_ASSETS; ++i) {
            c.assets[i].state = STATE_EMPTY;
            c.assets[i].refs = 0;
            c.assets[i].gen = 0;
            c.assets[i].payload = NULL;
            c.assets[i].id[0] = '\0';
        }
    }

    derived.append("01");
    print("CACHE max=", MAX_ASSETS, "payload=", PAYLOAD_BYTES,
          "derived=", derived.c_str());

    if (cache_load(&c, tex.c_str(), &err) < 0 ||
        cache_load(&c, snd.c_str(), &err) < 0 ||
        cache_load(&c, derived.c_str(), &err) < 0) {
        print("FATAL: load failed: ", err.what());
        return 2;
    }
    if (cache_load(&c, tex.c_str(), &err) < 0)
        report("duplicate-load", &err);

    h1 = cache_acquire(&c, tex.c_str(), &err);
    h2 = cache_acquire(&c, tex.c_str(), &err);
    if (h1 < 0 || h2 < 0) {
        print("FATAL: acquire failed: ", err.what());
        return 2;
    }
    /* h1 is stale once the second acquire bumped the generation. */
    if (cache_release(&c, h1, &err) != 0)
        report("stale-handle", &err);
    cache_use(&c, h2, &err);
    cache_release(&c, h2, &err);  /* refs 2 -> 1 */
    cache_release(&c, h2, &err);  /* refs 1 -> 0: unload */

    /* Deliberate metadata refusals after the last reference was released. */
    if (cache_release(&c, h2, &err) != 0)
        report("double-release", &err);
    if (cache_use(&c, h2, &err) != 0)
        report("use-after-release", &err);
    if (cache_acquire(&c, tex.c_str(), &err) < 0)
        report("not-loaded", &err);
    if (cache_acquire(&c, "missing", &err) < 0)
        report("unknown-id", &err);

    /* Reloading the same id is allowed: the retired slot is reused. */
    if (cache_load(&c, tex.c_str(), &err) < 0) {
        print("FATAL: reload failed: ", err.what());
        return 2;
    }
    h3 = cache_acquire(&c, tex.c_str(), &err);
    if (h3 < 0) {
        print("FATAL: reacquire failed: ", err.what());
        return 2;
    }
    cache_use(&c, h3, &err);
    cache_release(&c, h3, &err);

    print("SUMMARY used=", c.used, "index=", (int)strmap_size(&index));
    return 0;
}
