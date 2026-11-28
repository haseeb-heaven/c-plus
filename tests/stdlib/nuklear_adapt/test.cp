/* Nuklear-on-scratch allocator proof: allocations come from the VM data
 * stack and are reclaimed by one scope-store, not per-object free. */
#include "cplus/cplus.h"
#include "stdlib/nuklear_adapt.hp"

int main(void) {
    error e = error();
    stdlib_datastack *ds = stdlib_thread_datastack(&e);
    struct nk_allocator a;
    int fails = 0;
    size_t before, after;
    void *p;
    int i;

    stdlib_set_error_callback((stdlib_error_callback)0);
    if (ds == (stdlib_datastack *)0) { print("NK_ADAPT FATAL datastack"); return 1; }
    {
        stdlib_scratch_scope frame = stdlib_scratch_scope(ds);
        before = (size_t)(ds->top - ds->base);
        nuklear_scratch_allocator(&a, ds);
        if (a.alloc == 0 || a.free == 0 || a.userdata.ptr != (void *)ds)
            fails++;
        for (i = 0; i < 64; ++i) {
            p = a.alloc(a.userdata, (void *)0, 32);
            if (p == (void *)0) { fails++; break; }
            if ((unsigned char *)p < ds->base ||
                (unsigned char *)p >= ds->base + ds->capacity) fails++;
            a.free(a.userdata, p);   /* no-op, by design */
        }
        if ((size_t)(ds->top - ds->base) <= before) fails++;
        frame.drop();
        after = (size_t)(ds->top - ds->base);
        if (after != before) fails++;
    }
    print("NK_ADAPT", fails);
    return fails ? 1 : 0;
}
