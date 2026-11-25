/* BASELINE: case_08.c crashes; SAFE: caller-side ownership fix + checked APIs. */
/* Ordinary-C checked layer (no `_s` spelling in the program logic):
 *   default      -> stdlib/safe/safe_c.h
 *                   link build/safe/libcplus_safecrt.a + build/libc/libcplus_hardened.a
 *   -DPLAIN_LIBC -> libc/install-include/cplus_libc_safe.h
 *                   link build/libc/libcplus_hardened.a only
 *
 * HONESTY: writing to a node after free() is raw-C UB. A library cannot make a
 * dangling pointer safe. The FIX is caller-side: save `next` before releasing,
 * unlink the node, scrub it while it is still LIVE, and advance via the saved
 * pointer. The library only (a) routes malloc/free/memset through checked,
 * tracked entry points and (b) offers a provenance query used as diagnosis.
 */
#ifdef PLAIN_LIBC
#include "cplus_libc_safe.h"
#define CPLUS_IS_LIVE(p) cplus_alloc_is_live(p)
#else
#include "stdlib/safe/safe_c.h"
#include "stdlib/safe/safe_runtime.h"
#define CPLUS_IS_LIVE(p) cplus_safe_ptr_is_live(p)
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

struct Item { uint32_t tag; struct Item *next; };

/* Caller discipline for every release in this file: save next, unlink, then
 * free; never touch the released node again. */
static void free_rest(struct Item *c) {
    while (c) {
        struct Item *next = c->next;
        free(c);
        c = next;
    }
}

int main(void) {
    uint32_t tag = 0;
    printf("tag: ");
    if (scanf("%u", &tag) != 1) return 1;

    struct Item *head = NULL;
    for (uint32_t i = 1; i <= 5; ++i) {
        struct Item *n = malloc(sizeof *n);
        if (!n) { free_rest(head); return 1; }
        n->tag = i;
        n->next = head;
        head = n;
    }

    unsigned removed = 0;
    struct Item *c = head, *prev = NULL;
    while (c) {
        struct Item *next = c->next;    /* caller discipline: save BEFORE free */

        /* Library-side diagnosis only; the discipline above is the real fix. */
        if (!CPLUS_IS_LIVE(c)) {
            free_rest(head);
            puts("outcome: REFUSED | tracked allocator reported a stale node");
            return 3;
        }

        if (c->tag == tag) {
            if (prev) prev->next = next;
            else head = next;           /* unlink: no live owner keeps a stale ptr */
            memset(c, 0xFF, sizeof *c); /* scrub while LIVE (capacity checked) */
            free(c);                    /* checked/tracked free via ordinary name */
            c = next;                   /* advance via the SAVED pointer */
            ++removed;
        } else {
            prev = c;
            c = next;
        }
    }
    free_rest(head);                    /* clean up remaining owned nodes */

    printf("outcome: SAFE | removed=%u nodes; saved next before free + unlinked + "
           "scrubbed while live; checked tracked allocation active\n", removed);
    return 0;
}
