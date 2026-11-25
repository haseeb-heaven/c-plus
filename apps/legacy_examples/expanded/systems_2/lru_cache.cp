/* lru_cache.cp -- deterministic fixed-capacity LRU cache teaching model.
 *
 * NOT a production cache: a bounded, single-threaded model whose entire
 * behaviour is driven by a fixed argv transcript.  No wall clock, no threads,
 * no randomness.  A monotonic operation counter (not time) supplies recency.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/expanded/systems_2/lru_cache.cp --strict \
 *       --sanitize=none -I. -o examples/expanded/systems_2/results/lru_cache
 *
 * Commands (one argv segment each, separated by "--"):
 *   cap N            set capacity (1..LRU_MAX_CAP); 0 or too large is REFUSED
 *   put KEY VALUE    insert or update; evicts the least-recently-used entry
 *   get KEY          read + promote; a missing key is REFUSED
 *   dump             print entries most-recent first + the eviction log
 *   expect KEY VALUE content check (no promote): OK or FAIL + checkfail++
 *
 * Every hostile input is refused through the stdlib error* API with an
 * explicit category and cplus exit code.  This is refusal EVIDENCE for the
 * listed cases, not a formal proof of memory safety or correctness.
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

#define LRU_MAX_CAP   8
#define LRU_KEY_MAX   24
#define LRU_VAL_MAX   32
#define LRU_MAX_EV    32

typedef struct {
    char key[LRU_KEY_MAX];
    char val[LRU_VAL_MAX];
    int  used;
} LruSlot;

static LruSlot lru_slots[LRU_MAX_CAP];
static strmap  lru_index;
static vector  lru_order;      /* slot indices, least-recent first */
static int     lru_capacity;
static int     lru_ops;
static int     lru_refused;
static int     lru_checkfail;
static char    lru_evlog[LRU_MAX_EV][LRU_KEY_MAX];
static int     lru_evcount;

/* argv is an FFI pointer: copy the slot with memcpy so the strict checker
 * can prove the read. */
