/* event_bus.cp -- deterministic in-memory publish/subscribe teaching model.
 *
 * NOT a messaging system: a bounded, single-threaded event bus driven by a
 * fixed argv transcript.  Topics are indexed by a stdlib strmap and each
 * topic keeps an ordered, bounded subscriber list.  Publishing is a pure
 * in-memory fan-out in subscription order; no callbacks, threads or I/O.
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/legacy_examples/expanded/systems_2/event_bus.cp --strict \
 *       --sanitize=none -I. -o apps/legacy_examples/expanded/systems_2/results/event_bus
 *
 * Commands (one argv segment each, separated by "--"):
 *   topic NAME          register a topic (duplicate / oversize is REFUSED)
 *   sub TOPIC WHO       subscribe (duplicate / unknown / full is REFUSED)
 *   unsub TOPIC WHO     unsubscribe (unknown topic / subscriber REFUSED)
 *   pub TOPIC PAYLOAD   fan out to every subscriber in subscription order
 *   dump                print topics, subscribers and the delivery log
 *   expect TOPIC WHO    content check: is WHO subscribed to TOPIC?
 *
 * All hostile inputs are refused through the stdlib error* API with an
 * explicit category and cplus exit code.  This is refusal EVIDENCE, not a
 * formal proof of correctness or memory safety.
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

#define EB_MAX_TOPICS  8
#define EB_TOPIC_MAX   20
#define EB_MAX_SUBS    6
#define EB_SUB_MAX     20
#define EB_PAYLOAD_MAX 40

typedef struct {
    char name[EB_TOPIC_MAX];
    char subs[EB_MAX_SUBS][EB_SUB_MAX];
    int  sub_count;
    int  used;
} Topic;

static Topic  eb_topics[EB_MAX_TOPICS];
static strmap eb_index;
static vector eb_deliveries;   /* one int per pub: number of subscribers */
static int    eb_ops;
static int    eb_refused;
static int    eb_checkfail;

