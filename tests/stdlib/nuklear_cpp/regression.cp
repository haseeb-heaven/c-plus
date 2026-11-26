/* Deterministic regressions for issue #87:
 *  - NuklearScratchFrame captures ds->generation in its constructor and
 *    drop() really rewinds ds->top (including nested frames).
 *  - init() never returns from inside TRY, so a successful init leaves no
 *    exceptions4c block behind and a later TRY/THROW still works. */
#include "cplus/cplus.h"
#include "stdlib/nuklear_cpp.hp"
#include "stdlib/safe/safe_except.h"

static const struct e4c_exception_type REG_LATER_FAIL = {
    (const struct e4c_exception_type *)0, "NuklearRegLaterFail"};

static volatile int later_stage = 0;

static int later_throw(void) {
    volatile int seen = 0;
    TRY {
        later_stage = 1;
        THROW(REG_LATER_FAIL, "later throw");
        later_stage = 2; /* unreachable */
    }
    CATCH (REG_LATER_FAIL) {
        seen = 1;
    }
    CATCH_ALL {
        seen = 2;
    }
    return (int)seen + (int)later_stage * 10; /* 11 == caught by REG_LATER_FAIL */
}

int main(void) {
    error e = error();
    stdlib_datastack *ds = stdlib_thread_datastack(&e);
    unsigned char *base_top;
    int fails = 0;
    int i;

    cplus_except_init();
    if (ds == (stdlib_datastack *)0) { print("NK_REG FATAL datastack"); return 2; }
    base_top = ds->top;

    /* Nested frames: each drop must restore the top captured by its own
     * constructor, even after several bumps of the data stack. */
    {
        NuklearScratchFrame outer = NuklearScratchFrame(ds);
        unsigned char *outer_top;
        if (outer.ok() == 0) fails++;
        outer_top = ds->top;
        for (i = 0; i < 4; ++i) {
            NuklearScratchFrame inner = NuklearScratchFrame(ds);
            if (inner.ok() == 0) fails++;
            if (stdlib_datastack_alloc(ds, 64u, 16u, &e) == (void *)0) fails++;
            if (ds->top == outer_top) fails++; /* the bump is visible */
            inner.drop();
            if (ds->top != outer_top) { fails++; print("NK_REG inner rewind", i); }
        }
        outer.drop();
        if (ds->top != base_top) { fails++; print("NK_REG outer rewind", 0); }
    }

    /* Successful init: the CATCH chain and the TRY frame must be unwound
     * before init() returns. e4c_get_env() is NULL with no active TRY. */
    {
        struct nk_context ctx;
        NuklearScratchFrame f = NuklearScratchFrame(ds);
        int rc;
        if (e4c_get_env() != (e4c_env *)0) { fails++; print("NK_REG env before init", 0); }
        rc = f.init(&ctx, (const struct nk_user_font *)0);
        if (rc != 1 || f.ok() != 1) { fails++; print("NK_REG init", rc); }
        if (e4c_get_env() != (e4c_env *)0) { fails++; print("NK_REG env after init", 0); }
        f.drop();
        if (ds->top != base_top) { fails++; print("NK_REG init rewind", 0); }
    }

    /* Refusal path (THROW -> CATCH) followed by a later THROW in another
     * function: the later TRY/THROW must catch its own exception. */
    {
        NuklearScratchFrame f = NuklearScratchFrame(ds);
        int rc = f.init((struct nk_context *)0, (const struct nk_user_font *)0);
        if (rc != 0 || f.ok() != 0) { fails++; print("NK_REG refused", rc); }
        f.drop();
    }
    if (later_throw() != 11) { fails++; print("NK_REG later throw", 0); }
    if (e4c_get_env() != (e4c_env *)0) { fails++; print("NK_REG env after throw", 0); }
    if (ds->top != base_top) { fails++; print("NK_REG final top", 0); }

    print("NK_REG fails", fails);
    return fails ? 1 : 0;
}