static char *lru_arg(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

static int lru_parse_int(const char *text, int *out) {
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

/* Refuse a hostile/invalid operation, recording category + exit code. */
static void lru_refuse(error *e, const char *op, const char *reason,
                       int cat, cplus_exit_code code) {
    e->clear();
    e->set_category(cat, code, reason);
    lru_refused++;
    print("LRU REFUSE", op, "cat=", e->category(), "code=", e->code(),
          "msg=", e->what());
}

/* Validate and copy an untrusted token into bounded storage via string.
 * Returns 0 on success, -1 on NULL/empty/oversize with an error set. */
static int lru_copy_token(const char *src, char *dst, size_t cap,
                          error *e, const char *op, const char *what) {
    string tmp;
    size_t n;
    if (src == NULL) {
        lru_refuse(e, op, what, STDLIB_CATEGORY_INVALID,
                   CPLUS_EXIT_UNSUPPORTED);
        return -1;
    }
    tmp = string(src);
    if (!tmp.valid()) {
        lru_refuse(e, op, what, STDLIB_CATEGORY_MEMORY, CPLUS_EXIT_RESOURCE);
        return -1;
    }
    n = tmp.size();
    if (n == 0 || n + 1 > cap) {
        tmp.drop();
        lru_refuse(e, op, what, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS);
        return -1;
    }
    if (tmp.substr(0, n, dst, cap) != 0) {
        tmp.drop();
        lru_refuse(e, op, what, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS);
        return -1;
    }
    tmp.drop();
    return 0;
}

/* Move `slot` to the most-recent end of the LRU order vector. */
static void lru_promote(int slot) {
    size_t n = lru_order.size();
    size_t i;
    for (i = 0; i < n; i++) {
        int cur = -1;
        if (lru_order.at_checked(i, &cur) != 0)
            return;
        if (cur == slot) {
            lru_order.erase(i, (int *)0);
            lru_order.push_back(slot);
            return;
        }
    }
}

static LruSlot *lru_slot_at(int slot) {
    if (slot < 0 || slot >= lru_capacity)
        return (LruSlot *)0;
    return &lru_slots[slot];
}

static int lru_find_slot(const char *key) {
    int slot = -1;
    if (lru_index.get(key, &slot) != 0)
        return -1;
    if (slot < 0 || slot >= lru_capacity)
        return -1;
    if (!lru_slots[slot].used || strcmp(lru_slots[slot].key, key) != 0)
        return -1;
    return slot;
}

static int lru_free_slot(void) {
    int i;
    for (i = 0; i < lru_capacity; ++i)
        if (!lru_slots[i].used)
            return i;
    return -1;
}

static void lru_record_eviction(const char *key) {
    if (lru_evcount < LRU_MAX_EV) {
        snprintf(lru_evlog[lru_evcount], LRU_KEY_MAX, "%s", key);
        lru_evcount++;
    }
}

static void lru_cmd_cap(const char *a1, error *e) {
    int n = 0;
    if (!lru_parse_int(a1, &n) || n <= 0 || n > LRU_MAX_CAP) {
        lru_refuse(e, "cap", "capacity out of range", STDLIB_CATEGORY_RANGE,
                   CPLUS_EXIT_BOUNDS);
        return;
    }
    if ((size_t)n < lru_order.size()) {
        lru_refuse(e, "cap", "capacity below live entries",
                   STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS);
        return;
    }
    lru_capacity = n;
    print("LRU cap", n);
}

static void lru_cmd_put(const char *key, const char *val, error *e) {
    char k[LRU_KEY_MAX];
    char v[LRU_VAL_MAX];
    int slot;
    if (lru_copy_token(key, k, sizeof(k), e, "put", "key") != 0)
        return;
    if (lru_copy_token(val, v, sizeof(v), e, "put", "value") != 0)
        return;
    if (lru_capacity <= 0) {
        lru_refuse(e, "put", "zero capacity", STDLIB_CATEGORY_RANGE,
                   CPLUS_EXIT_BOUNDS);
        return;
    }
    slot = lru_find_slot(k);
    if (slot >= 0) {
        snprintf(lru_slots[slot].val, LRU_VAL_MAX, "%s", v);
        lru_promote(slot);
        print("LRU PUT", k, v, "update=1");
        return;
    }
    if ((int)lru_order.size() >= lru_capacity) {
        int old = -1;
        LruSlot *oldp;
        if (lru_order.erase(0, &old) != 0) {
            lru_refuse(e, "put", "eviction pop failed",
                       STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS);
            return;
        }
        oldp = lru_slot_at(old);
        if (oldp == (LruSlot *)0) {
            lru_refuse(e, "put", "eviction index invalid",
                       STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS);
            return;
        }
        lru_record_eviction(oldp->key);
        print("LRU EVICT", oldp->key);
        lru_index.remove(oldp->key);
        oldp->used = 0;
        oldp->key[0] = '\0';
        oldp->val[0] = '\0';
        slot = old;
    } else {
        slot = lru_free_slot();
    }
    if (slot < 0) {
        lru_refuse(e, "put", "no free slot", STDLIB_CATEGORY_MEMORY,
                   CPLUS_EXIT_RESOURCE);
        return;
    }
    snprintf(lru_slots[slot].key, LRU_KEY_MAX, "%s", k);
    snprintf(lru_slots[slot].val, LRU_VAL_MAX, "%s", v);
    lru_slots[slot].used = 1;
    if (lru_index.put(k, slot) != 0) {
        lru_slots[slot].used = 0;
        lru_refuse(e, "put", "index insert failed", STDLIB_CATEGORY_MEMORY,
                   CPLUS_EXIT_RESOURCE);
        return;
    }
    lru_order.push_back(slot);
    print("LRU PUT", k, v, "update=0");
}

static void lru_cmd_get(const char *key, error *e) {
    char k[LRU_KEY_MAX];
    int slot;
    if (lru_copy_token(key, k, sizeof(k), e, "get", "key") != 0)
        return;
    slot = lru_find_slot(k);
    if (slot < 0) {
        lru_refuse(e, "get", "missing key", STDLIB_CATEGORY_INVALID,
                   CPLUS_EXIT_OWNERSHIP);
        return;
    }
    lru_promote(slot);
    print("LRU GET", k, lru_slots[slot].val);
}

static void lru_cmd_expect(const char *key, const char *val, error *e) {
    char k[LRU_KEY_MAX];
    char v[LRU_VAL_MAX];
    int slot;
    if (lru_copy_token(key, k, sizeof(k), e, "expect", "key") != 0)
        return;
    if (lru_copy_token(val, v, sizeof(v), e, "expect", "value") != 0)
        return;
    slot = lru_find_slot(k);
    if (slot < 0 || strcmp(lru_slots[slot].val, v) != 0) {
        lru_checkfail++;
        print("LRU expect", k, "FAIL got=", slot < 0 ? "<missing>" : lru_slots[slot].val,
              "want=", v);
        return;
    }
    print("LRU expect", k, "OK", v);
}

static void lru_cmd_dump(void) {
    size_t n = lru_order.size();
    int i;
    print("LRU dump n=", (int)n);
    for (i = (int)n - 1; i >= 0; --i) {
        int slot = -1;
        if (lru_order.at_checked((size_t)i, &slot) == 0 && slot >= 0 &&
            slot < lru_capacity && lru_slots[slot].used)
            print("LRU entry", lru_slots[slot].key, lru_slots[slot].val);
    }
    print("LRU evictions", lru_evcount);
    for (i = 0; i < lru_evcount; ++i)
        print("LRU evicted", lru_evlog[i]);
}

int main(int argc, char **argv) {
    error err = error();
    int i = 1;

    lru_index = strmap();
    lru_order = vector();
    lru_capacity = 0;
    lru_ops = 0;
    lru_refused = 0;
    lru_checkfail = 0;
    lru_evcount = 0;
    {
        int s;
        for (s = 0; s < LRU_MAX_CAP; ++s) {
            lru_slots[s].used = 0;
            lru_slots[s].key[0] = '\0';
            lru_slots[s].val[0] = '\0';
        }
    }

    while (i < argc) {
        char *cmd = lru_arg(argv, (size_t)i);
        char *a1 = (char *)0;
        char *a2 = (char *)0;
        i++;
        if (i < argc && strcmp(lru_arg(argv, (size_t)i), "--") != 0)
            a1 = lru_arg(argv, (size_t)i++);
        if (i < argc && strcmp(lru_arg(argv, (size_t)i), "--") != 0)
            a2 = lru_arg(argv, (size_t)i++);
        while (i < argc && strcmp(lru_arg(argv, (size_t)i), "--") != 0) {
            lru_refuse(&err, cmd, "bad args", STDLIB_CATEGORY_INVALID,
                       CPLUS_EXIT_UNSUPPORTED);
            i++;
        }
        if (i < argc)
            i++; /* consume "--" */

        err.clear(); /* each command starts with fresh error state */
        lru_ops++;
        if (strcmp(cmd, "cap") == 0) {
            lru_cmd_cap(a1, &err);
        } else if (strcmp(cmd, "put") == 0) {
            lru_cmd_put(a1, a2, &err);
        } else if (strcmp(cmd, "get") == 0) {
            lru_cmd_get(a1, &err);
        } else if (strcmp(cmd, "expect") == 0) {
            lru_cmd_expect(a1, a2, &err);
        } else if (strcmp(cmd, "dump") == 0) {
            lru_cmd_dump();
        } else {
            lru_refuse(&err, cmd, "unknown command", STDLIB_CATEGORY_INVALID,
                       CPLUS_EXIT_UNSUPPORTED);
        }
    }

    if (argc < 2) {
        lru_refuse(&err, "usage", "no commands", STDLIB_CATEGORY_INVALID,
                   CPLUS_EXIT_UNSUPPORTED);
    }

    print("LRU_SUMMARY", lru_ops, lru_ops - lru_refused, lru_refused,
          lru_checkfail);
    lru_order.drop();
    lru_index.drop();
    return lru_checkfail != 0 ? 1 : 0;
}
