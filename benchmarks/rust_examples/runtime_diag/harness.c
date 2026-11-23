/* Deterministic C99 harness: C+ checked runtime-diagnostic / wrapper APIs.
 *
 * Build (from repo root):
 *   /usr/bin/clang -std=c99 -O2 -pedantic \
 *     -Istdlib/safe -Ithird_party/exceptions4c -Ithird_party/safeclib/include \
 *     -Ilibc/install-include \
 *     benchmarks/rust_examples/runtime_diag/harness.c \
 *     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
 *     -lm -pthread -o <out>
 *
 * Output lines are exactly:  <api> <case> <token>
 *
 * Trap/fatal cases are NOT invoked in the main process.  compare.sh runs this
 * binary again with `--trap-on` / `--trap-off` in a fresh subprocess and maps
 * the observed exit code to a token (70 -> TRAP, 0 -> RETURN).  The main
 * process therefore never dies from a forced violation.
 *
 * Every symbol called below was verified as a defined (T) symbol with nm in
 * build/safe/libcplus_safecrt.a and build/libc/libcplus_hardened.a before
 * use (see NOTES.md).  No symbol is called without that evidence.
 */

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "safe_except.h"

/* ------------------------------------------------------------------ ABI --- */
#define CPLUS_SAFE_OK 0
#define CPLUS_SAFE_EINVAL 22

typedef void (*cplus_sc_constraint_handler_t)(const char *, void *, int);

typedef struct cplus_safe_alloc_meta {
  size_t byte_size;
  size_t elem_size;
  size_t elem_count;
  unsigned long long generation;
  int is_live;
} cplus_safe_alloc_meta;

/* runtime / safe runtime (libcplus_safecrt.a) */
extern void cplus_safe_log_set_level(int level);
extern void cplus_runtime_set_violation_trap(int enabled);
extern int cplus_runtime_get_violation_trap(void);
extern void cplus_runtime_violation(const char *api, const char *detail);
extern void cplus_runtime_set_failstop(int enabled);
extern int cplus_runtime_get_failstop(void);
extern int cplus_runtime_push_cleanup(void (*drop)(void *), void *self);
extern void cplus_runtime_pop_cleanup(int execute);
extern int cplus_runtime_cleanup_mark(void);
extern int cplus_runtime_cleanup_depth(void);
extern void cplus_runtime_cleanup_unwind_to(int mark);
extern void cplus_runtime_unwind_cleanups(void);
extern unsigned long cplus_runtime_leaked_cleanups(void);
extern unsigned long cplus_runtime_unwound_cleanups(void);
extern void cplus_runtime_reset_counters(void);
extern int cplus_runtime_guard(int (*body)(void *ctx), void *ctx);
extern void cplus_runtime_set_handler(void (*handler)(const char *message));
extern void cplus_runtime_report(const char *message);

/* safe allocation queries (libcplus_safecrt.a) */
extern void *malloc_s(size_t size);
extern void *calloc_s(size_t count, size_t size);
extern void free_s(void *ptr);
extern int cplus_safe_ptr_is_live(const void *ptr);
extern int cplus_safe_ptr_was_freed(const void *ptr);
extern size_t cplus_safe_ptr_size(const void *ptr);
extern size_t cplus_safe_ptr_elem_size(const void *ptr);
extern size_t cplus_safe_ptr_count(const void *ptr);
extern int cplus_safe_query_length(const void *ptr, size_t elem_size,
                                   size_t *out_count);
extern size_t cplus_safe_remaining_capacity(const void *ptr);
extern int cplus_safe_alloc_get_meta(const void *ptr,
                                     cplus_safe_alloc_meta *out);
extern void cplus_except_init(void);

/* constraint-mode + handler setters (libcplus_safecrt.a) */
extern void cplus_safe_set_constraint_mode(int mode);
extern int cplus_safe_get_constraint_mode(void);
extern cplus_sc_constraint_handler_t
cplus_sc_set_mem_constraint_handler_s(cplus_sc_constraint_handler_t handler);
extern cplus_sc_constraint_handler_t
cplus_sc_set_str_constraint_handler_s(cplus_sc_constraint_handler_t handler);
extern void cplus_sc_ignore_handler_s(const char *msg, void *ptr, int error);
extern void cplus_sc_abort_handler_s(const char *msg, void *ptr, int error);