static char *eb_arg(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

static void eb_refuse(error *e, const char *op, const char *reason,
                      int cat, cplus_exit_code code) {
    e->clear();
    e->set_category(cat, code, reason);
    eb_refused++;
    print("EB REFUSE", op, "cat=", e->category(), "code=", e->code(),
          "msg=", e->what());
}

static int eb_copy_token(const char *src, char *dst, size_t cap,
                         error *e, const char *op, const char *what) {
    string tmp;
    size_t n;
    if (src == NULL) {
        eb_refuse(e, op, what, STDLIB_CATEGORY_INVALID,
                  CPLUS_EXIT_UNSUPPORTED);
        return -1;
    }
    tmp = string(src);
    if (!tmp.valid()) {
        eb_refuse(e, op, what, STDLIB_CATEGORY_MEMORY, CPLUS_EXIT_RESOURCE);
        return -1;
    }
    n = tmp.size();
    if (n == 0 || n + 1 > cap) {
        tmp.drop();
        eb_refuse(e, op, what, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS);
        return -1;
    }
    if (tmp.substr(0, n, dst, cap) != 0) {
        tmp.drop();
        eb_refuse(e, op, what, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS);
        return -1;
    }
    tmp.drop();
    return 0;
}

static Topic *eb_topic_at(int idx) {
    if (idx < 0 || idx >= EB_MAX_TOPICS)
        return (Topic *)0;
    return &eb_topics[idx];
}

static int eb_find_topic(const char *name) {
    int slot = -1;
    if (eb_index.get(name, &slot) != 0)
        return -1;
    if (slot < 0 || slot >= EB_MAX_TOPICS)
        return -1;
    if (!eb_topics[slot].used || strcmp(eb_topics[slot].name, name) != 0)
        return -1;
    return slot;
}

static int eb_find_sub(Topic *t, const char *who) {
    int i;
    for (i = 0; i < t->sub_count; ++i)
        if (strcmp(t->subs[i], who) == 0)
            return i;
    return -1;
}

static void eb_cmd_topic(const char *name, error *e) {
    char nm[EB_TOPIC_MAX];
    int slot;
    if (eb_copy_token(name, nm, sizeof(nm), e, "topic", "name") != 0)
        return;
    if (eb_find_topic(nm) >= 0) {
        eb_refuse(e, "topic", "duplicate topic", STDLIB_CATEGORY_INVALID,
                  CPLUS_EXIT_OWNERSHIP);
        return;
    }
    for (slot = 0; slot < EB_MAX_TOPICS; ++slot)
        if (!eb_topics[slot].used)
            break;
    if (slot >= EB_MAX_TOPICS) {
        eb_refuse(e, "topic", "topic table full", STDLIB_CATEGORY_MEMORY,
                  CPLUS_EXIT_RESOURCE);
        return;
    }
    snprintf(eb_topics[slot].name, EB_TOPIC_MAX, "%s", nm);
    eb_topics[slot].used = 1;
    eb_topics[slot].sub_count = 0;
    if (eb_index.put(nm, slot) != 0) {
        eb_topics[slot].used = 0;
        eb_refuse(e, "topic", "index insert failed", STDLIB_CATEGORY_MEMORY,
                  CPLUS_EXIT_RESOURCE);
        return;
    }
    print("EB topic", nm);
}

static Topic *eb_resolve(const char *name, error *e, const char *op) {
    char nm[EB_TOPIC_MAX];
    int slot;
    if (eb_copy_token(name, nm, sizeof(nm), e, op, "topic") != 0)
        return (Topic *)0;
    slot = eb_find_topic(nm);
    if (slot < 0) {
        eb_refuse(e, op, "unknown topic", STDLIB_CATEGORY_INVALID,
                  CPLUS_EXIT_OWNERSHIP);
        return (Topic *)0;
    }
    return eb_topic_at(slot);
}

static void eb_cmd_sub(const char *topic, const char *who, error *e) {
    char w[EB_SUB_MAX];
    Topic *t = eb_resolve(topic, e, "sub");
    if (t == (Topic *)0)
        return;
    if (eb_copy_token(who, w, sizeof(w), e, "sub", "subscriber") != 0)
        return;
    if (eb_find_sub(t, w) >= 0) {
        eb_refuse(e, "sub", "duplicate subscription", STDLIB_CATEGORY_INVALID,
                  CPLUS_EXIT_OWNERSHIP);
        return;
    }
    if (t->sub_count >= EB_MAX_SUBS) {
        eb_refuse(e, "sub", "subscriber list full", STDLIB_CATEGORY_RANGE,
                  CPLUS_EXIT_RESOURCE);
        return;
    }
    snprintf(t->subs[t->sub_count], EB_SUB_MAX, "%s", w);
    t->sub_count++;
    print("EB sub", t->name, w);
}

static void eb_cmd_unsub(const char *topic, const char *who, error *e) {
    char w[EB_SUB_MAX];
    Topic *t = eb_resolve(topic, e, "unsub");
    int at;
    int i;
    if (t == (Topic *)0)
        return;
    if (eb_copy_token(who, w, sizeof(w), e, "unsub", "subscriber") != 0)
        return;
    at = eb_find_sub(t, w);
    if (at < 0) {
        eb_refuse(e, "unsub", "unknown subscriber", STDLIB_CATEGORY_INVALID,
                  CPLUS_EXIT_OWNERSHIP);
        return;
    }
    for (i = at; i + 1 < t->sub_count; ++i)
        snprintf(t->subs[i], EB_SUB_MAX, "%s", t->subs[i + 1]);
    t->subs[t->sub_count - 1][0] = '\0';
    t->sub_count--;
    print("EB unsub", t->name, w);
}

static void eb_cmd_pub(const char *topic, const char *payload, error *e) {
    char p[EB_PAYLOAD_MAX];
    Topic *t = eb_resolve(topic, e, "pub");
    int i;
    if (t == (Topic *)0)
        return;
    if (eb_copy_token(payload, p, sizeof(p), e, "pub", "payload") != 0)
        return;
    print("EB pub", t->name, "deliver=", t->sub_count, "payload=", p);
    for (i = 0; i < t->sub_count; ++i)
        print("EB deliver", t->name, "->", t->subs[i], p);
    eb_deliveries.push_back(t->sub_count);
}

static void eb_cmd_expect(const char *topic, const char *who, error *e) {
    char w[EB_SUB_MAX];
    Topic *t = eb_resolve(topic, e, "expect");
    if (t == (Topic *)0)
        return;
    if (eb_copy_token(who, w, sizeof(w), e, "expect", "subscriber") != 0)
        return;
    if (eb_find_sub(t, w) < 0) {
        eb_checkfail++;
        print("EB expect", t->name, w, "FAIL not-subscribed");
        return;
    }
    print("EB expect", t->name, w, "OK subscribed");
}

static void eb_cmd_dump(void) {
    int i;
    size_t n = eb_deliveries.size();
    size_t k;
    int topics = 0;
    for (i = 0; i < EB_MAX_TOPICS; ++i)
        if (eb_topics[i].used)
            topics++;
    print("EB dump topics=", topics);
    for (i = 0; i < EB_MAX_TOPICS; ++i) {
        int j;
        if (!eb_topics[i].used)
            continue;
        print("EB topic", eb_topics[i].name, "subs=", eb_topics[i].sub_count);
        for (j = 0; j < eb_topics[i].sub_count; ++j)
            print("EB has", eb_topics[i].name, eb_topics[i].subs[j]);
    }
    print("EB deliveries n=", (int)n);
    for (k = 0; k < n; ++k) {
        int d = -1;
        if (eb_deliveries.at_checked(k, &d) == 0)
            print("EB delivery", (int)k, d);
    }
}

int main(int argc, char **argv) {
    error err = error();
    int i = 1;

    eb_index = strmap();
    eb_deliveries = vector();
    eb_ops = 0;
    eb_refused = 0;
    eb_checkfail = 0;
    {
        int s;
        for (s = 0; s < EB_MAX_TOPICS; ++s) {
            eb_topics[s].used = 0;
            eb_topics[s].sub_count = 0;
            eb_topics[s].name[0] = '\0';
        }
    }

    while (i < argc) {
        char *cmd = eb_arg(argv, (size_t)i);
        char *a1 = (char *)0;
        char *a2 = (char *)0;
        i++;
        if (i < argc && strcmp(eb_arg(argv, (size_t)i), "--") != 0)
            a1 = eb_arg(argv, (size_t)i++);
        if (i < argc && strcmp(eb_arg(argv, (size_t)i), "--") != 0)
            a2 = eb_arg(argv, (size_t)i++);
        while (i < argc && strcmp(eb_arg(argv, (size_t)i), "--") != 0) {
            eb_refuse(&err, cmd, "bad args", STDLIB_CATEGORY_INVALID,
                      CPLUS_EXIT_UNSUPPORTED);
            i++;
        }
        if (i < argc)
            i++;

        err.clear(); /* each command starts with fresh error state */
        eb_ops++;
        if (strcmp(cmd, "topic") == 0) {
            eb_cmd_topic(a1, &err);
        } else if (strcmp(cmd, "sub") == 0) {
            eb_cmd_sub(a1, a2, &err);
        } else if (strcmp(cmd, "unsub") == 0) {
            eb_cmd_unsub(a1, a2, &err);
        } else if (strcmp(cmd, "pub") == 0) {
            eb_cmd_pub(a1, a2, &err);
        } else if (strcmp(cmd, "expect") == 0) {
            eb_cmd_expect(a1, a2, &err);
        } else if (strcmp(cmd, "dump") == 0) {
            eb_cmd_dump();
        } else {
            eb_refuse(&err, cmd, "unknown command", STDLIB_CATEGORY_INVALID,
                      CPLUS_EXIT_UNSUPPORTED);
        }
    }

    if (argc < 2)
        eb_refuse(&err, "usage", "no commands", STDLIB_CATEGORY_INVALID,
                  CPLUS_EXIT_UNSUPPORTED);

    print("EB_SUMMARY", eb_ops, eb_ops - eb_refused, eb_refused, eb_checkfail);
    eb_deliveries.drop();
    eb_index.drop();
    return eb_checkfail != 0 ? 1 : 0;
}
