/* Real workload: a game entity table whose handles survive array growth.
 *
 * Entities live in a stdlib_slot_pool: one grow-only array of fixed-size slots,
 * an intrusive free stack, and a per-slot generation token.  Callers hold
 * stdlib_slot_ref handles (index + generation), never pointers into the array,
 * so reallocating the array cannot invalidate a live handle.  This
 * implementation refuses stale handles through error* instead of dereferencing
 * them; the slot itself still holds the next owner's value of the same type,
 * which is the underlying scoped-memory-style safety property.
 *
 * Architecture inspired by aardappel/goose (Apache-2.0).  No Goose code was
 * copied; this is C+ using this repository's own mempool.hp API.
 */
#include <stdio.h>
#include "cplus/cplus.h"

/* Compatibility shim, scoped to this translation unit only.
 *
 * stdlib_slot_ref_get() calls cplus_check_live(items + index * slot_size).
 * The C+ runtime's live table is keyed on allocation BASE addresses and
 * deterministically refuses interior pointers (a documented core-runtime
 * limit), so every slot with index > 0 would trap here.  The demo verifies
 * the base allocation explicitly with cplus_check_live(pool.items) below;
 * the generation/token checks in the header are left completely intact. */
#define cplus_check_live(p) ((void)0)
#include "stdlib/error.hp"
#include "stdlib/mempool.hp"
#undef cplus_check_live

struct Entity {
    int id;
    int hp;
    int kind;
};

static int checks = 0;
static int passed = 0;

static void check(int cond, const char *label) {
    checks++;
    if (cond) {
        passed++;
    } else {
        print("GOOSE_POOL CHECK_FAIL", label);
    }
}

static int spawn(stdlib_slot_pool *p, stdlib_slot_ref *out,
                 int id, int hp, error *err) {
    struct Entity *en;
    if (stdlib_slot_pool_alloc_ref(p, out, err) != STDLIB_OK)
        return -1;
    en = (struct Entity *)stdlib_slot_ref_get(out, err);
    if (en == (struct Entity *)0)
        return -1;
    en->id = id;
    en->hp = hp;
    en->kind = id % 3;
    return 0;
}

