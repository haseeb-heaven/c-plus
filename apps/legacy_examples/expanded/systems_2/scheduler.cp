/* scheduler.cp -- deterministic round-robin + priority task scheduler model.
 *
 * NOT an OS scheduler: a bounded, single-threaded model driven entirely by a
 * fixed argv transcript.  Two FIFO deques (high priority, normal priority)
 * hold integer task ids; each tick serves high first, then normal tasks in
 * round-robin order, re-enqueuing a task that still has cycles left.
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/legacy_examples/expanded/systems_2/scheduler.cp --strict \
 *       --sanitize=none -I. -o apps/legacy_examples/expanded/systems_2/results/scheduler
 *
 * Commands (one argv segment each, separated by "--"):
 *   capacity N            bound the total waiting queue (1..SCH_MAX_Q)
 *   arrive NAME PRIO BURST  enqueue a task; PRIO 0=normal 1=high
 *   tick                   run one decision (empty queue is REFUSED)
 *   run N                  run N bounded decisions
 *   dump                   print both queues + completion log
 *   expect NAME REMAIN     content check (peek) of a waiting task
 *
 * Hostile inputs (zero/oversized capacity, queue overflow, oversized name,
 * invalid priority/burst, duplicate name, empty-queue tick, bad run count)
 * are REFUSED through the stdlib error* API with explicit category and code.
 * This is refusal EVIDENCE for listed cases, not a formal correctness proof.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"
#include "stdlib/collections.hp"

#define SCH_MAX_Q      8
#define SCH_NAME_MAX   20
#define SCH_MAX_TASKS  48
#define SCH_MAX_BURST  16
#define SCH_MAX_RUN    64

typedef struct {
    char name[SCH_NAME_MAX];
    int  prio;      /* 0 normal, 1 high */
    int  remain;    /* cycles left */
    int  live;      /* not yet completed */
} Task;

static Task   sch_tasks[SCH_MAX_TASKS];
static int    sch_task_count;
static deque  sch_high;
static deque  sch_norm;
static vector sch_done;        /* completed task ids, in completion order */
static int    sch_capacity;
static int    sch_ops;
static int    sch_refused;
static int    sch_checkfail;

