/* ecs.cp -- a minimal, deterministic Entity-Component-System model.
 *
 * This is NOT a game engine: it is a small, fixed-seed simulation that shows
 * how checked C+ standard-library containers can hold components and how
 * recoverable errors are reported when a lookup cannot be satisfied.
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/legacy_examples/expanded/engine/ecs.cp --strict --sanitize=none \
 *       -I. -o apps/legacy_examples/expanded/engine/.build/ecs
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/collections.hp"

#define ENTITY_COUNT 6
#define TICK_COUNT 12
#define WORLD_MIN (-9)
#define WORLD_MAX 9

/* ---- deterministic pseudo-random source (no time(), no global state drift) */
static unsigned long long rng_state = 0x12345678ULL;

static void rng_reset(void) {
    rng_state = 0x12345678ULL;
}

static int rng_next(int lo, int hi) {
    unsigned int bits;
    unsigned int span;
    if (hi <= lo) return lo;
    rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
    bits = (unsigned int)((rng_state >> 33) & 0xffffffffu);
    span = (unsigned int)(hi - lo + 1);
    if (span == 0u) return lo; /* explicit guard for strict arithmetic mode */
    return lo + (int)(bits % span);
}

/* ---- the world: components keyed by stable entity id ------------------- */
typedef struct {
    vector *ids;     /* entity ids in spawn order (bounded iteration) */
    intmap *posx;
    intmap *posy;
    intmap *velx;
    intmap *vely;
    intmap *health;
    intmap *alive;   /* 1 = alive, 0 = dead (still present until sweep) */
} World;

/* Every vector read goes through the checked accessor. */
static int vec_at(vector *v, size_t index, int *out) {
    return vector_at_checked(v, index, out);
}

static int spawn(World *w, int id, int px, int py, int vx, int vy, int hp) {
    if (vector_push_back(w->ids, id) != 0) return -1;
    if (intmap_put(w->posx, id, px) != 0) return -1;
    if (intmap_put(w->posy, id, py) != 0) return -1;
    if (intmap_put(w->velx, id, vx) != 0) return -1;
    if (intmap_put(w->vely, id, vy) != 0) return -1;
    if (intmap_put(w->health, id, hp) != 0) return -1;
    if (intmap_put(w->alive, id, 1) != 0) return -1;
    return 0;
}

static intmap *component_map(World *w, const char *name) {
    if (strcmp(name, "posx") == 0) return w->posx;
    if (strcmp(name, "posy") == 0) return w->posy;
    if (strcmp(name, "velx") == 0) return w->velx;
    if (strcmp(name, "vely") == 0) return w->vely;
    if (strcmp(name, "health") == 0) return w->health;
    return NULL;
}

/* Component lookup that refuses missing entities, dead entities and unknown
 * component names through the recoverable error* API.  0 on success, -1 on
 * refusal (in which case *err describes the reason). */
static int component_get(World *w, int id, const char *name,
                         int *out, error *err) {
    intmap *map;
    int alive_value = 0;
    err->clear();
    if (intmap_get(w->health, id, out) != 0) {
        err->set_category(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_OWNERSHIP,
                          "missing entity: no component record for id");
        return -1;
    }
    if (intmap_get(w->alive, id, &alive_value) != 0 || alive_value == 0) {
        err->set_category(STDLIB_CATEGORY_LIFECYCLE, CPLUS_EXIT_USE_AFTER_FREE,
                          "missing entity: entity is dead or removed");
        return -1;
    }
    if (name == NULL || (map = component_map(w, name)) == NULL) {
        err->set_category(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                          "missing component: unknown component name");
        return -1;
    }
    if (intmap_get(map, id, out) != 0) {
        err->set_category(STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                          "missing component: checked read failed");
        return -1;
    }
    return 0;
}

static void report_refusal(const char *label, error *err) {
    print("REFUSE", label, "ok=", err->ok(),
          "category=", stdlib_category_name(err->category()),
          "code=", err->code(), "msg=", err->what());
}

/* ---- systems ------------------------------------------------------------ */
static void system_move(World *w) {
    size_t i;
    size_t n = vector_size(w->ids);
    for (i = 0; i < n; ++i) {
        int id = 0, alive_value = 0, x = 0, y = 0, vx = 0, vy = 0;
        if (vec_at(w->ids, i, &id) != 0) continue;
        if (intmap_get(w->alive, id, &alive_value) != 0 || alive_value == 0)
            continue;
        if (intmap_get(w->posx, id, &x) != 0 ||
            intmap_get(w->posy, id, &y) != 0 ||
            intmap_get(w->velx, id, &vx) != 0 ||
            intmap_get(w->vely, id, &vy) != 0)
            continue;
        x += vx;
        y += vy;
        if (x < WORLD_MIN) x = WORLD_MAX;
        if (x > WORLD_MAX) x = WORLD_MIN;
        if (y < WORLD_MIN) y = WORLD_MAX;
        if (y > WORLD_MAX) y = WORLD_MIN;
        intmap_put(w->posx, id, x);
        intmap_put(w->posy, id, y);
    }
}

