/*
 * lc50 - combined ABI probe: opaque context, tagged union events, function
 * pointer dispatch table, and malloc-backed state machine. Independent C99/C11
 * program representing the final entry of the legacy-C corpus.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct lc50_machine; /* opaque */

enum lc50_event_kind { EV_SET = 1, EV_ADD = 2, EV_RESET = 3 };

struct lc50_event {
    enum lc50_event_kind kind;
    union {
        int value;
        unsigned char raw[4];
    } payload;
};

struct lc50_state {
    int current;
    int transitions;
    char history[32];
    size_t used;
};

struct lc50_machine {
    unsigned int magic;
    struct lc50_state st;
};

#define LC50_MAGIC 0xC50u

static void ev_set(struct lc50_state *s, const struct lc50_event *e)
{
    s->current = e->payload.value;
}

static void ev_add(struct lc50_state *s, const struct lc50_event *e)
{
    s->current += e->payload.value;
}

static void ev_reset(struct lc50_state *s, const struct lc50_event *e)
{
    (void)e;
    s->current = 0;
}

typedef void (*handler_fn)(struct lc50_state *, const struct lc50_event *);

static const handler_fn handlers[4] = {
    NULL, ev_set, ev_add, ev_reset
};

static const char handler_syms[4] = { '?', 'S', 'A', 'R' };

struct lc50_machine *lc50_create(void)
{
    struct lc50_machine *m =
        (struct lc50_machine *)malloc(sizeof(*m));
    if (m == NULL) {
        return NULL;
    }
    m->magic = LC50_MAGIC;
    m->st.current = 0;
    m->st.transitions = 0;
    m->st.used = 0;
    m->st.history[0] = '\0';
    return m;
}

int lc50_dispatch(struct lc50_machine *m, const struct lc50_event *e)
{
    if (m == NULL || m->magic != LC50_MAGIC || e == NULL ||
        e->kind < EV_SET || e->kind > EV_RESET) {
        return 0;
    }
    {
        handler_fn h = handlers[e->kind];
        if (h == NULL) {
            return 0;
        }
        h(&m->st, e);
    }
    m->st.transitions += 1;
    if (m->st.used + 1u < sizeof(m->st.history)) {
        m->st.history[m->st.used++] = handler_syms[e->kind];
        m->st.history[m->st.used] = '\0';
    }
    return 1;
}

void lc50_destroy(struct lc50_machine *m)
{
    if (m != NULL && m->magic == LC50_MAGIC) {
        m->magic = 0u;
        free(m);
    }
}

int main(void)
{
    struct lc50_machine *m = lc50_create();
    struct lc50_event e;
    int accepted = 0;

    if (m == NULL) {
        printf("lc50 result: FAIL allocation\n");
        return 1;
    }
    e.kind = EV_SET;
    e.payload.value = 50;
    accepted += lc50_dispatch(m, &e);
    e.kind = EV_ADD;
    e.payload.value = 7;
    accepted += lc50_dispatch(m, &e);
    e.kind = EV_RESET;
    accepted += lc50_dispatch(m, &e);
    e.kind = EV_ADD;
    e.payload.value = 42;
    accepted += lc50_dispatch(m, &e);
    e.kind = (enum lc50_event_kind)99; /* invalid event must be rejected */
    accepted += lc50_dispatch(m, &e);

    if (accepted != 4 || m->st.current != 42 || m->st.transitions != 4 ||
        strcmp(m->st.history, "SARA") != 0) {
        printf("lc50 result: FAIL accepted=%d current=%d trans=%d history=%s\n",
               accepted, m->st.current, m->st.transitions, m->st.history);
        lc50_destroy(m);
        return 1;
    }
    printf("lc50 result: OK accepted=%d current=%d trans=%d history=%s\n",
           accepted, m->st.current, m->st.transitions, m->st.history);
    lc50_destroy(m);
    return 0;
}
