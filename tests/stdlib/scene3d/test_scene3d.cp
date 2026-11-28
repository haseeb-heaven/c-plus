/* stdlib/scene3d.hp conformance suite.
 *
 * Self-checking, deterministic, no stdin, no external library.  Every check
 * prints "<tag> <0|1>" (1 = passed) and the final line is
 * "SCENE3D <checks> <failures>".  Traversal/refusal/allocation/canary probes
 * print their exact observed values so run.sh can compare stdout byte-for-byte.
 *
 * Build (from the repository root):
 *   ./build/c+ build tests/stdlib/scene3d/test_scene3d.cp \
 *     --default --sanitize=none -I. -o tests/stdlib/scene3d/.run/test_scene3d \
 *     -- build/libc/libcplus_hardened.a
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/alloc_iface.hp"
#include "stdlib/scene3d.hp"

static int s3_checks = 0;
static int s3_failures = 0;
/* 52 unique single-character names for the depth-limit chain (<=32 nodes). */
static const char s3_alpha[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

static void s3_ck(const char *tag, int cond) {
    s3_checks++;
    if (!cond)
        s3_failures++;
    print(tag, cond ? 1 : 0);
}

/* Refusing allocator: every request fails without aborting. */
static void *s3_refuse_alloc(void *ctx, size_t size, size_t align) {
    (void)ctx; (void)size; (void)align;
    return (void *)0;
}
static void *s3_refuse_realloc(void *ctx, void *ptr, size_t old_size,
                               size_t new_size, size_t align) {
    (void)ctx; (void)ptr; (void)old_size; (void)new_size; (void)align;
    return (void *)0;
}
static void s3_noop_release(void *ctx, void *ptr, size_t size) {
    (void)ctx; (void)ptr; (void)size;
}
static void s3_noop_reset(void *ctx) { (void)ctx; }

typedef struct s3_canary_box {
    unsigned char head[16];
    scene3d_scene scene;
    unsigned char tail[16];
} s3_canary_box;

static int s3_all_byte(const unsigned char *p, size_t n, unsigned char value) {
    size_t i;
    for (i = 0; i < n; ++i)
        if (p[i] != value)
            return 0;
    return 1;
}

int main(void) {
    error e = error();
    stdlib_allocator heap;
    size_t base_live;
    scene3d_scene sc;
    scene3d_scene sc2;
    size_t a, b, a1, a2, b1;
    size_t out[64];
    size_t n;
    scene3d_transform t;
    scene3d_transform t2;
    char big[SCENE3D_MAX_NAME + 8];

    heap = stdlib_heap_allocator();
    stdlib_set_error_callback((stdlib_error_callback)0);
    stdlib_alloc_fail_reset();
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    base_live = stdlib_state.meta_live;

    /* ---- A: init / root -------------------------------------------------- */
    s3_ck("a_init", scene3d_init(&sc, &heap, &e) == STDLIB_OK);
    s3_ck("a_root", scene3d_root(&sc) == SCENE3D_ROOT_INDEX);
    s3_ck("a_root_name", strcmp(scene3d_node_name(&sc, SCENE3D_ROOT_INDEX), "root") == 0);
    s3_ck("a_root_parent", scene3d_node_parent(&sc, SCENE3D_ROOT_INDEX) == SCENE3D_NO_INDEX);
    s3_ck("a_root_visible", scene3d_node_is_visible(&sc, SCENE3D_ROOT_INDEX) == 1);
    s3_ck("a_count", scene3d_count(&sc) == 1u);
    s3_ck("a_find_root", scene3d_find(&sc, "root", &e) == SCENE3D_ROOT_INDEX);
    s3_ck("a_identity_scale", scene3d_transform_identity().scale.x == 1.0f);

    /* ---- B: build a deterministic tree ---------------------------------- */
    a = scene3d_node_add(&sc, SCENE3D_ROOT_INDEX, "a", &e);
    b = scene3d_node_add(&sc, SCENE3D_ROOT_INDEX, "b", &e);
    a1 = scene3d_node_add(&sc, a, "a1", &e);
    a2 = scene3d_node_add(&sc, a, "a2", &e);
    b1 = scene3d_node_add(&sc, b, "b1", &e);
    s3_ck("b_idx_a", a == 1u);
    s3_ck("b_idx_b", b == 2u);
    s3_ck("b_idx_a1", a1 == 3u);
    s3_ck("b_idx_a2", a2 == 4u);
    s3_ck("b_idx_b1", b1 == 5u);
    s3_ck("b_parent_a1", scene3d_node_parent(&sc, a1) == a);
    s3_ck("b_name_b1", strcmp(scene3d_node_name(&sc, b1), "b1") == 0);
    s3_ck("b_depth_b1", scene3d_node_depth(&sc, b1) == 2u);
    s3_ck("b_children_root", scene3d_node_child_count(&sc, SCENE3D_ROOT_INDEX) == 2u);
    s3_ck("b_children_a", scene3d_node_child_count(&sc, a) == 2u);
    s3_ck("b_child_at_root_1", scene3d_node_child_at(&sc, SCENE3D_ROOT_INDEX, 1) == b);

    /* ---- C: deterministic preorder traversal ---------------------------- */
    n = 99;
    s3_ck("c_traverse_ok", scene3d_traverse(&sc, SCENE3D_ROOT_INDEX, out, 64, &n, &e) == STDLIB_OK);
    s3_ck("c_traverse_count", n == 6u);
    print("traverse", out[0]); print("traverse", out[1]);
    print("traverse", out[2]); print("traverse", out[3]);
    print("traverse", out[4]); print("traverse", out[5]);
    s3_ck("c_traverse_order", out[0] == 0u && out[1] == 1u && out[2] == 3u &&
                              out[3] == 4u && out[4] == 2u && out[5] == 5u);
    n = 99;
    s3_ck("c_subtree_ok", scene3d_traverse(&sc, a, out, 64, &n, &e) == STDLIB_OK);
    s3_ck("c_subtree_count", n == 3u);
    print("subtree", out[0]); print("subtree", out[1]); print("subtree", out[2]);
    s3_ck("c_subtree_order", out[0] == 1u && out[1] == 3u && out[2] == 4u);

    /* ---- D: transform and visibility ------------------------------------ */
    t.position.x = 1.0f; t.position.y = 2.0f; t.position.z = 3.0f;
    t.rotation.x = 0.1f; t.rotation.y = 0.2f; t.rotation.z = 0.3f;
    t.scale.x = 2.0f; t.scale.y = 2.0f; t.scale.z = 2.0f;
    s3_ck("d_transform_set", scene3d_node_transform_set(&sc, a1, &t, &e) == STDLIB_OK);
    s3_ck("d_transform_get", scene3d_node_transform_get(&sc, a1, &t2, &e) == STDLIB_OK);
    s3_ck("d_transform_roundtrip", t2.position.x == 1.0f && t2.position.y == 2.0f &&
                                   t2.rotation.z == 0.3f && t2.scale.x == 2.0f);
    s3_ck("d_visible_off", scene3d_node_set_visible(&sc, a1, 0, &e) == STDLIB_OK &&
                           scene3d_node_is_visible(&sc, a1) == 0);
    s3_ck("d_visible_on", scene3d_node_set_visible(&sc, a1, 1, &e) == STDLIB_OK &&
                          scene3d_node_is_visible(&sc, a1) == 1);
    s3_ck("d_bad_transform_get", scene3d_node_transform_get(&sc, 999, &t2, &e) == STDLIB_FAIL);
    s3_ck("d_bad_visible", scene3d_node_is_visible(&sc, 999) == 0);

    /* ---- E: refusal cases (exact codes) --------------------------------- */
    s3_ck("e_duplicate", scene3d_node_add(&sc, SCENE3D_ROOT_INDEX, "a", &e) == SCENE3D_NO_INDEX &&
                         e.code() == CPLUS_EXIT_UNSUPPORTED);
    memset(big, 'x', sizeof(big));
    big[sizeof(big) - 1u] = '\0';
    s3_ck("e_oversized_name", scene3d_node_add(&sc, SCENE3D_ROOT_INDEX, big, &e) == SCENE3D_NO_INDEX &&
                              e.code() == CPLUS_EXIT_UNSUPPORTED);
    s3_ck("e_invalid_parent", scene3d_node_add(&sc, 999, "x", &e) == SCENE3D_NO_INDEX &&
                              e.code() == CPLUS_EXIT_BOUNDS);
    s3_ck("e_self_cycle", scene3d_node_reparent(&sc, a, a, &e) == STDLIB_FAIL &&
                          e.code() == CPLUS_EXIT_OWNERSHIP);
    s3_ck("e_descendant_cycle", scene3d_node_reparent(&sc, a, a1, &e) == STDLIB_FAIL &&
                                e.code() == CPLUS_EXIT_OWNERSHIP);
    s3_ck("e_remove_root", scene3d_node_remove(&sc, SCENE3D_ROOT_INDEX, &e) == STDLIB_FAIL &&
                           e.code() == CPLUS_EXIT_OWNERSHIP);
    n = 99;
    s3_ck("e_small_buffer", scene3d_traverse(&sc, SCENE3D_ROOT_INDEX, out, 3, &n, &e) == STDLIB_FAIL &&
                            n == 0u && e.code() == CPLUS_EXIT_BOUNDS);
    s3_ck("e_bad_find", scene3d_find(&sc, big, &e) == SCENE3D_NO_INDEX);

    /* depth limit: build a chain in a separate scene to keep `sc` stable. */
    s3_ck("e_sc2_init", scene3d_init(&sc2, &heap, &e) == STDLIB_OK);
    {
        size_t tip = SCENE3D_ROOT_INDEX;
        size_t step = 0;
        char nm[8];
        while (scene3d_node_depth(&sc2, tip) < SCENE3D_MAX_DEPTH) {
            nm[0] = s3_alpha[step];
            nm[1] = '\0';
            tip = scene3d_node_add(&sc2, tip, nm, &e);
            step++;
        }
        s3_ck("e_chain_depth", scene3d_node_depth(&sc2, tip) == SCENE3D_MAX_DEPTH);
        s3_ck("e_depth_refusal", scene3d_node_add(&sc2, tip, "too_deep", &e) == SCENE3D_NO_INDEX &&
                                 e.code() == CPLUS_EXIT_BOUNDS);
    }
    scene3d_drop(&sc2);

    /* ---- F: reparent changes links and preorder -------------------------- */
    s3_ck("f_reparent", scene3d_node_reparent(&sc, b1, a, &e) == STDLIB_OK);
    s3_ck("f_new_parent", scene3d_node_parent(&sc, b1) == a);
    s3_ck("f_old_parent_children", scene3d_node_child_count(&sc, b) == 0u);
    s3_ck("f_new_parent_children", scene3d_node_child_count(&sc, a) == 3u);
    n = 99;
    s3_ck("f_traverse_ok", scene3d_traverse(&sc, SCENE3D_ROOT_INDEX, out, 64, &n, &e) == STDLIB_OK);
    s3_ck("f_traverse_count", n == 6u);
    print("reparent", out[0]); print("reparent", out[1]);
    print("reparent", out[2]); print("reparent", out[3]);
    print("reparent", out[4]); print("reparent", out[5]);
    s3_ck("f_traverse_order", out[0] == 0u && out[1] == 1u && out[2] == 3u &&
                              out[3] == 4u && out[4] == 5u && out[5] == 2u);
    s3_ck("f_root_fixed", scene3d_node_reparent(&sc, SCENE3D_ROOT_INDEX, a, &e) == STDLIB_FAIL);

    /* ---- G: removal leaves indices stable -------------------------------- */
    s3_ck("g_remove_leaf", scene3d_node_remove(&sc, a2, &e) == STDLIB_OK);
    s3_ck("g_leaf_gone", scene3d_find(&sc, "a2", &e) == SCENE3D_NO_INDEX);
    s3_ck("g_live_count", scene3d_count(&sc) == 5u);
    s3_ck("g_parent_children", scene3d_node_child_count(&sc, a) == 2u);
    s3_ck("g_tombstone_transform", scene3d_node_transform_get(&sc, a2, &t2, &e) == STDLIB_FAIL);
    s3_ck("g_remove_subtree", scene3d_node_remove(&sc, a, &e) == STDLIB_OK);
    s3_ck("g_subtree_gone", scene3d_find(&sc, "a", &e) == SCENE3D_NO_INDEX &&
                            scene3d_find(&sc, "a1", &e) == SCENE3D_NO_INDEX &&
                            scene3d_find(&sc, "b1", &e) == SCENE3D_NO_INDEX);
    s3_ck("g_count_after", scene3d_count(&sc) == 2u);
    n = 99;
    s3_ck("g_traverse_ok", scene3d_traverse(&sc, SCENE3D_ROOT_INDEX, out, 64, &n, &e) == STDLIB_OK);
    print("after_remove", out[0]); print("after_remove", out[1]);
    s3_ck("g_traverse_order", n == 2u && out[0] == 0u && out[1] == 2u);

    /* ---- H: allocation failure is recoverable and atomic ---------------- */
    {
        stdlib_allocator bad;
        scene3d_scene sf;
        bad.ctx = (void *)0;
        bad.alloc = s3_refuse_alloc;
        bad.realloc = s3_refuse_realloc;
        bad.release = s3_noop_release;
        bad.reset = s3_noop_reset;
        s3_ck("h_bad_init", scene3d_init(&sf, &bad, &e) == STDLIB_FAIL);
        s3_ck("h_bad_init_clean", sf.nodes == (scene3d_node *)0 && sf.count == 0u);
    }
    {
        scene3d_scene si;
        size_t r;
        size_t kid;
        size_t branch;
        size_t k;
        char nm[2];
        s3_ck("h_si_init", scene3d_init(&si, &heap, &e) == STDLIB_OK);
        /* Node vector has spare capacity: this add needs one child-list
         * allocation, which is refused. */
        stdlib_alloc_fail_at(1);
        r = scene3d_node_add(&si, SCENE3D_ROOT_INDEX, "fail_node", &e);
        stdlib_alloc_fail_reset();
        s3_ck("h_add_alloc", r == SCENE3D_NO_INDEX);
        s3_ck("h_add_state", scene3d_count(&si) == 1u &&
                             scene3d_find(&si, "fail_node", &e) == SCENE3D_NO_INDEX);
        /* One-node subtree removal needs exactly one scratch allocation. */
        kid = scene3d_node_add(&si, SCENE3D_ROOT_INDEX, "kid", &e);
        s3_ck("h_kid_added", kid != SCENE3D_NO_INDEX);
        stdlib_alloc_fail_at(1);
        s3_ck("h_remove_alloc1", scene3d_node_remove(&si, kid, &e) == STDLIB_FAIL);
        stdlib_alloc_fail_reset();
        s3_ck("h_remove_still_alive", scene3d_find(&si, "kid", &e) == kid);
        /* Six-node subtree: the scratch vector grows once (allocation 2). */
        branch = scene3d_node_add(&si, SCENE3D_ROOT_INDEX, "branch", &e);
        for (k = 0; k < 5u; ++k) {
            nm[0] = s3_alpha[k];
            nm[1] = '\0';
            (void)scene3d_node_add(&si, branch, nm, &e);
        }
        stdlib_alloc_fail_at(2);
        s3_ck("h_remove_alloc2", scene3d_node_remove(&si, branch, &e) == STDLIB_FAIL);
        stdlib_alloc_fail_reset();
        s3_ck("h_remove_alloc2_state", scene3d_find(&si, "branch", &e) == branch &&
                                       scene3d_count(&si) == 8u);
        scene3d_drop(&si);
    }

    /* ---- I: canary bytes around the scene are untouched ------------------ */
    {
        s3_canary_box box;
        size_t kid;
        memset(box.head, 0xA5, sizeof(box.head));
        memset(box.tail, 0x5A, sizeof(box.tail));
        s3_ck("i_box_init", scene3d_init(&box.scene, &heap, &e) == STDLIB_OK);
        kid = scene3d_node_add(&box.scene, SCENE3D_ROOT_INDEX, "canary_kid", &e);
        s3_ck("i_box_add", kid != SCENE3D_NO_INDEX);
        s3_ck("i_box_transform", scene3d_node_transform_set(&box.scene, kid, &t, &e) == STDLIB_OK);
        s3_ck("i_box_remove", scene3d_node_remove(&box.scene, kid, &e) == STDLIB_OK);
        s3_ck("i_canary_head", s3_all_byte(box.head, sizeof(box.head), 0xA5));
        s3_ck("i_canary_tail", s3_all_byte(box.tail, sizeof(box.tail), 0x5A));
        scene3d_drop(&box.scene);
        s3_ck("i_canary_after_drop", s3_all_byte(box.head, sizeof(box.head), 0xA5) &&
                                     s3_all_byte(box.tail, sizeof(box.tail), 0x5A));
    }

    /* Direct reach for public helpers (coverage tool call sites). */
    (void)scene3d_live(&sc, 0);
    (void)scene3d_name_ok("root");
    (void)scene3d_subtree_height(&sc, 0);

    scene3d_drop(&sc);
    s3_ck("z_meta_balance", stdlib_state.meta_live == base_live);
    print("SCENE3D", s3_checks, s3_failures);
    return s3_failures == 0 ? 0 : 1;
}
