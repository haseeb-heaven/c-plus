/* C+ RAII over vendored CTL (rurban/ctl, MIT, pinned ede5f455...).
 *
 * Real workload: a top-N leaderboard built from a stream of score events.
 * Three distinct CTL container families do the work:
 *   vec_int   - unique player ids (de-duplication via CTL find)
 *   vec_entry - cumulative totals, then sorted with the CTL in-place sort
 *   deq_int   - rolling window of the last three score deltas
 *   str       - the formatted output line (pre-instantiated CTL string)
 *
 * CTL is plain C: it calls realloc/free directly and is not memory-safe on
 * its own.  Each container is owned by a small C+ class whose drop() calls
 * the matching CTL *_free exactly once (guarded by a `released` flag and by
 * the resource pointer, because C+ zeroes a local after an explicit drop).
 */
#include "cplus/cplus.h"
#include <stdio.h>

/* CTL string is pre-instantiated (`str`); include it before the T-based
 * containers so the template macros are in a known state. */
#include <ctl/string.h>

#include <stdlib.h>

typedef struct {
    int player;
    int total;
} entry;

/* CTL sort wants a 2-way "comes before" predicate: descending by total,
 * ascending by player id for ties. */
static int entry_before(entry *a, entry *b) {
    if (a->total != b->total)
        return a->total > b->total;
    return a->player < b->player;
}

#define POD
#define NOT_INTEGRAL
#define T entry
#include <ctl/vector.h> /* vec_entry */
#define POD
#define T int
#include <ctl/vector.h> /* vec_int */

#define POD
#define T int
#include <ctl/deque.h> /* deq_int */

/* Exact-once evidence: one increment per real CTL free. */
static int g_ctl_frees = 0;

class CtlBoard {
    vec_entry inner;
    int released;

    CtlBoard(void) {
        this->inner = vec_entry_init();
        this->inner.compare = entry_before;
        this->released = 0;
    }

    /* Accumulate points for one player using CTL's contiguous storage. */
    void accumulate(int player, int points) {
        size_t i;
        for (i = 0; i < this->inner.size; ++i) {
            if (this->inner.vector[i].player == player) {
                this->inner.vector[i].total += points;
                return;
            }
        }
        entry row;
        row.player = player;
        row.total = points;
        vec_entry_push_back(&this->inner, row);
    }

    void sort(void) { vec_entry_sort(&this->inner); }

    size_t size(void) { return vec_entry_size(&this->inner); }

    entry *at(size_t index) { return vec_entry_at(&this->inner, index); }

    void drop(void) {
        if (this->released || this->inner.vector == (entry *)0)
            return;
        this->released = 1;
        vec_entry_free(&this->inner);
        g_ctl_frees++;
        printf("CTL_FREE board\n");
    }
};

class CtlSeen {
    vec_int inner;
    int released;

    CtlSeen(void) {
        this->inner = vec_int_init();
        this->released = 0;
    }

    void add_unique(int player) {
        vec_int_it found = vec_int_find(&this->inner, player);
        if (vec_int_it_done(&found))
            vec_int_push_back(&this->inner, player);
    }

    size_t size(void) { return vec_int_size(&this->inner); }

    void drop(void) {
        if (this->released || this->inner.vector == (int *)0)
            return;
        this->released = 1;
        vec_int_free(&this->inner);
        g_ctl_frees++;
        printf("CTL_FREE seen\n");
    }
};

class CtlDeque {
    deq_int inner;
    size_t keep;
    int released;

    CtlDeque(size_t window) {
        this->inner = deq_int_init();
        this->keep = window;
        this->released = 0;
    }

    void add(int delta) {
        deq_int_push_back(&this->inner, delta);
        while (deq_int_size(&this->inner) > this->keep)
            deq_int_pop_front(&this->inner);
    }

    int sum(void) {
        size_t i;
        int total = 0;
        for (i = 0; i < deq_int_size(&this->inner); ++i)
            total += *deq_int_at(&this->inner, i);
        return total;
    }

    void drop(void) {
        if (this->released || this->inner.pages == (void *)0)
            return;
        this->released = 1;
        deq_int_free(&this->inner);
        g_ctl_frees++;
        printf("CTL_FREE window\n");
    }
};

class CtlStr {
    str inner;
    int released;

    CtlStr(const char *seed) {
        this->inner = str_init(seed);
        this->released = 0;
    }

    void add(const char *text) { str_append(&this->inner, text); }

    void add_int(int value) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", value);
        str_append(&this->inner, buf);
    }

    const char *text(void) { return str_c_str(&this->inner); }

    void drop(void) {
        if (this->released || this->inner.vector == (char *)0)
            return;
        this->released = 1;
        str_free(&this->inner);
        g_ctl_frees++;
        printf("CTL_FREE line\n");
    }
};

int main(void) {
    static const int players[] = {7, 2, 7, 9, 2, 5, 9, 7, 5, 3};
    static const int points[] = {3, 5, 4, 2, 6, 8, 7, 1, 2, 9};
    const size_t events = sizeof(players) / sizeof(players[0]);

    {
        CtlBoard ctl_board = CtlBoard();
        CtlSeen ctl_seen = CtlSeen();
        CtlDeque ctl_window = CtlDeque(3);
        CtlStr ctl_line = CtlStr("TOP");
        size_t i;

        for (i = 0; i < events; ++i) {
            ctl_seen.add_unique(players[i]);
            ctl_board.accumulate(players[i], points[i]);
            ctl_window.add(points[i]);
        }

        printf("CTL_COLLECTIONS events=%zu unique=%zu window=%zu\n", events,
               ctl_seen.size(), (size_t)ctl_window.sum());

        ctl_board.sort();
        for (i = 0; i < 3 && i < ctl_board.size(); ++i) {
            entry *row = ctl_board.at(i);
            ctl_line.add(" ");
            ctl_line.add_int(row->player);
            ctl_line.add(":");
            ctl_line.add_int(row->total);
        }
        printf("%s\n", ctl_line.text());

        /* Edge: repeated ids (players[] contains 7, 2 and 9 twice) already
         * hit CtlBoard.accumulate's update branch during the loop; call it
         * explicitly once more to keep that branch covered. */
        ctl_board.accumulate(players[1], 1);
        printf("CTL_EDGE duplicate_accumulate ok\n");

        /* Edge: double release of the same CtlBoard is a no-op (the zero-safe
         * guard in drop() keeps the second call from freeing twice). */
        ctl_board.drop();
        printf("CTL_EDGE double_drop ok\n");

        /* Explicit early release: C+ zeroes the object, then the compiler's
         * scope-exit drop runs again; the zero-safe guard makes it a no-op. */
        ctl_seen.drop();
        printf("CTL_EXPLICIT_DROP seen\n");
    }

    printf("CTL_RAII frees=%d\n", g_ctl_frees);
    return g_ctl_frees == 4 ? 0 : 1;
}
