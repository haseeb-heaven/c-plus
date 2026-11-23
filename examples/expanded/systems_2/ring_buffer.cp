/* ring_buffer.cp -- deterministic fixed-capacity byte ring buffer model.
 *
 * NOT a production queue: a bounded, single-threaded byte ring whose entire
 * behaviour is driven by a fixed argv transcript.  A monotonic head index and
 * a used count give explicit wraparound; every read/write is bounds-checked
 * before it touches the backing array.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/expanded/systems_2/ring_buffer.cp --strict \
 *       --sanitize=none -I. -o examples/expanded/systems_2/results/ring_buffer
 *
 * Commands (one argv segment each, separated by "--"):
 *   cap N              set capacity (1..RB_MAX_CAP); 0 / too big REFUSED
 *   push B0 B1 ...     append bytes; overflow / bad byte REFUSED
 *   pop N              remove N bytes; underflow REFUSED
 *   peek N             read N bytes without removing; underflow REFUSED
 *   expect N B0 B1 ... content check (bounded peek) -> OK or FAIL
 *   dump               print used/cap and the linearised contents
 *
 * Hostile inputs (zero/oversized capacity, overflow, underflow, out-of-range
 * byte, oversized batch) are refused through the stdlib error* API with an
 * explicit category and cplus exit code.  This is refusal EVIDENCE for the
 * listed cases, not a formal proof of memory safety.
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

#define RB_MAX_CAP   32
#define RB_MAX_ARGS  40
#define RB_MAX_BATCH 32

static unsigned char rb_buf[RB_MAX_CAP];
static int  rb_head;
static int  rb_used;
static int  rb_capacity;
static vector rb_log;          /* signed op codes: +n push, -n pop, 0 peek */
static int  rb_ops;
static int  rb_refused;
static int  rb_checkfail;

static char *rb_arg(char **p, size_t idx) {
    char *value;
    __builtin_memcpy(&value, p + idx, sizeof(value));
    return value;
}

