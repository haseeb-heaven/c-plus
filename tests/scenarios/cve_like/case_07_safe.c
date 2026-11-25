/* BASELINE: case_07.c crashes; SAFE: caller-side ownership fix + checked APIs. */
/* Ordinary-C checked layer (no `_s` spelling in the program logic):
 *   default      -> stdlib/safe/safe_c.h
 *                   link build/safe/libcplus_safecrt.a + build/libc/libcplus_hardened.a
 *   -DPLAIN_LIBC -> libc/install-include/cplus_libc_safe.h
 *                   link build/libc/libcplus_hardened.a only
 *
 * HONESTY: the double free is caused by the caller keeping a pointer to memory
 * it already released. A library cannot make that raw pointer safe. The FIX is
 * caller-side: clear the owning slot BEFORE the release, so a repeated "op 3"
 * is a no-op. The library only refuses a release of an already-freed address
 * when the CHECKED entry point is used; a raw free() twice is still UB.
 */
#ifdef PLAIN_LIBC
#include "cplus_libc_safe.h"
#else
#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_runtime.h"
#endif
#include <stdio.h>
#include <stdlib.h>

struct Session { int authenticated; char *session_token; };

/* Caller-side discipline: single owner, slot cleared before/around the free.
 * `stale` is a DIAGNOSTIC copy only: it is never dereferenced, and the final
 * check below hands it exclusively to the checked free (which refuses it). */
static char *stale_diagnostic;

static int checked_release(char *p) {
#ifdef PLAIN_LIBC
    void *slot = p;
    return cplus_free_checked(&slot);   /* 0 = released, EINVAL = refused */
#else
    return free_checked_s(p);           /* CPLUS_SAFE_OK = released */
#endif
}

static void session_drop_token(struct Session *s) {
    char *old = s->session_token;
    if (!old) return;                   /* already released: nothing to do */
    s->session_token = NULL;            /* caller discipline: clear slot first */
    stale_diagnostic = old;             /* diagnostic only, never dereferenced */
    (void)checked_release(old);         /* library: tracked, refusal-safe free */
}

int main(void) {
    struct Session s = {.authenticated = 1, .session_token = malloc(64)};
    if (!s.session_token) return 1;

    int op = 0;
    while (scanf("%d", &op) == 1 && op != 0) {
        if (op == 1) s.authenticated = 0;
        else if (op == 3) session_drop_token(&s);
        else if (op == 2) {
            /* Only touch the token while it is actually owned. */
            if (s.session_token) s.session_token[0] = 'T';
            else puts("note: token already released; access skipped");
        }
    }

    int refused = -1;
    if (stale_diagnostic) {
        /* Library-side evidence: the checked API refuses the repeat release
         * without calling the host allocator; a raw free here would be UB. */
        refused = (checked_release(stale_diagnostic) == 0) ? 0 : 1;
    }
    stale_diagnostic = NULL;

    printf("outcome: SAFE | token slot cleared on first release; repeat op 3 no-op; "
           "checked re-release of stale token %s; checked tracked allocation active\n",
           refused == 1 ? "REFUSED by library" : "not attempted/invalid state");
    return 0;
}
