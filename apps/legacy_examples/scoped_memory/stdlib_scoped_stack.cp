/* Real workload: one game frame's transient particle buffer and diagnostic log.
 *
 * Both containers are allocator-aware and bound to the calling thread's
 * scoped-memory-style VM data stack.  Every byte they allocate dies when the frame's
 * scratch scope is dropped, regardless of how many times the containers grew.
 * The only pointer reported outside this frame is an offset from ds->base,
 * so the output is deterministic even though the reservation address is not.
 *
 * Architecture inspired by aardappel/goose (Apache-2.0).  No Goose code was
 * copied; this is C+ using this repository's own datastack/vector/string APIs.
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/alloc_iface.hp"
#include "stdlib/datastack.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"

static int checks = 0;
static int passed = 0;

static void check(int cond, const char *label) {
    checks++;
    if (cond) {
        passed++;
    } else {
        print("SCOPED_STACK CHECK_FAIL", label);
    }
}

int main(void) {
    error e = error();
    stdlib_set_error_callback((stdlib_error_callback)0);

    stdlib_datastack *ds = stdlib_thread_datastack(&e);
    if (ds == (stdlib_datastack *)0) {
        print("SCOPED_STACK FATAL datastack", e.code());
        return 1;
    }
    stdlib_allocator scratch = stdlib_scratch_allocator();

    size_t enter_offset = (size_t)(ds->top - ds->base);
    print("SCOPED_STACK enter_offset", enter_offset);
    check(ds->capacity > 0, "vm_reservation");

    {
        /* RAII guard: records top + generation; drop() restores top in one
         * store.  It is dropped explicitly at the end of the simulated frame. */
        stdlib_scratch_scope frame = stdlib_scratch_scope(ds);

        /* A direct data-stack allocation made BEFORE either container grows.
         * Its address must survive every later bump and reallocation. */
        int *anchor = (int *)stdlib_datastack_alloc(ds, sizeof(int), 4u, &e);
        if (anchor == (int *)0) {
            print("SCOPED_STACK FATAL anchor", e.code());
            return 1;
        }
        *anchor = 4242;
        size_t anchor_offset = (size_t)((unsigned char *)anchor - ds->base);
        print("SCOPED_STACK anchor_offset", anchor_offset);
        print("SCOPED_STACK anchor_value", *anchor);
        check(anchor_offset >= enter_offset, "anchor_inside_frame");

        /* Allocator-aware containers: no per-element heap bookkeeping. */
        vector particles = vector_in(&scratch);
        string log = string_in(&scratch, "");
        check(particles.size() == 0 && log.size() == 0, "containers_start_empty");

        /* 40 particle ids force vector growth 4 -> 8 -> 16 -> 32 -> 64. */
        int i;
        for (i = 0; i < 40; i++) {
            if (particles.push_back(1000 + i * 7) != 0) {
                print("SCOPED_STACK FATAL push", i);
                return 1;
            }
        }
        print("SCOPED_STACK vector_size", particles.size());
        print("SCOPED_STACK vector_cap", particles.capacity());
        check(particles.size() == 40 && particles.capacity() >= 40,
              "vector_growth");
        check(particles.at(0) == 1000 && particles.at(39) == 1273,
              "vector_contents_after_growth");

        /* The per-frame log grows in place on the same data stack.  c_str()
         * exposes the buffer address; it must change when the string grows. */
        const char *log_before = log.c_str();
        size_t log_before_offset = (size_t)(log_before - (const char *)ds->base);
        for (i = 0; i < 48; i++) {
            if (log.append("frame:42 tick\n") != 0) {
                print("SCOPED_STACK FATAL append", i);
                return 1;
            }
        }
        const char *log_after = log.c_str();
        size_t log_after_offset = (size_t)(log_after - (const char *)ds->base);
        print("SCOPED_STACK log_size", log.size());
        print("SCOPED_STACK log_cap", log.capacity());
        print("SCOPED_STACK log_buffer_moved", log_after != log_before ? 1 : 0);
        print("SCOPED_STACK log_offset_before", log_before_offset);
        print("SCOPED_STACK log_offset_after", log_after_offset);
        check(log.size() == 48u * 14u, "log_growth");

        /* Stability proof: both container buffers moved, but the earlier
         * data-stack allocation kept its exact address and value. */
        size_t anchor_after = (size_t)((unsigned char *)anchor - ds->base);
        print("SCOPED_STACK stable_anchor_offset", anchor_after);
        print("SCOPED_STACK stable_anchor_value", *anchor);
        check(anchor_after == anchor_offset && *anchor == 4242,
              "anchor_stable_across_container_growth");

        /* Edge: a scope with no allocations restores top to the point it was
         * at when the scope was created; its drop is a no-op, not an error. */
        {
            size_t top_before_empty = (size_t)(ds->top - ds->base);
            stdlib_scratch_scope empty = stdlib_scratch_scope(ds);
            empty.drop();
            print("SCOPED_STACK empty_scope_top_ok",
                  (size_t)(ds->top - ds->base) == top_before_empty ? 1 : 0);
            check((size_t)(ds->top - ds->base) == top_before_empty,
                  "empty_scope_restores");
        }

        /* Edge: nested scopes -- inner allocations die at the inner drop,
         * leaving the outer frame's allocations untouched. */
        {
            stdlib_scratch_scope nested = stdlib_scratch_scope(ds);
            size_t top_before_nested = (size_t)(ds->top - ds->base);
            int *inner = (int *)stdlib_datastack_alloc(ds, sizeof(int), 4u, &e);
            if (inner == (int *)0) {
                print("SCOPED_STACK FATAL nested");
                return 1;
            }
            *inner = 77;
            print("SCOPED_STACK nested_grew",
                  (size_t)(ds->top - ds->base) > top_before_nested ? 1 : 0);
            nested.drop();
            print("SCOPED_STACK nested_drop_restored",
                  (size_t)(ds->top - ds->base) == top_before_nested ? 1 : 0);
            check((size_t)(ds->top - ds->base) == top_before_nested,
                  "nested_scope_drop_restores");
        }

        /* One-store free: show the live top, drop the frame, show it rewound. */
        size_t top_before = (size_t)(ds->top - ds->base);
        print("SCOPED_STACK frame_top_before", top_before);
        log.drop();
        particles.drop();
        frame.drop();
        size_t top_after = (size_t)(ds->top - ds->base);
        print("SCOPED_STACK frame_top_after", top_after);
        check(top_after == enter_offset, "scope_exit_one_store");
    }

    int fails = checks - passed;
    print("SCOPED_STACK SUMMARY", checks, passed, fails);
    return fails == 0 ? 0 : 1;
}
