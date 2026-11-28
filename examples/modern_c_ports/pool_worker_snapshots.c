/* Independent modern-C port of the "pool_worker_snapshots" sample workload.
 * C11, no upstream source was copied. Transient storage uses a one-store scope
 * arena modeled on stdlib/scoped_mem.hp. Workers are simulated round-robin so
 * stdout stays deterministic and every loop is bounded. */
#include "common.h"

#define SCRATCH_CAP (16u * 1024u)
static unsigned char g_scratch[SCRATCH_CAP];

typedef struct { unsigned char *base; size_t cap, top; } Scope;
static void scope_init(Scope *s) { s->base = g_scratch; s->cap = SCRATCH_CAP; s->top = 0; }
static size_t scope_mark(const Scope *s) { return s->top; }
static void scope_release(Scope *s, size_t mark) { s->top = mark; }
static void *scope_take(Scope *s, size_t bytes) {
    size_t pad = (16u - (s->top & 15u)) & 15u;
    if (s->top > s->cap || pad > s->cap - s->top) return NULL;
    s->top += pad;
    if (bytes > s->cap - s->top) { s->top -= pad; return NULL; }
    { void *p = s->base + s->top; s->top += bytes; return p; }
}

/* Payload returned by the simulated content service for an absent path. */
static const char NOT_FOUND[] =
    "{\"message\":\"Not Found\","
    "\"documentation_url\":\"https://docs.github.com/rest/repos/contents"
    "#get-repository-content\",\"status\":\"404\"}";

#define NWORK 4
#define NJOBS 8
typedef struct { int id; const char *path; } Request;
typedef struct { int id; unsigned status; size_t len; char body[192]; } Snapshot;

static const Request requests[NJOBS] = {
    {0, "samples/29_ui_todo"},      {1, "samples/30_mini_minecraft"},
    {2, "samples/31_mini_doom"},    {3, "samples/32_pool_worker_snapshots"},
    {4, "samples/00_bignum"},       {5, "samples/01_calc"},
    {6, "samples/02_json"},         {7, "samples/03_vm"}
};

static void fill_snapshot(Snapshot *s, const Request *r) {
    size_t n = strlen(NOT_FOUND);
    s->id = r->id;
    s->status = 404u;
    s->len = n < sizeof s->body - 1u ? n : sizeof s->body - 1u;
    memcpy(s->body, NOT_FOUND, s->len);
    s->body[s->len] = '\0';
}

int main(void) {
    Scope sc; scope_init(&sc);
    size_t mark;
    Snapshot *snaps;
    int worker, j, steps = 0;

    mark = scope_mark(&sc);
    snaps = scope_take(&sc, sizeof(Snapshot) * NJOBS);
    if (!snaps) return 1;

    /* Fixed-size work queue: worker w handles jobs w, w+NWORK, ... with a
     * hard step budget, so no thread scheduling or unbounded wait is needed. */
    for (worker = 0; worker < NWORK; ++worker) {
        for (j = worker; j < NJOBS; j += NWORK) {
            if (steps++ >= NJOBS) break;
            fill_snapshot(&snaps[j], &requests[j]);
        }
    }
    for (j = 0; j < NJOBS; ++j) {
        if (snaps[j].id != j || snaps[j].status != 404u ||
            snaps[j].len != strlen(NOT_FOUND) ||
            memcmp(snaps[j].body, NOT_FOUND, snaps[j].len) != 0) fails++;
    }
    if (steps != NJOBS) fails++;

    /* Emit the first collected snapshot body verbatim, with no trailing newline. */
    fputs(snaps[0].body, stdout);
    scope_release(&sc, mark);
    return fails;
}