/* atomic wrappers (libcplus_hardened.a) */
extern void cplus_atomic_init_int(int *obj, int desired);
extern int cplus_atomic_load_int(const int *obj);
extern void cplus_atomic_store_int(int *obj, int desired);
extern int cplus_atomic_exchange_int(int *obj, int desired);
extern int cplus_atomic_fetch_add_int(int *obj, int operand);
extern int cplus_atomic_fetch_sub_int(int *obj, int operand);
extern int cplus_atomic_compare_exchange_strong_int(int *obj, int *expected,
                                                    int desired);

/* ----------------------------------------------------------------- util --- */
static void emit(const char *api, const char *case_name, const char *token) {
  printf("%s %s %s\n", api, case_name, token);
}

static void emit_i(const char *api, const char *case_name, int value) {
  char tok[48];
  snprintf(tok, sizeof tok, "OK:%d", value);
  emit(api, case_name, tok);
}

/* ------------------------------------------------- trap-only subprocess --- */
/* These two functions are reached only via a fresh `--trap-*` subprocess. */
static void subprocess_trap_on(void) {
  cplus_runtime_set_violation_trap(1);
  cplus_runtime_violation("harness", "forced trap-on probe");
  /* Unreachable when the trap works; present so the compiler is happy. */
  emit("runtime_violation_trap", "trap_on", "NO_TRAP");
}

static void subprocess_trap_off(void) {
  cplus_runtime_set_violation_trap(0);
  cplus_runtime_violation("harness", "forced trap-off probe");
  emit("runtime_violation_trap", "trap_off", "RETURN");
}

/* ------------------------------------------------- runtime trap/failstop -- */
static void case_violation_trap_state(void) {
  int before, observed;
  before = cplus_runtime_get_violation_trap();
  emit("runtime_violation_trap", "default", before ? "ON" : "OFF");

  cplus_runtime_set_violation_trap(0);
  observed = cplus_runtime_get_violation_trap();
  emit("runtime_violation_trap", "set_off", observed ? "ON" : "OFF");

  cplus_runtime_set_violation_trap(1);
  observed = cplus_runtime_get_violation_trap();
  emit("runtime_violation_trap", "set_on", observed ? "ON" : "OFF");
  cplus_runtime_set_violation_trap(before ? 1 : 0);
}

static void case_failstop_state(void) {
  int before, observed;
  before = cplus_runtime_get_failstop();
  emit("runtime_failstop", "default", before == 0 ? "OFF" : "ON");

  cplus_runtime_set_failstop(1);
  observed = cplus_runtime_get_failstop();
  emit("runtime_failstop", "set_on", observed ? "ON" : "OFF");

  cplus_runtime_set_failstop(0);
  observed = cplus_runtime_get_failstop();
  emit("runtime_failstop", "set_off", observed ? "ON" : "OFF");
  (void)before;
}

/* --------------------------------------------------------- cleanup stack -- */
static char g_drop_order[16];
static int g_drop_count;

static void drop_record(void *self) {
  char label = (char)(intptr_t)self;
  if (g_drop_count < (int)sizeof(g_drop_order) - 1) {
    g_drop_order[g_drop_count] = label;
    g_drop_order[g_drop_count + 1] = '\0';
  }
  ++g_drop_count;
}

static void reset_drop_log(void) {
  g_drop_order[0] = '\0';
  g_drop_count = 0;
}

static void case_cleanup_stack(void) {
  int mark, depth;
  char tok[64];

  /* Start from an empty, freshly counted stack. */
  cplus_runtime_unwind_cleanups();
  cplus_runtime_reset_counters();
  reset_drop_log();

  (void)cplus_runtime_push_cleanup(drop_record, (void *)(intptr_t)'A');
  (void)cplus_runtime_push_cleanup(drop_record, (void *)(intptr_t)'B');
  (void)cplus_runtime_push_cleanup(drop_record, (void *)(intptr_t)'C');

  mark = cplus_runtime_cleanup_mark();
  depth = cplus_runtime_cleanup_depth();
  snprintf(tok, sizeof tok, "OK:%d/%d", mark, depth);
  emit("runtime_cleanup", "mark_round_trip",
       mark == 3 && depth == 3 ? tok : "BAD_STATE");

  /* NULL drop callback is refused: -1, stack unchanged. */
  depth = cplus_runtime_push_cleanup(NULL, NULL);
  emit("runtime_cleanup", "null_refusal", depth == -1 ? "REFUSED" : "BAD_ACCEPT");

  /* Unwind entries above mark=1 newest-first: 'C' then 'B'. */
  cplus_runtime_cleanup_unwind_to(1);
  emit("runtime_cleanup", "unwind_lifo",
       strcmp(g_drop_order, "CB") == 0 ? "OK:CB" : "BAD_ORDER");

  snprintf(tok, sizeof tok, "OK:%lu",
           cplus_runtime_unwound_cleanups());
  emit("runtime_cleanup", "unwound_counter",
       cplus_runtime_unwound_cleanups() == 2 ? tok : "BAD_COUNT");

  emit_i("runtime_cleanup", "depth_after_unwind",
         cplus_runtime_cleanup_depth());

  /* Drain the single remaining entry so later families see an empty stack. */
  cplus_runtime_unwind_cleanups();
}