static char *sch_arg(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

static int sch_parse_int(const char *text, int *out) {
    char *end;
    long v;
    if (text == NULL)
        return 0;
    end = (char *)text;
    errno = 0;
    v = strtol(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE ||
        v < INT_MIN || v > INT_MAX)
        return 0;
    *out = (int)v;
    return 1;
}

static void sch_refuse(error *e, const char *op, const char *reason,
                       int cat, cplus_exit_code code) {
    e->clear();
    e->set_category(cat, code, reason);
    sch_refused++;
    print("SCH REFUSE", op, "cat=", e->category(), "code=", e->code(),
          "msg=", e->what());
}

static int sch_copy_token(const char *src, char *dst, size_t cap,
                          error *e, const char *op, const char *what) {
    string tmp;
    size_t n;
    if (src == NULL) {
        sch_refuse(e, op, what, STDLIB_CATEGORY_INVALID,
                   CPLUS_EXIT_UNSUPPORTED);
        return -1;
    }
    tmp = string(src);
    if (!tmp.valid()) {
        sch_refuse(e, op, what, STDLIB_CATEGORY_MEMORY, CPLUS_EXIT_RESOURCE);
        return -1;
    }
    n = tmp.size();
    if (n == 0 || n + 1 > cap) {
        tmp.drop();
        sch_refuse(e, op, what, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS);
        return -1;
    }
    if (tmp.substr(0, n, dst, cap) != 0) {
        tmp.drop();
        sch_refuse(e, op, what, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS);
        return -1;
    }
    tmp.drop();
    return 0;
}

static Task *sch_task_at(int idx) {
    if (idx < 0 || idx >= sch_task_count)
        return (Task *)0;
    return &sch_tasks[idx];
}

static int sch_find_name(const char *name) {
    int i;
    for (i = 0; i < sch_task_count; ++i)
        if (sch_tasks[i].live && strcmp(sch_tasks[i].name, name) == 0)
            return i;
    return -1;
}

static deque *sch_queue_for(int prio) {
    return prio == 1 ? &sch_high : &sch_norm;
}

static void sch_cmd_capacity(const char *a1, error *e) {
    int n = 0;
    size_t waiting = sch_high.size() + sch_norm.size();
    if (!sch_parse_int(a1, &n) || n <= 0 || n > SCH_MAX_Q) {
        sch_refuse(e, "capacity", "capacity out of range",
                   STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS);
        return;
    }
    if ((size_t)n < waiting) {
        sch_refuse(e, "capacity", "capacity below waiting tasks",
                   STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS);
        return;
    }
    sch_capacity = n;
    print("SCH capacity", n);
}

static void sch_cmd_arrive(const char *name, const char *prio_s,
                           const char *burst_s, error *e) {
    char nm[SCH_NAME_MAX];
    int prio = -1;
    int burst = -1;
    int id;
    if (sch_copy_token(name, nm, sizeof(nm), e, "arrive", "name") != 0)
        return;
    if (!sch_parse_int(prio_s, &prio) || (prio != 0 && prio != 1)) {
        sch_refuse(e, "arrive", "invalid priority", STDLIB_CATEGORY_INVALID,
                   CPLUS_EXIT_UNSUPPORTED);
        return;
    }
    if (!sch_parse_int(burst_s, &burst) || burst <= 0 || burst > SCH_MAX_BURST) {
        sch_refuse(e, "arrive", "burst out of range", STDLIB_CATEGORY_RANGE,
                   CPLUS_EXIT_BOUNDS);
        return;
    }
    if (sch_capacity <= 0) {
        sch_refuse(e, "arrive", "zero capacity", STDLIB_CATEGORY_RANGE,
                   CPLUS_EXIT_BOUNDS);
        return;
    }
    if (sch_high.size() + sch_norm.size() >= (size_t)sch_capacity) {
        sch_refuse(e, "arrive", "queue overflow", STDLIB_CATEGORY_RANGE,
                   CPLUS_EXIT_RESOURCE);
        return;
    }
    if (sch_find_name(nm) >= 0) {
        sch_refuse(e, "arrive", "duplicate name", STDLIB_CATEGORY_INVALID,
                   CPLUS_EXIT_OWNERSHIP);
        return;
    }
    if (sch_task_count >= SCH_MAX_TASKS) {
        sch_refuse(e, "arrive", "task table full", STDLIB_CATEGORY_MEMORY,
                   CPLUS_EXIT_RESOURCE);
        return;
    }
    id = sch_task_count++;
    snprintf(sch_tasks[id].name, SCH_NAME_MAX, "%s", nm);
    sch_tasks[id].prio = prio;
    sch_tasks[id].remain = burst;
    sch_tasks[id].live = 1;
    if (deque_push_back(sch_queue_for(prio), id) != 0) {
        sch_task_count--;
        sch_refuse(e, "arrive", "enqueue failed", STDLIB_CATEGORY_MEMORY,
                   CPLUS_EXIT_RESOURCE);
        return;
    }
    print("SCH arrive", nm, "prio=", prio, "burst=", burst);
}

/* One scheduling decision.  Distinct return codes keep "empty queue" (a
 * normal refusal for tick/run) separate from an internal error that already
 * set `e`, so stale error state can never suppress a refusal. */
#define SCH_TICK_OK    0
#define SCH_TICK_EMPTY 1
#define SCH_TICK_ERROR 2

static int sch_tick(error *e) {
    deque *q;
    int id = -1;
    Task *t;
    if (sch_high.size() > 0)
        q = &sch_high;
    else if (sch_norm.size() > 0)
        q = &sch_norm;
    else
        return SCH_TICK_EMPTY;
    if (deque_pop_front(q, &id) != 0) {
        sch_refuse(e, "tick", "dequeue failed", STDLIB_CATEGORY_BOUNDS,
                   CPLUS_EXIT_BOUNDS);
        return SCH_TICK_ERROR;
    }
    t = sch_task_at(id);
    if (t == (Task *)0 || !t->live) {
        sch_refuse(e, "tick", "stale task id", STDLIB_CATEGORY_LIFECYCLE,
                   CPLUS_EXIT_USE_AFTER_FREE);
        return SCH_TICK_ERROR;
    }
    t->remain--;
    print("SCH run", t->name, "prio=", t->prio, "remain=", t->remain);
    if (t->remain > 0) {
        if (deque_push_back(q, id) != 0) {
            sch_refuse(e, "tick", "re-enqueue failed", STDLIB_CATEGORY_MEMORY,
                       CPLUS_EXIT_RESOURCE);
            return SCH_TICK_ERROR;
        }
    } else {
        t->live = 0;
        sch_done.push_back(id);
        print("SCH done", t->name);
    }
    return SCH_TICK_OK;
}

static void sch_cmd_tick(error *e) {
    int rc = sch_tick(e);
    if (rc == SCH_TICK_EMPTY) {
        sch_refuse(e, "tick", "empty queue", STDLIB_CATEGORY_RANGE,
                   CPLUS_EXIT_BOUNDS);
    }
}

static void sch_cmd_run(const char *a1, error *e) {
    int n = 0;
    int k;
    if (!sch_parse_int(a1, &n) || n <= 0 || n > SCH_MAX_RUN) {
        sch_refuse(e, "run", "run count out of range", STDLIB_CATEGORY_RANGE,
                   CPLUS_EXIT_BOUNDS);
        return;
    }
    for (k = 0; k < n; ++k) {
        int rc = sch_tick(e);
        if (rc == SCH_TICK_EMPTY) {
            sch_refuse(e, "run", "empty queue", STDLIB_CATEGORY_RANGE,
                       CPLUS_EXIT_BOUNDS);
            return;
        }
        if (rc == SCH_TICK_ERROR)
            return;
    }
}

static void sch_dump_queue(const char *label, deque *q) {
    size_t n = deque_size(q);
    size_t i;
    print("SCH queue", label, "n=", (int)n);
    for (i = 0; i < n; ++i) {
        int id = -1;
        if (deque_at_checked(q, i, &id) == 0) {
            Task *t = sch_task_at(id);
            if (t != (Task *)0 && t->live)
                print("SCH waiting", label, t->name, "remain=", t->remain);
        }
    }
}

static void sch_cmd_dump(void) {
    size_t nd = sch_done.size();
    size_t i;
    sch_dump_queue("high", &sch_high);
    sch_dump_queue("norm", &sch_norm);
    print("SCH completed", (int)nd);
    for (i = 0; i < nd; ++i) {
        int id = -1;
        if (sch_done.at_checked(i, &id) == 0) {
            Task *t = sch_task_at(id);
            if (t != (Task *)0)
                print("SCH did", t->name);
        }
    }
}

static void sch_cmd_expect(const char *name, const char *remain_s, error *e) {
    char nm[SCH_NAME_MAX];
    int want = -1;
    int id;
    if (sch_copy_token(name, nm, sizeof(nm), e, "expect", "name") != 0)
        return;
    if (!sch_parse_int(remain_s, &want)) {
        sch_refuse(e, "expect", "invalid remain", STDLIB_CATEGORY_INVALID,
                   CPLUS_EXIT_UNSUPPORTED);
        return;
    }
    id = sch_find_name(nm);
    if (id < 0) {
        sch_checkfail++;
        print("SCH expect", nm, "FAIL missing");
        return;
    }
    if (sch_tasks[id].remain != want) {
        sch_checkfail++;
        print("SCH expect", nm, "FAIL got=", sch_tasks[id].remain,
              "want=", want);
        return;
    }
    print("SCH expect", nm, "OK", want);
}

int main(int argc, char **argv) {
    error err = error();
    int i = 1;

    sch_high = deque();
    sch_norm = deque();
    sch_done = vector();
    sch_capacity = 0;
    sch_task_count = 0;
    sch_ops = 0;
    sch_refused = 0;
    sch_checkfail = 0;

    while (i < argc) {
        char *cmd = sch_arg(argv, (size_t)i);
        char *a1 = (char *)0;
        char *a2 = (char *)0;
        char *a3 = (char *)0;
        i++;
        if (i < argc && strcmp(sch_arg(argv, (size_t)i), "--") != 0)
            a1 = sch_arg(argv, (size_t)i++);
        if (i < argc && strcmp(sch_arg(argv, (size_t)i), "--") != 0)
            a2 = sch_arg(argv, (size_t)i++);
        if (i < argc && strcmp(sch_arg(argv, (size_t)i), "--") != 0)
            a3 = sch_arg(argv, (size_t)i++);
        while (i < argc && strcmp(sch_arg(argv, (size_t)i), "--") != 0) {
            sch_refuse(&err, cmd, "bad args", STDLIB_CATEGORY_INVALID,
                       CPLUS_EXIT_UNSUPPORTED);
            i++;
        }
        if (i < argc)
            i++;

        err.clear(); /* each command starts with fresh error state */
        sch_ops++;
        if (strcmp(cmd, "capacity") == 0) {
            sch_cmd_capacity(a1, &err);
        } else if (strcmp(cmd, "arrive") == 0) {
            sch_cmd_arrive(a1, a2, a3, &err);
        } else if (strcmp(cmd, "tick") == 0) {
            sch_cmd_tick(&err);
        } else if (strcmp(cmd, "run") == 0) {
            sch_cmd_run(a1, &err);
        } else if (strcmp(cmd, "expect") == 0) {
            sch_cmd_expect(a1, a2, &err);
        } else if (strcmp(cmd, "dump") == 0) {
            sch_cmd_dump();
        } else {
            sch_refuse(&err, cmd, "unknown command", STDLIB_CATEGORY_INVALID,
                       CPLUS_EXIT_UNSUPPORTED);
        }
    }

    if (argc < 2)
        sch_refuse(&err, "usage", "no commands", STDLIB_CATEGORY_INVALID,
                   CPLUS_EXIT_UNSUPPORTED);

    print("SCH_SUMMARY", sch_ops, sch_ops - sch_refused, sch_refused,
          sch_checkfail);
    sch_done.drop();
    sch_norm.drop();
    sch_high.drop();
    return sch_checkfail != 0 ? 1 : 0;
}