static void system_damage(World *w, int tick) {
    int alive_ids[ENTITY_COUNT];
    int alive_count = 0;
    size_t i;
    size_t n = vector_size(w->ids);
    for (i = 0; i < n && alive_count < ENTITY_COUNT; ++i) {
        int id = 0, alive_value = 0;
        if (vec_at(w->ids, i, &id) != 0) continue;
        if (intmap_get(w->alive, id, &alive_value) != 0 || alive_value == 0)
            continue;
        alive_ids[alive_count++] = id;
    }
    if (alive_count == 0) {
        print("DAMAGE tick=", tick, "no living targets");
        return;
    }
    {
        int victim = alive_ids[rng_next(0, alive_count - 1)];
        int hp = 0;
        int amount = rng_next(2, 4);
        if (intmap_get(w->health, victim, &hp) != 0) return;
        hp -= amount;
        intmap_put(w->health, victim, hp);
        print("DAMAGE tick=", tick, "id=", victim, "amount=", amount,
              "hp=", hp);
        if (hp <= 0) {
            intmap_put(w->alive, victim, 0);
            print("DEATH tick=", tick, "id=", victim);
        }
    }
}

static void system_remove_dead(World *w) {
    size_t i = vector_size(w->ids);
    while (i > 0) {
        int id = 0, alive_value = 0;
        --i;
        if (vec_at(w->ids, i, &id) != 0) continue;
        if (intmap_get(w->alive, id, &alive_value) == 0 && alive_value != 0)
            continue;
        intmap_remove(w->posx, id);
        intmap_remove(w->posy, id);
        intmap_remove(w->velx, id);
        intmap_remove(w->vely, id);
        intmap_remove(w->health, id);
        intmap_remove(w->alive, id);
        vector_erase(w->ids, i, NULL);
        print("REMOVE id=", id);
    }
}

static void print_world(World *w, int tick) {
    size_t i;
    size_t n = vector_size(w->ids);
    print("TICK", tick, "entities=", (int)n);
    for (i = 0; i < n; ++i) {
        int id = 0, x = 0, y = 0, hp = 0, alive_value = 0;
        if (vec_at(w->ids, i, &id) != 0) continue;
        if (intmap_get(w->alive, id, &alive_value) != 0 || alive_value == 0)
            continue;
        if (intmap_get(w->posx, id, &x) != 0 ||
            intmap_get(w->posy, id, &y) != 0 ||
            intmap_get(w->health, id, &hp) != 0)
            continue;
        print("  ENTITY id=", id, "x=", x, "y=", y, "hp=", hp);
    }
}

static int checksum(World *w) {
    size_t i;
    size_t n = vector_size(w->ids);
    int sum = 0;
    for (i = 0; i < n; ++i) {
        int id = 0, x = 0, y = 0, hp = 0, alive_value = 0;
        if (vec_at(w->ids, i, &id) != 0) continue;
        if (intmap_get(w->alive, id, &alive_value) != 0 || alive_value == 0)
            continue;
        if (intmap_get(w->posx, id, &x) != 0 ||
            intmap_get(w->posy, id, &y) != 0 ||
            intmap_get(w->health, id, &hp) != 0)
            continue;
        sum += id + x * 3 + y * 5 + hp * 7;
    }
    return sum;
}

int main(void) {
    vector ids = vector();
    intmap posx = intmap();
    intmap posy = intmap();
    intmap velx = intmap();
    intmap vely = intmap();
    intmap health = intmap();
    intmap alive = intmap();
    error err = error();
    World w;
    int tick;
    int first_removed = -1;
    int value = 0;

    w.ids = &ids;
    w.posx = &posx;
    w.posy = &posy;
    w.velx = &velx;
    w.vely = &vely;
    w.health = &health;
    w.alive = &alive;

    rng_reset();
    print("ECS seed=0x12345678 ticks=", TICK_COUNT, "entities=", ENTITY_COUNT);
    if (spawn(&w, 101, -4, 2, 1, 0, 9) != 0 ||
        spawn(&w, 102, 3, -3, -1, 1, 7) != 0 ||
        spawn(&w, 103, 0, 0, 0, -1, 8) != 0 ||
        spawn(&w, 104, 5, 5, -1, -1, 6) != 0 ||
        spawn(&w, 105, -6, -2, 2, 1, 10) != 0 ||
        spawn(&w, 106, 1, 6, 0, -2, 5) != 0) {
        print("FATAL: component storage failed");
        return 2;
    }
    print_world(&w, -1);

    for (tick = 0; tick < TICK_COUNT; ++tick) {
        system_move(&w);
        system_damage(&w, tick);
        system_remove_dead(&w);
        print_world(&w, tick);
    }

    /* Find any removed id for the removed-entity refusal demo. */
    {
        int candidate;
        for (candidate = 101; candidate <= 106; ++candidate) {
            if (intmap_get(&health, candidate, &value) != 0) {
                first_removed = candidate;
                break;
            }
        }
    }

    /* Deliberate lookup failures, reported through the error* API. */
    if (component_get(&w, 999, "posx", &value, &err) != 0)
        report_refusal("unknown-entity", &err);
    if (component_get(&w, 101, "mass", &value, &err) != 0)
        report_refusal("unknown-component", &err);
    if (first_removed >= 0 &&
        component_get(&w, first_removed, "posx", &value, &err) != 0)
        report_refusal("removed-entity", &err);

    print("SUMMARY survivors=", (int)vector_size(&ids),
          "checksum=", checksum(&w));
    return 0;
}