/* --------------------------------------------------------------- guards --- */
static int body_ok(void *ctx) {
  (void)ctx;
  return 0;
}
static int body_error(void *ctx) {
  (void)ctx;
  return 1;
}
static int body_violation(void *ctx) {
  int *cleaned = (int *)ctx;
  if (cplus_runtime_push_cleanup(drop_record, (void *)(intptr_t)'X') == 0) {
    /* A cleanup was registered inside the guarded body; the unwind must run
     * it before the guard returns CPLUS_RUNTIME_ERROR. */
    *cleaned = 1;
  }
  cplus_runtime_violation("guard", "forced unwind probe");
  return 0; /* not reached */
}

static void case_guards(void) {
  int rc, cleaned;

  cplus_runtime_set_violation_trap(1);
  cplus_runtime_set_failstop(0);

  rc = cplus_runtime_guard(body_ok, NULL);
  emit("runtime_guard", "body_ok", rc == 0 ? "OK" : "BAD_RC");

  rc = cplus_runtime_guard(body_error, NULL);
  emit("runtime_guard", "body_error", rc == 1 ? "ERROR" : "BAD_RC");

  reset_drop_log();
  cleaned = 0;
  cplus_runtime_reset_counters();
  rc = cplus_runtime_guard(body_violation, &cleaned);
  emit("runtime_guard", "unwind_on_violation",
       rc == 1 && cleaned == 1 && strcmp(g_drop_order, "X") == 0
           ? "ERROR:CLEANED"
           : "BAD_UNWIND");

  rc = cplus_runtime_guard(NULL, NULL);
  emit("runtime_guard", "null_body", rc == 1 ? "ERROR" : "BAD_RC");

  cplus_runtime_unwind_cleanups();
}

/* ---------------------------------------------------------------- report -- */
static int g_handler_calls;
static void counting_handler(const char *message) {
  (void)message;
  ++g_handler_calls;
}

static void case_report(void) {
  g_handler_calls = 0;
  cplus_runtime_set_handler(counting_handler);
  cplus_runtime_report("harness report probe");
  emit_i("runtime_report", "handler_invoked", g_handler_calls);

  g_handler_calls = 0;
  cplus_runtime_report(NULL);
  emit_i("runtime_report", "null_message", g_handler_calls);

  cplus_runtime_set_handler(NULL);
}

/* ------------------------------------------------------ safe ptr queries -- */
static int g_invalid_sentinel;

