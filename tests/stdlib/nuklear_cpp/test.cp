/* C+ style proof: class + TRY/CATCH + logs + scratch allocator. */
#include "cplus/cplus.h"
#include "stdlib/nuklear_cpp.hp"

int main(void) {
    error e = error();
    stdlib_datastack *ds = stdlib_thread_datastack(&e);
    NuklearScratchFrame frame = NuklearScratchFrame(ds);
    int init_rc = frame.init((struct nk_context *)0, (const struct nk_user_font *)0); /* forced TRY/CATCH refusal */
    int ok = frame.ok();
    frame.drop();
    print("NK_CPP init", init_rc);
    print("NK_CPP ok", ok);
    return (init_rc == 0 && ok == 0) ? 0 : 1;
}
