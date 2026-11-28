/* C+ style proof: class + TRY/CATCH + logs + scratch allocator.
 * Also covers issue #87: frame_gen must rewind the datastack and init() must
 * not return from inside TRY (which leaves a dead exceptions4c frame). */
#include "cplus/cplus.h"
#include "stdlib/nuklear_cpp.hp"

int main(void) {
    error e = error();
    stdlib_datastack *ds = stdlib_thread_datastack(&e);
    NuklearScratchFrame frame = NuklearScratchFrame(ds);
    int init_rc = frame.init((struct nk_context *)0, (const struct nk_user_font *)0); /* forced TRY/CATCH refusal */
    int ok = frame.ok();
    int fails = 0;
    int caught = 0;
    size_t before, after;
    struct nk_context ctx;

    frame.drop();
    print("NK_CPP init", init_rc);
    print("NK_CPP ok", ok);
    if (init_rc != 0 || ok != 0) fails++;

    /* A frame must restore the saved top even when allocations are made
     * between construction and drop(). */
    before = (size_t)(ds->top - ds->base);
    {
        NuklearScratchFrame f2 = NuklearScratchFrame(ds);
        if (stdlib_datastack_alloc(ds, 64u, 16u, &e) == (void *)0) fails++;
        if (f2.init(&ctx, (const struct nk_user_font *)0) != 1) fails++;
        nk_free(&ctx);
        f2.drop();
    }
    after = (size_t)(ds->top - ds->base);
    if (after != before) fails++;

    /* If init() returned from inside TRY, this THROW longjmps into a dead
     * stack frame and the process crashes. */
    TRY {
        THROW(NUKLEAR_INIT_FAIL, "post-frame throw");
    }
    CATCH (NUKLEAR_INIT_FAIL) {
        caught = 1;
    }
    CATCH_ALL {
    }
    if (caught == 0) fails++;

    print("NK_CPP regress", fails);
    return (init_rc == 0 && ok == 0 && fails == 0) ? 0 : 1;
}