static void case_ptr_queries(void) {
  void *p = calloc_s(8, 4); /* 8 elements x 4 bytes = 32 bytes */
  size_t n = 0;
  cplus_safe_alloc_meta meta;
  char tok[64];

  if (!p) {
    emit("safe_ptr_query", "valid_live", "ALLOC_FAILED");
    return;
  }

  /* valid live block: size=32, elem_size=4, count=8, query_length OK */
  {
    int live = cplus_safe_ptr_is_live(p);
    size_t sz = cplus_safe_ptr_size(p);
    size_t es = cplus_safe_ptr_elem_size(p);
    size_t cnt = cplus_safe_ptr_count(p);
    int qrc = cplus_safe_query_length(p, 4, &n);
    int mrc = cplus_safe_alloc_get_meta(p, &meta);
    snprintf(tok, sizeof tok, "OK:%lu/%lu/%lu", (unsigned long)sz,
             (unsigned long)es, (unsigned long)cnt);
    emit("safe_ptr_query", "valid_live",
         live && sz == 32 && es == 4 && cnt == 8 && qrc == CPLUS_SAFE_OK &&
                 n == 8 && mrc == CPLUS_SAFE_OK && meta.is_live
             ? tok
             : "BAD_META");
  }

  /* NULL: refused by every query. */
  emit("safe_ptr_query", "null_refusal",
       !cplus_safe_ptr_is_live(NULL) && cplus_safe_ptr_size(NULL) == 0 &&
               cplus_safe_alloc_get_meta(NULL, &meta) == CPLUS_SAFE_EINVAL &&
               cplus_safe_query_length(NULL, 4, &n) == CPLUS_SAFE_EINVAL
           ? "REFUSED"
           : "BAD_ACCEPT");

  /* Address never produced by malloc_s/calloc_s: refused. */
  emit("safe_ptr_query", "invalid_refusal",
       !cplus_safe_ptr_is_live(&g_invalid_sentinel) &&
               cplus_safe_ptr_size(&g_invalid_sentinel) == 0
           ? "REFUSED"
           : "BAD_ACCEPT");

  emit("safe_ptr_query", "remaining_base",
       cplus_safe_remaining_capacity(p) == 32 ? "OK:32" : "BAD_CAP");
  emit("safe_ptr_query", "remaining_interior",
       cplus_safe_remaining_capacity((const char *)p + 8) == 24 ? "OK:24"
                                                                : "BAD_CAP");
  emit("safe_ptr_query", "remaining_end",
       cplus_safe_remaining_capacity((const char *)p + 32) == 0 ? "OK:0"
                                                                : "BAD_CAP");
  emit("safe_ptr_query", "remaining_null",
       cplus_safe_remaining_capacity(NULL) == (size_t)-1 ? "REFUSED"
                                                         : "BAD_ACCEPT");

  free_s(p);
  emit("safe_ptr_query", "after_free",
       !cplus_safe_ptr_is_live(p) && cplus_safe_ptr_was_freed(p) &&
               cplus_safe_ptr_size(p) == 0
           ? "FREED"
           : "BAD_STATE");

  /* Also exercise a raw malloc_s size query for the 1-byte zero-size rule. */
  {
    void *q = malloc_s(0);
    if (q) {
      emit("safe_ptr_query", "zero_size_alloc",
           cplus_safe_ptr_size(q) >= 1 ? "OK" : "BAD_SIZE");
      free_s(q);
    }
  }
}

/* ------------------------------------------------------- constraint mode -- */
static void case_constraint_mode(void) {
  int before = cplus_safe_get_constraint_mode();
  emit("safe_constraint_mode", "default", before == 0 ? "OK:0" : "BAD_STATE");

  cplus_safe_set_constraint_mode(1); /* ABORT */
  emit("safe_constraint_mode", "set_abort",
       cplus_safe_get_constraint_mode() == 1 ? "OK:1" : "BAD_STATE");

  cplus_safe_set_constraint_mode(99); /* invalid -> IGNORE fallback */
  emit("safe_constraint_mode", "invalid_fallback",
       cplus_safe_get_constraint_mode() == 0 ? "OK:0" : "BAD_STATE");
}

/* --------------------------------------------------- handler setters ------ */
static void case_handler_setters(void) {
  cplus_sc_constraint_handler_t old, h1, h2, h3;

  /* mem: ignore -> abort -> ignore, checking each returned previous value. */
  old = cplus_sc_set_mem_constraint_handler_s(cplus_sc_ignore_handler_s);
  h1 = cplus_sc_set_mem_constraint_handler_s(cplus_sc_abort_handler_s);
  h2 = cplus_sc_set_mem_constraint_handler_s(cplus_sc_ignore_handler_s);
  h3 = cplus_sc_set_mem_constraint_handler_s(old);
  emit("sc_handler_setters", "mem_round_trip",
       h1 == cplus_sc_ignore_handler_s && h2 == cplus_sc_abort_handler_s &&
               h3 == cplus_sc_ignore_handler_s
           ? "OK"
           : "BAD_HANDLER");

  /* str: same three-step round trip. */
  old = cplus_sc_set_str_constraint_handler_s(cplus_sc_ignore_handler_s);
  h1 = cplus_sc_set_str_constraint_handler_s(cplus_sc_abort_handler_s);
  h2 = cplus_sc_set_str_constraint_handler_s(cplus_sc_ignore_handler_s);
  h3 = cplus_sc_set_str_constraint_handler_s(old);
  emit("sc_handler_setters", "str_round_trip",
       h1 == cplus_sc_ignore_handler_s && h2 == cplus_sc_abort_handler_s &&
               h3 == cplus_sc_ignore_handler_s
           ? "OK"
           : "BAD_HANDLER");

  /* NULL means "restore the library default": the returned value is the
   * handler we just installed, and the next set returns a non-NULL default. */
  old = cplus_sc_set_mem_constraint_handler_s(cplus_sc_ignore_handler_s);
  h1 = cplus_sc_set_mem_constraint_handler_s(NULL);
  h2 = cplus_sc_set_mem_constraint_handler_s(cplus_sc_ignore_handler_s);
  h3 = cplus_sc_set_mem_constraint_handler_s(old);
  emit("sc_handler_setters", "null_refusal",
       h1 == cplus_sc_ignore_handler_s && h2 != NULL &&
               h3 == cplus_sc_ignore_handler_s
           ? "OK"
           : "BAD_HANDLER");
}

