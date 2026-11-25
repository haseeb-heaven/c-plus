/* Real-world example: game-engine data layer. All storage is allocator-aware
 * via vector_in(&A); invalid operations refuse through error*. Flat C style
 * with local vectors passed by pointer avoids C+ nested-member lowering. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/alloc_iface.hp"
#include "stdlib/arena.hp"
#include "stdlib/vector.hp"
#include "stdlib/collections.hp"

#define MAX_ENTITIES 64

static int engine_spawn(vector *pos, vector *hp, int x, int y, int h) {
    error e = error();
    size_t cur = pos->size();
    if (h < 0 || h > 1000 || x < -1000 || x > 1000 || y < -1000 || y > 1000)
        return error_fail(&e, CPLUS_EXIT_UNSUPPORTED, "engine: invalid spawn"), -1;
    if (cur >= MAX_ENTITIES * 2u)
        return error_fail(&e, CPLUS_EXIT_RESOURCE, "engine: entity limit"), -1;
    if (pos->push_back(x) != 0 || pos->push_back(y) != 0 || hp->push_back(h) != 0)
        return error_fail(&e, CPLUS_EXIT_RESOURCE, "engine: allocation refused"), -1;
    return (int)hp->size();
}

static int engine_damage(vector *hp, int entity, int amount) {
    error e = error();
    int v;
    if (entity <= 0 || entity > (int)hp->size() || amount < 0)
        return error_fail_cat(&e, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                              "engine: invalid damage"), -1;
    v = hp->at((size_t)(entity - 1)) - amount;
    if (v < 0) v = 0;
    return v;
}

int main(void) {
    error e = error();
    stdlib_arena ar;
    stdlib_allocator A;
    vector pos;
    vector hp;
    int id, left, bad1, bad2, bad3, tick = 0;

    stdlib_arena_init(&ar, 8192u, &e);
    A = stdlib_arena_as_allocator(&ar);
    pos = vector_in(&A);
    hp = vector_in(&A);

    id = engine_spawn(&pos, &hp, 10, 20, 100);
    left = engine_damage(&hp, id, 30);
    tick++;
    bad1 = engine_spawn(&pos, &hp, 2000, 0, 10);
    bad2 = engine_damage(&hp, 999, 5);
    bad3 = engine_damage(&hp, id, -1);
    print("GAME_ENGINE", id, left, tick, bad1, bad2, bad3);
    pos.drop(); hp.drop();
    stdlib_arena_destroy(&ar, &e);
    return (id == 1 && left == 70 && tick == 1 && bad1 == -1 &&
            bad2 == -1 && bad3 == -1) ? 0 : 1;
}