static int rb_parse_int(const char *text, int *out) {
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

static void rb_refuse(error *e, const char *op, const char *reason,
                      int cat, cplus_exit_code code) {
    e->clear();
    e->set_category(cat, code, reason);
    rb_refused++;
    print("RB REFUSE", op, "cat=", e->category(), "code=", e->code(),
          "msg=", e->what());
}

/* Validate a numeric token with string, then parse a bounded byte 0..255. */
static int rb_parse_byte(const char *token, int *out, error *e,
                         const char *op) {
    string tmp;
    size_t n;
    int v = -1;
    if (token == NULL) {
        rb_refuse(e, op, "missing byte", STDLIB_CATEGORY_INVALID,
                  CPLUS_EXIT_UNSUPPORTED);
        return -1;
    }
    tmp = string(token);
    if (!tmp.valid()) {
        rb_refuse(e, op, "byte token alloc failed", STDLIB_CATEGORY_MEMORY,
                  CPLUS_EXIT_RESOURCE);
        return -1;
    }
    n = tmp.size();
    if (n == 0 || n > 3) {
        tmp.drop();
        rb_refuse(e, op, "byte token length", STDLIB_CATEGORY_RANGE,
                  CPLUS_EXIT_BOUNDS);
        return -1;
    }
    tmp.drop();
    if (!rb_parse_int(token, &v) || v < 0 || v > 255) {
        rb_refuse(e, op, "byte out of range", STDLIB_CATEGORY_RANGE,
                  CPLUS_EXIT_BOUNDS);
        return -1;
    }
    *out = v;
    return 0;
}

static void rb_cmd_cap(const char *a1, error *e) {
    int n = 0;
    if (!rb_parse_int(a1, &n) || n <= 0 || n > RB_MAX_CAP) {
        rb_refuse(e, "cap", "capacity out of range", STDLIB_CATEGORY_RANGE,
                  CPLUS_EXIT_BOUNDS);
        return;
    }
    if (n < rb_used) {
        rb_refuse(e, "cap", "capacity below used bytes", STDLIB_CATEGORY_RANGE,
                  CPLUS_EXIT_BOUNDS);
        return;
    }
    rb_capacity = n;
    print("RB cap", n);
}

static void rb_cmd_push(char **tok, int n, error *e) {
    int bytes[RB_MAX_BATCH];
    int k;
    if (rb_capacity <= 0) {
        rb_refuse(e, "push", "zero capacity", STDLIB_CATEGORY_RANGE,
                  CPLUS_EXIT_BOUNDS);
        return;
    }
    if (n - 1 <= 0) {
        rb_refuse(e, "push", "empty batch", STDLIB_CATEGORY_INVALID,
                  CPLUS_EXIT_UNSUPPORTED);
        return;
    }
    if (n - 1 > RB_MAX_BATCH) {
        rb_refuse(e, "push", "batch too large", STDLIB_CATEGORY_RANGE,
                  CPLUS_EXIT_BOUNDS);
        return;
    }
    /* Validate every byte BEFORE mutating the ring (failure-atomic). */
    for (k = 1; k < n; ++k)
        if (rb_parse_byte(tok[k], &bytes[k - 1], e, "push") != 0)
            return;
    if (n - 1 > rb_capacity - rb_used) {
        rb_refuse(e, "push", "overflow", STDLIB_CATEGORY_RANGE,
                  CPLUS_EXIT_RESOURCE);
        return;
    }
    print("RB PUSH n=", n - 1, "used=", rb_used);
    for (k = 0; k < n - 1; ++k) {
        int pos = rb_head + rb_used;
        if (pos >= rb_capacity)
            pos -= rb_capacity;
        rb_buf[pos] = (unsigned char)bytes[k];
        rb_used++;
    }
    print("RB push_bytes used=", rb_used, "head=", rb_head);
    rb_log.push_back(n - 1);
}

static int rb_read_n(const char *a1, int *n, const char *op, error *e) {
    if (!rb_parse_int(a1, n) || *n <= 0) {
        rb_refuse(e, op, "count out of range", STDLIB_CATEGORY_RANGE,
                  CPLUS_EXIT_BOUNDS);
        return -1;
    }
    if (*n > rb_used) {
        rb_refuse(e, op, "underflow", STDLIB_CATEGORY_RANGE,
                  CPLUS_EXIT_BOUNDS);
        return -1;
    }
    if (*n > RB_MAX_CAP) {
        rb_refuse(e, op, "read too large", STDLIB_CATEGORY_RANGE,
                  CPLUS_EXIT_BOUNDS);
        return -1;
    }
    return 0;
}

static void rb_cmd_pop(const char *a1, error *e) {
    int n = 0;
    int k;
    if (rb_read_n(a1, &n, "pop", e) != 0)
        return;
    print("RB POP n=", n, "used=", rb_used);
    for (k = 0; k < n; ++k) {
        print("RB popped", k, (int)rb_buf[rb_head]);
        rb_head++;
        if (rb_head >= rb_capacity)
            rb_head = 0;   /* wraparound */
        rb_used--;
    }
    print("RB pop_bytes used=", rb_used, "head=", rb_head);
    rb_log.push_back(-n);
}

static void rb_cmd_peek(const char *a1, error *e) {
    int n = 0;
    int k;
    if (rb_read_n(a1, &n, "peek", e) != 0)
        return;
    print("RB PEEK n=", n, "used=", rb_used);
    for (k = 0; k < n; ++k) {
        int pos = rb_head + k;
        if (pos >= rb_capacity)
            pos -= rb_capacity;
        print("RB peeked", k, (int)rb_buf[pos]);
    }
    rb_log.push_back(0);
}

static void rb_cmd_expect(char **tok, int n, error *e) {
    int count = 0;
    int k;
    if (n < 2 || rb_read_n(tok[1], &count, "expect", e) != 0)
        return;
    if (n - 2 != count) {
        rb_refuse(e, "expect", "arity mismatch", STDLIB_CATEGORY_INVALID,
                  CPLUS_EXIT_UNSUPPORTED);
        return;
    }
    for (k = 0; k < count; ++k) {
        int want = -1;
        int pos;
        if (rb_parse_byte(tok[2 + k], &want, e, "expect") != 0)
            return;
        pos = rb_head + k;
        if (pos >= rb_capacity)
            pos -= rb_capacity;
        if ((int)rb_buf[pos] != want) {
            rb_checkfail++;
            print("RB expect", k, "FAIL got=", (int)rb_buf[pos], "want=", want);
            return;
        }
    }
    print("RB expect OK n=", count);
}

static void rb_cmd_dump(void) {
    int i;
    size_t n = rb_log.size();
    size_t k;
    print("RB dump used=", rb_used, "cap=", rb_capacity, "head=", rb_head);
    for (i = 0; i < rb_used; ++i) {
        int pos = rb_head + i;
        if (pos >= rb_capacity)
            pos -= rb_capacity;
        print("RB byte", i, (int)rb_buf[pos]);
    }
    print("RB oplog n=", (int)n);
    for (k = 0; k < n; ++k) {
        int v = 0;
        if (rb_log.at_checked(k, &v) == 0)
            print("RB op", (int)k, v);
    }
}

int main(int argc, char **argv) {
    error err = error();
    int i = 1;

    rb_log = vector();
    rb_head = 0;
    rb_used = 0;
    rb_capacity = 0;
    rb_ops = 0;
    rb_refused = 0;
    rb_checkfail = 0;
    {
        int b;
        for (b = 0; b < RB_MAX_CAP; ++b)
            rb_buf[b] = 0;
    }

    while (i < argc) {
        char *tok[RB_MAX_ARGS];
        int n = 0;
        char *cmd;
        while (i < argc && n < RB_MAX_ARGS &&
               strcmp(rb_arg(argv, (size_t)i), "--") != 0)
            tok[n++] = rb_arg(argv, (size_t)i++);
        while (i < argc && strcmp(rb_arg(argv, (size_t)i), "--") != 0)
            i++; /* drop excess tokens past the fixed argument window */
        if (i < argc)
            i++; /* consume "--" */
        if (n == 0)
            continue;
        cmd = tok[0];
        err.clear(); /* each command starts with fresh error state */
        rb_ops++;
        if (strcmp(cmd, "cap") == 0) {
            rb_cmd_cap(n >= 2 ? tok[1] : (char *)0, &err);
        } else if (strcmp(cmd, "push") == 0) {
            rb_cmd_push(tok, n, &err);
        } else if (strcmp(cmd, "pop") == 0) {
            rb_cmd_pop(n >= 2 ? tok[1] : (char *)0, &err);
        } else if (strcmp(cmd, "peek") == 0) {
            rb_cmd_peek(n >= 2 ? tok[1] : (char *)0, &err);
        } else if (strcmp(cmd, "expect") == 0) {
            rb_cmd_expect(tok, n, &err);
        } else if (strcmp(cmd, "dump") == 0) {
            rb_cmd_dump();
        } else {
            rb_refuse(&err, cmd, "unknown command", STDLIB_CATEGORY_INVALID,
                      CPLUS_EXIT_UNSUPPORTED);
        }
    }

    if (argc < 2)
        rb_refuse(&err, "usage", "no commands", STDLIB_CATEGORY_INVALID,
                  CPLUS_EXIT_UNSUPPORTED);

    print("RB_SUMMARY", rb_ops, rb_ops - rb_refused, rb_refused, rb_checkfail);
    rb_log.drop();
    return rb_checkfail != 0 ? 1 : 0;
}