/* ------------------------------------------------------------- exceptions - */
static const struct e4c_exception_type HARNESS_OOPS = {NULL, "HarnessOops"};

static void case_except(void) {
  volatile int caught = 0;

  cplus_except_init();
  cplus_except_init(); /* idempotent */
  emit("except_init", "idempotent", "OK");

  TRY { THROW(HARNESS_OOPS, "%s", "harness probe"); }
  CATCH(HARNESS_OOPS) { caught = 1; }
  emit("except_init", "try_catch", caught == 1 ? "OK" : "BAD_CATCH");
}

/* --------------------------------------------------------------- atomics --- */
static void case_atomic_int_sequence(void) {
  int obj = 0;
  int expected, observed;
  char tok[64];

  cplus_atomic_init_int(&obj, 7);
  emit_i("atomic_int", "init_load", cplus_atomic_load_int(&obj));

  cplus_atomic_store_int(&obj, 9);
  emit_i("atomic_int", "store_load", cplus_atomic_load_int(&obj));

  observed = cplus_atomic_exchange_int(&obj, 3);
  snprintf(tok, sizeof tok, "OK:%d/%d", observed, cplus_atomic_load_int(&obj));
  emit("atomic_int", "exchange", tok);

  observed = cplus_atomic_fetch_add_int(&obj, 5);
  snprintf(tok, sizeof tok, "OK:%d/%d", observed, cplus_atomic_load_int(&obj));
  emit("atomic_int", "fetch_add", tok);

  observed = cplus_atomic_fetch_sub_int(&obj, 3);
  snprintf(tok, sizeof tok, "OK:%d/%d", observed, cplus_atomic_load_int(&obj));
  emit("atomic_int", "fetch_sub", tok);

  expected = 5;
  observed = cplus_atomic_compare_exchange_strong_int(&obj, &expected, 42);
  snprintf(tok, sizeof tok, "OK:%d/%d", observed, cplus_atomic_load_int(&obj));
  emit("atomic_int", "cas_strong_ok", tok);

  expected = 0; /* deliberately wrong */
  observed = cplus_atomic_compare_exchange_strong_int(&obj, &expected, 99);
  snprintf(tok, sizeof tok, "OK:%d/%d", observed, expected);
  emit("atomic_int", "cas_strong_fail", tok);
}

/* ------------------------------------------------------------------- main -- */
int main(int argc, char **argv) {
  cplus_safe_log_set_level(0); /* CPLUS_SAFE_LOG_OFF: keep stdout clean */

  if (argc == 2 && strcmp(argv[1], "--trap-on") == 0) {
    subprocess_trap_on();
    return 0;
  }
  if (argc == 2 && strcmp(argv[1], "--trap-off") == 0) {
    subprocess_trap_off();
    return 0;
  }

  case_violation_trap_state();
  case_failstop_state();
  case_cleanup_stack();
  case_guards();
  case_report();
  case_ptr_queries();
  case_constraint_mode();
  case_handler_setters();
  case_except();
  case_atomic_int_sequence();

  /* Restore globally visible settings before exit. */
  cplus_runtime_set_failstop(0);
  cplus_runtime_set_violation_trap(1);
  cplus_safe_set_constraint_mode(0);
  return 0;
}
