/* C+ hybrid: CTL owns the persistent index; the Goose-inspired DataStack
 * owns transient query data.
 *
 * Real workload: aggregate a stream of document ids into a persistent CTL
 * vector of (id, count) rows, then use a scratch allocator-backed C+ vector
 * and string inside one stdlib_scratch_scope to build a rolling window,
 * format its label and derive a hot-id bonus.  The bonus is written back into
 * the persistent CTL container, which is then sorted and printed.  Leaving
 * the scope restores the data stack top with a single store.
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/alloc_iface.hp"
#include "stdlib/datastack.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"
#include <stdio.h>

typedef struct {
    int id;
    int count;
} score;

static int score_before(score *a, score *b) {
    if (a->count != b->count)
        return a->count > b->count;
    return a->id < b->id;
}

#define POD
#define NOT_INTEGRAL
#define T score
#include <ctl/vector.h> /* vec_score: the persistent, typed CTL container */

static int g_ctl_frees = 0;

/* The only owner of the CTL allocation.  drop() is zero-safe because C+
 * zeroes a local after an explicit drop() and re-runs the scope-exit drop. */
class CtlBoard {
    vec_score inner;
    int released;

    CtlBoard(void) {
        this->inner = vec_score_init();
        this->inner.compare = score_before;
        this->released = 0;
    }

    void add(int id) {
        size_t i;
        for (i = 0; i < this->inner.size; ++i) {
            if (this->inner.vector[i].id == id) {
                this->inner.vector[i].count++;
                return;
            }
        }
        score row;
        row.id = id;
        row.count = 1;
        vec_score_push_back(&this->inner, row);
    }

    void bump(int id, int bonus) {
        size_t i;
        for (i = 0; i < this->inner.size; ++i)
            if (this->inner.vector[i].id == id)
                this->inner.vector[i].count += bonus;
    }

    void sort(void) { vec_score_sort(&this->inner); }

    size_t size(void) { return vec_score_size(&this->inner); }

    score *at(size_t index) { return vec_score_at(&this->inner, index); }

    void drop(void) {
        if (this->released || this->inner.vector == (score *)0)
            return;
        this->released = 1;
        vec_score_free(&this->inner);
        g_ctl_frees++;
        printf("CTL_FREE board\n");
    }
};

int main(void) {
    static const int stream[] = {4, 1, 4, 2, 3, 1, 4, 2};
    const size_t events = sizeof(stream) / sizeof(stream[0]);
    const size_t window_len = 5;
    error err = error();
    stdlib_datastack *ds;
    size_t baseline;
    int bonus_id = 0;
    int bonus = 0;
    int scratch_ok = 1;

    stdlib_set_error_callback((stdlib_error_callback)0);
    ds = stdlib_thread_datastack(&err);
    if (ds == (stdlib_datastack *)0) {
        printf("HYBRID datastack=unavailable\n");
        return 1;
    }
    baseline = (size_t)(ds->top - ds->base);

    {
        CtlBoard ctl_board = CtlBoard();
        size_t i;

        for (i = 0; i < events; ++i)
            ctl_board.add(stream[i]);

        {
            /* Scope declared FIRST so its drop runs LAST: the scratch
             * containers release before the top is rewound. */
            stdlib_allocator A = stdlib_scratch_allocator();
            stdlib_scratch_scope scope = stdlib_scratch_scope(ds);
            vector scratch_window = vector_in(&A);
            string scratch_label = string_in(&A, "W");
            char buf[32];

            for (i = events - window_len; i < events; ++i) {
                int id = stream[i];
                if (scratch_window.push_back(id) != 0)
                    scratch_ok = 0;
                if (i > events - window_len)
                    if (scratch_label.append(",") != 0)
                        scratch_ok = 0;
                snprintf(buf, sizeof(buf), "%d", id);
                if (scratch_label.append(buf) != 0)
                    scratch_ok = 0;
            }

            bonus_id = scratch_window.at(0);
            for (i = 0; i < scratch_window.size(); ++i)
                if (scratch_window.at(i) == bonus_id)
                    bonus++;

            ctl_board.bump(bonus_id, bonus);

            printf("HYBRID scratch_ok=%d window=%zu bonus_id=%d bonus=%d\n",
                   scratch_ok, scratch_window.size(), bonus_id, bonus);
            printf("HYBRID scratch_label=%s\n", scratch_label.c_str());
            printf("HYBRID top_grew=%d\n",
                   (size_t)(ds->top - ds->base) > baseline ? 1 : 0);
        }

        printf("HYBRID top_restored=%d\n",
               (size_t)(ds->top - ds->base) == baseline ? 1 : 0);

        ctl_board.sort();
        printf("HYBRID board=");
        for (i = 0; i < ctl_board.size(); ++i) {
            score *row = ctl_board.at(i);
            printf("%s%d:%d", i == 0 ? "" : " ", row->id, row->count);
        }
        printf("\n");
    }

    printf("HYBRID ctl_free=%d\n", g_ctl_frees);
    return (scratch_ok == 1 && bonus == 2 && g_ctl_frees == 1) ? 0 : 1;
}