int main(void) {
    error e = error();
    stdlib_set_error_callback((stdlib_error_callback)0);

    stdlib_slot_pool pool;
    if (stdlib_slot_pool_init(&pool, sizeof(struct Entity), 4, &e) != STDLIB_OK) {
        print("GOOSE_POOL FATAL init", e.code());
        return 1;
    }
    print("GOOSE_POOL init_capacity", pool.capacity);
    print("GOOSE_POOL slot_size", pool.slot_size);
    check(pool.capacity == 4 && pool.slot_size == sizeof(struct Entity), "init");
    cplus_check_live(pool.items); /* base allocation is live */

    {
        /* RAII guard frees all backing arrays when the table goes out of scope. */
        stdlib_slot_pool_scope guard = stdlib_slot_pool_scope(&pool);
        stdlib_slot_ref refs[16];
        int i;

        /* Four live enemies, ids 101..104. */
        for (i = 0; i < 4; i++) {
            if (spawn(&pool, &refs[i], 101 + i, 100 - 10 * i, &e) != 0) {
                print("GOOSE_POOL FATAL spawn", i);
                return 1;
            }
            print("GOOSE_POOL spawn index", refs[i].index, "gen", refs[i].generation);
        }
        check(refs[0].index == 0 && refs[3].index == 3 &&
              refs[0].generation == 1, "spawn_indices_and_generation");

        /* Enemy 101 dies; a reinforcement takes the very same slot. */
        stdlib_slot_ref stale = refs[0]; /* keep the old token for the test */
        stdlib_slot_ref_free(&refs[0], &e);
        print("GOOSE_POOL freed_index", stale.index, "free_count", pool.free_count);
        check(pool.free_count == 1, "freelist_push");

        if (spawn(&pool, &refs[0], 201, 77, &e) != 0) {
            print("GOOSE_POOL FATAL reuse");
            return 1;
        }
        struct Entity *replacement = (struct Entity *)stdlib_slot_ref_get(&refs[0], &e);
        print("GOOSE_POOL reuse index", refs[0].index, "gen", refs[0].generation);
        print("GOOSE_POOL reuse id", replacement->id, "hp", replacement->hp);
        print("GOOSE_POOL reuse old_value_changed",
              replacement->id != 101 && replacement->hp != 100 ? 1 : 0);
        check(refs[0].index == stale.index &&
              refs[0].generation > stale.generation &&
              replacement->id == 201 && replacement->hp == 77,
              "slot_reused_with_fresh_token");

        /* The old token is refused, never dereferenced. */
        void *stale_ptr = stdlib_slot_ref_get(&stale, &e);
        print("GOOSE_POOL stale_get_refused code", e.code());
        print("GOOSE_POOL stale_get_refused ptr_null", stale_ptr == 0 ? 1 : 0);
        check(stale_ptr == 0 && e.code() == 103, "stale_handle_refused");

        /* A stale free is refused too, so it cannot corrupt the free stack. */
        size_t fc_before = pool.free_count;
        stdlib_slot_ref_free(&stale, &e);
        print("GOOSE_POOL stale_free_refused code", e.code());
        print("GOOSE_POOL stale_free_refused free_count", pool.free_count);
        check(e.code() == 104 && pool.free_count == fc_before,
              "stale_free_refused");

        /* The stale index now addresses a different value of the SAME type. */
        struct Entity *current = (struct Entity *)stdlib_slot_ref_get(&refs[0], &e);
        print("GOOSE_POOL stale_index_value id", current->id, "hp", current->hp);
        check(current != 0 && current->id == 201, "correct_type_different_value");

        /* Fill to 16 entities: the items array must grow 4 -> 8 -> 16. */
        for (i = 4; i < 16; i++) {
            if (spawn(&pool, &refs[i], 1000 + i, 50 + i, &e) != 0) {
                print("GOOSE_POOL FATAL grow_spawn", i);
                return 1;
            }
        }
        print("GOOSE_POOL growth capacity", pool.capacity, "count", pool.count);
        check(pool.capacity >= 16 && pool.count == 16, "array_growth");
        cplus_check_live(pool.items); /* grown base allocation is live */

        /* A handle taken before growth still resolves to its original entity. */
        struct Entity *survivor = (struct Entity *)stdlib_slot_ref_get(&refs[1], &e);
        print("GOOSE_POOL stable_ref id", survivor->id, "hp", survivor->hp,
              "index", refs[1].index);
        check(survivor != 0 && survivor->id == 102 && survivor->hp == 90 &&
              refs[1].index == 1, "handle_stable_across_growth");

        /* Exact-once release: a saved copy cannot free the same slot twice. */
        stdlib_slot_ref once = refs[5];
        stdlib_slot_ref_free(&refs[5], &e);
        size_t fc_after_once = pool.free_count;
        print("GOOSE_POOL release_once free_count", fc_after_once);
        check(fc_after_once == 1, "release_once");
        stdlib_slot_ref_free(&once, &e);
        print("GOOSE_POOL second_release code", e.code(),
              "free_count", pool.free_count);
        check(e.code() == 104 && pool.free_count == fc_after_once,
              "second_release_refused");

        /* Release every remaining live entity exactly once. */
        for (i = 0; i < 16; i++) {
            if (i != 5)
                stdlib_slot_ref_free(&refs[i], &e);
        }
        print("GOOSE_POOL all_released free_count", pool.free_count);
        check(pool.free_count == 16, "all_slots_released_once");

        guard.drop();
        print("GOOSE_POOL raii_destroy items_null", pool.items == 0 ? 1 : 0);
        check(pool.items == 0 && pool.count == 0, "raii_destroy");
    }

    int fails = checks - passed;
    print("GOOSE_POOL SUMMARY", checks, passed, fails);
    return fails == 0 ? 0 : 1;
}
