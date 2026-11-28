/*===-- harness.cp - EDGE100 runtime edge-case harness -------------*- C -*-===
 *
 * Edge-case tests for the runtime-facing interfaces declared by this repo's
 * runtime/include/cplus/cplus.h and implemented in runtime/cplus_runtime.c.
 *
 * Model: fork-per-case.  The parent performs no runtime calls at all (so the
 * runtime's global allocator bookkeeping is never inherited in a locked or
 * dirty state).  For every case the parent forks a child that redirects its
 * stdout/stderr to per-case files, runs exactly one case, and _exit()s with a
 * verdict.  Fatal runtime paths call _Exit(code) inside the child, so the
 * parent observes the deterministic exit code without dying itself.
 *
 * A per-child alarm(2) bounds hangs (exit 124 == watchdog).  run.sh bounds the
 * whole process with `timeout 60`.
 *
 * Verdict classification:
 *   normal case : child returns 0 on success, nonzero on failure; expect exit 0
 *   fatal case  : child calls a fatal API; if it returns, child returns 250;
 *                 parent expects the exact deterministic exit code (101..109)
 *   expect_diag : stderr must contain "C+ runtime error: " (fatal cases)
 *   expect_out  : exact stdout bytes (print cases), otherwise not checked
 *===----------------------------------------------------------------------===*/

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#include "cplus/cplus.h"

#define FAIL_SENTINEL 250
#define WATCHDOG_EXIT 124

/* Output directory supplied by run.sh via EDGE100_OUT. */
static const char *g_out = ".";

/* -------------------------------------------------------------------------
 * Small file helpers (parent side only)
 * ------------------------------------------------------------------------- */
static void path_for(char *buf, size_t cap, const char *id, const char *ext) {
  snprintf(buf, cap, "%s/case_%s.%s", g_out, id, ext);
}

static long read_file(const char *path, char *buf, long cap) {
  FILE *f = fopen(path, "rb");
  if (!f) return -1;
  long n = (long)fread(buf, 1, (size_t)(cap - 1), f);
  buf[n < 0 ? 0 : n] = '\0';
  fclose(f);
  return n;
}

static int file_contains(const char *path, const char *needle) {
  char buf[8192];
  long n = read_file(path, buf, (long)sizeof(buf));
  if (n < 0) return 0;
  return strstr(buf, needle) != NULL;
}

static int file_nonempty(const char *path) {
  char buf[64];
  long n = read_file(path, buf, (long)sizeof(buf));
  return n > 0;
}

/* -------------------------------------------------------------------------
 * Case functions.  Each returns 0 on success for normal cases; fatal cases
 * invoke a runtime safety failure and return FAIL_SENTINEL only if the runtime
 * (incorrectly) returns.
 * ------------------------------------------------------------------------- */

/* ---- cplus_fatal: every exit-code domain ------------------------------- */
static int fatal_101(void) { cplus_fatal(CPLUS_EXIT_BOUNDS, "edge", __FILE__, __LINE__, "d101"); return FAIL_SENTINEL; }
static int fatal_102(void) { cplus_fatal(CPLUS_EXIT_NULL_POINTER, "edge", __FILE__, __LINE__, "d102"); return FAIL_SENTINEL; }
static int fatal_103(void) { cplus_fatal(CPLUS_EXIT_USE_AFTER_FREE, "edge", __FILE__, __LINE__, "d103"); return FAIL_SENTINEL; }
static int fatal_104(void) { cplus_fatal(CPLUS_EXIT_DOUBLE_FREE, "edge", __FILE__, __LINE__, "d104"); return FAIL_SENTINEL; }
static int fatal_105(void) { cplus_fatal(CPLUS_EXIT_TYPE_CONFUSION, "edge", __FILE__, __LINE__, "d105"); return FAIL_SENTINEL; }
static int fatal_106(void) { cplus_fatal(CPLUS_EXIT_OWNERSHIP, "edge", __FILE__, __LINE__, "d106"); return FAIL_SENTINEL; }
static int fatal_107(void) { cplus_fatal(CPLUS_EXIT_RESOURCE, "edge", __FILE__, __LINE__, "d107"); return FAIL_SENTINEL; }
static int fatal_108(void) { cplus_fatal(CPLUS_EXIT_UNSUPPORTED, "edge", __FILE__, __LINE__, "d108"); return FAIL_SENTINEL; }
static int fatal_109(void) { cplus_fatal(CPLUS_EXIT_IO, "edge", __FILE__, __LINE__, "d109"); return FAIL_SENTINEL; }
static int fatal_nullargs(void) { cplus_fatal(CPLUS_EXIT_RESOURCE, NULL, NULL, 0, NULL); return FAIL_SENTINEL; }

/* ---- cplus_check_bounds ------------------------------------------------- */
static int bounds_valid(void) {
  int a[4] = {0,0,0,0};
  cplus_check_bounds(a, 4, 3, "f", 1, "a[i]");
  return 0;
}
static int bounds_null_base_valid(void) {
  cplus_check_bounds(NULL, 4, 1, "f", 2, "p[i]");
  return 0;
}
static int bounds_fatal_eq(void) {
  int a[4] = {0};
  cplus_check_bounds(a, 4, 4, "f", 3, "a[i]");
  return FAIL_SENTINEL;
}
static int bounds_fatal_zero(void) {
  cplus_check_bounds(NULL, 0, 0, "f", 4, "z[0]");
  return FAIL_SENTINEL;
}
static int bounds_fatal_huge(void) {
  cplus_check_bounds(NULL, 4, SIZE_MAX, "f", 5, "a[i]");
  return FAIL_SENTINEL;
}

/* ---- cplus_check_nonnull ------------------------------------------------ */
static int nonnull_valid(void) {
  int x = 0;
  cplus_check_nonnull(&x, "f", 6, "p");
  return 0;
}
static int nonnull_fatal_null(void) {
  cplus_check_nonnull(NULL, "f", 7, "p");
  return FAIL_SENTINEL;
}
static int nonnull_fatal_null_expr(void) {
  cplus_check_nonnull(NULL, NULL, 0, NULL);
  return FAIL_SENTINEL;
}

/* ---- cplus_alloc -------------------------------------------------------- */
static int alloc_valid(void) {
  void *p = cplus_alloc(16);
  if (!p) return 1;
  if (cplus_alloc_bytes(p) != 16) return 2;
  cplus_release(p);
  return 0;
}
static int alloc_zero(void) {
  void *p = cplus_alloc(0);
  if (!p) return 1;
  if (cplus_alloc_bytes(p) != 1) return 2;
  cplus_release(p);
  return 0;
}
static int alloc_large(void) {
  void *p = cplus_alloc(1u << 16);
  if (!p) return 1;
  if (cplus_alloc_bytes(p) != (1u << 16)) return 2;
  cplus_release(p);
  return 0;
}
static int alloc_many(void) {
  void *v[100];
  for (int i = 0; i < 100; ++i) {
    v[i] = cplus_alloc((size_t)i + 1);
    if (!v[i]) return 1;
    if (cplus_alloc_bytes(v[i]) != (size_t)i + 1) return 2;
    for (int j = 0; j < i; ++j) if (v[i] == v[j]) return 3;
  }
  for (int i = 0; i < 100; ++i) cplus_release(v[i]);
  return 0;
}

/* ---- cplus_zalloc ------------------------------------------------------- */
static int zalloc_zeroed(void) {
  unsigned char *p = (unsigned char *)cplus_zalloc(64);
  if (!p) return 1;
  for (int i = 0; i < 64; ++i) if (p[i] != 0) return 2;
  cplus_release(p);
  return 0;
}
static int zalloc_zero_size(void) {
  void *p = cplus_zalloc(0);
  if (!p) return 1;
  cplus_release(p);
  return 0;
}

/* ---- cplus_alloc_array -------------------------------------------------- */
static int array_valid(void) {
  int *a = (int *)cplus_alloc_array(10, sizeof(int));
  if (!a) return 1;
  if (cplus_alloc_bytes(a) != 40) return 2;
  if (cplus_alloc_count(a, sizeof(int)) != 10) return 3;
  for (int i = 0; i < 10; ++i) if (a[i] != 0) return 4;
  cplus_release(a);
  return 0;
}
static int array_zero_count(void) {
  void *p = cplus_alloc_array(0, 8);
  if (!p) return 1;
  cplus_release(p);
  return 0;
}
static int array_zero_elem(void) {
  void *p = cplus_alloc_array(10, 0);
  if (!p) return 1;
  if (cplus_alloc_bytes(p) != 10) return 2;
  cplus_release(p);
  return 0;
}
static int array_fatal_overflow(void) {
  cplus_alloc_array(SIZE_MAX, 2);
  return FAIL_SENTINEL;
}

/* ---- cplus_realloc ------------------------------------------------------ */
static int realloc_valid(void) {
  int *p = (int *)cplus_alloc(8);
  if (!p) return 1;
  *p = 42;
  int *q = (int *)cplus_realloc(p, 32);
  if (!q) return 2;
  if (q == p) return 3;
  if (*q != 42) return 4;
  if (cplus_alloc_bytes(q) != 32) return 5;
  cplus_check_live(p); /* this runtime keeps the old block live */
  cplus_release(p);
  cplus_release(q);
  return 0;
}
static int realloc_null(void) {
  void *p = cplus_realloc(NULL, 16);
  if (!p) return 1;
  if (cplus_alloc_bytes(p) != 16) return 2;
  cplus_release(p);
  return 0;
}
static int realloc_zero(void) {
  void *p = cplus_alloc(8);
  void *q = cplus_realloc(p, 0);
  if (q != NULL) return 1;
  return 0;
}
static int realloc_zero_null(void) {
  void *p = cplus_realloc(NULL, 0);
  if (!p) return 1;
  cplus_release(p);
  return 0;
}
static int realloc_shrink(void) {
  unsigned char *p = (unsigned char *)cplus_alloc(64);
  if (!p) return 1;
  for (int i = 0; i < 64; ++i) p[i] = (unsigned char)i;
  unsigned char *q = (unsigned char *)cplus_realloc(p, 8);
  if (!q) return 2;
  for (int i = 0; i < 8; ++i) if (q[i] != (unsigned char)i) return 3;
  cplus_release(p);
  cplus_release(q);
  return 0;
}
static int realloc_fatal_uaf(void) {
  void *p = cplus_alloc(8);
  cplus_realloc(p, 0); /* releases p */
  cplus_check_live(p);
  return FAIL_SENTINEL;
}

/* ---- cplus_zero --------------------------------------------------------- */
static int zero_valid(void) {
  unsigned char b[32];
  memset(b, 0xAA, sizeof(b));
  cplus_zero(b, sizeof(b));
  for (int i = 0; i < 32; ++i) if (b[i] != 0) return 1;
  return 0;
}
static int zero_null(void) {
  cplus_zero(NULL, 8);
  return 0;
}
static int zero_len0(void) {
  unsigned char b[4] = {1,2,3,4};
  cplus_zero(b, 0);
  if (b[0] != 1 || b[3] != 4) return 1;
  return 0;
}

/* ---- cplus_release ------------------------------------------------------ */
static int release_valid(void) {
  void *p = cplus_alloc(16);
  cplus_release(p);
  return 0;
}
static int release_null(void) {
  cplus_release(NULL);
  return 0;
}
static int release_fatal_double(void) {
  void *p = cplus_alloc(16);
  cplus_release(p);
  cplus_release(p);
  return FAIL_SENTINEL;
}
static int release_fatal_stack(void) {
  int stackvar = 0;
  cplus_release(&stackvar);
  return FAIL_SENTINEL;
}
static int release_fatal_static(void) {
  static int globalvar = 0;
  cplus_release(&globalvar);
  return FAIL_SENTINEL;
}

/* ---- cplus_alloc_bytes -------------------------------------------------- */
static int bytes_valid(void) {
  void *p = cplus_alloc(24);
  if (cplus_alloc_bytes(p) != 24) { cplus_release(p); return 1; }
  cplus_release(p);
  return 0;
}
static int bytes_fatal_null(void) {
  cplus_alloc_bytes(NULL);
  return FAIL_SENTINEL;
}
static int bytes_fatal_freed(void) {
  void *p = cplus_alloc(24);
  cplus_release(p);
  cplus_alloc_bytes(p);
  return FAIL_SENTINEL;
}
static int bytes_fatal_stack(void) {
  int stackvar = 0;
  cplus_alloc_bytes(&stackvar);
  return FAIL_SENTINEL;
}

/* ---- cplus_check_live --------------------------------------------------- */
static int live_valid(void) {
  void *p = cplus_alloc(16);
  cplus_check_live(p);
  cplus_release(p);
  return 0;
}
static int live_fatal_null(void) {
  cplus_check_live(NULL);
  return FAIL_SENTINEL;
}
static int live_fatal_freed(void) {
  void *p = cplus_alloc(16);
  cplus_release(p);
  cplus_check_live(p);
  return FAIL_SENTINEL;
}
static int live_fatal_stack(void) {
  int stackvar = 0;
  cplus_check_live(&stackvar);
  return FAIL_SENTINEL;
}

/* ---- cplus_alloc_count -------------------------------------------------- */
static int count_valid(void) {
  int *a = (int *)cplus_alloc_array(10, sizeof(int));
  if (cplus_alloc_count(a, sizeof(int)) != 10) { cplus_release(a); return 1; }
  cplus_release(a);
  return 0;
}
static int count_elem1(void) {
  void *p = cplus_alloc(7);
  if (cplus_alloc_count(p, 1) != 7) { cplus_release(p); return 1; }
  cplus_release(p);
  return 0;
}
static int count_fatal_zero_elem(void) {
  void *p = cplus_alloc(7);
  cplus_alloc_count(p, 0);
  return FAIL_SENTINEL;
}
static int count_fatal_null(void) {
  cplus_alloc_count(NULL, 4);
  return FAIL_SENTINEL;
}
static int count_fatal_freed(void) {
  void *p = cplus_alloc(7);
  cplus_release(p);
  cplus_alloc_count(p, 1);
  return FAIL_SENTINEL;
}

/* ---- cplus_check_index -------------------------------------------------- */
static int index_valid(void) {
  if (cplus_check_index(3, 4, "f", 8, "i") != 3) return 1;
  if (cplus_check_index(0, 1, "f", 9, "i") != 0) return 2;
  return 0;
}
static int index_fatal_eq(void) {
  cplus_check_index(4, 4, "f", 10, "i");
  return FAIL_SENTINEL;
}
static int index_fatal_zero(void) {
  cplus_check_index(0, 0, "f", 11, "i");
  return FAIL_SENTINEL;
}
static int index_fatal_huge(void) {
  cplus_check_index(SIZE_MAX, 4, "f", 12, "i");
  return FAIL_SENTINEL;
}

/* ---- cplus_scope_push / cplus_scope_cleanup ----------------------------- */
static int g_drop_order[16];
static int g_drop_n;
static void rec_drop(void *self) {
  g_drop_order[g_drop_n++] = (int)(intptr_t)self;
}
static void plain_drop(void *self) { (void)self; g_drop_n++; }

static int push_valid(void) {
  cplus_scope s = {0};
  void *p = cplus_alloc(8);
  if (cplus_scope_push(&s, p, NULL, CPLUS_CLEANUP_MANAGED) != 0) return 1;
  if (!s.head) return 2;
  cplus_scope_cleanup(&s);
  return s.head == NULL ? 0 : 3;
}
static int push_null_scope(void) {
  void *p = cplus_alloc(8);
  int rc = cplus_scope_push(NULL, p, NULL, CPLUS_CLEANUP_MANAGED);
  cplus_release(p);
  return rc == -1 ? 0 : 1;
}
static int push_null_drop(void) {
  cplus_scope s = {0};
  void *p = cplus_alloc(8);
  if (cplus_scope_push(&s, p, NULL, CPLUS_CLEANUP_RESOURCE) != 0) return 1;
  cplus_scope_cleanup(&s); /* drop is NULL -> skipped by runtime */
  cplus_release(p);
  return 0;
}
static int push_null_ptr(void) {
  cplus_scope s = {0};
  if (cplus_scope_push(&s, NULL, plain_drop, CPLUS_CLEANUP_RESOURCE) != 0) return 1;
  g_drop_n = 0;
  cplus_scope_cleanup(&s);
  return g_drop_n == 0 ? 0 : 1;
}
static int cleanup_valid(void) {
  cplus_scope s = {0};
  g_drop_n = 0;
  cplus_scope_push(&s, (void *)(intptr_t)1, rec_drop, CPLUS_CLEANUP_RESOURCE);
  cplus_scope_push(&s, (void *)(intptr_t)2, rec_drop, CPLUS_CLEANUP_RESOURCE);
  cplus_scope_cleanup(&s);
  return (g_drop_n == 2 && s.head == NULL) ? 0 : 1;
}
static int cleanup_reverse(void) {
  cplus_scope s = {0};
  g_drop_n = 0;
  cplus_scope_push(&s, (void *)(intptr_t)1, rec_drop, CPLUS_CLEANUP_RESOURCE);
  cplus_scope_push(&s, (void *)(intptr_t)2, rec_drop, CPLUS_CLEANUP_RESOURCE);
  cplus_scope_push(&s, (void *)(intptr_t)3, rec_drop, CPLUS_CLEANUP_RESOURCE);
  cplus_scope_cleanup(&s);
  return (g_drop_n == 3 && g_drop_order[0] == 3 && g_drop_order[1] == 2 &&
          g_drop_order[2] == 1) ? 0 : 1;
}
static int cleanup_double(void) {
  cplus_scope s = {0};
  g_drop_n = 0;
  cplus_scope_push(&s, (void *)(intptr_t)1, rec_drop, CPLUS_CLEANUP_RESOURCE);
  cplus_scope_cleanup(&s);
  cplus_scope_cleanup(&s); /* safe: list already empty */
  return g_drop_n == 1 ? 0 : 1;
}
static int cleanup_null_scope(void) {
  cplus_scope_cleanup(NULL);
  return 0;
}
static int cleanup_nested(void) {
  cplus_scope outer = {0}, inner = {0};
  g_drop_n = 0;
  cplus_scope_push(&outer, (void *)(intptr_t)1, rec_drop, CPLUS_CLEANUP_RESOURCE);
  cplus_scope_push(&outer, (void *)(intptr_t)2, rec_drop, CPLUS_CLEANUP_RESOURCE);
  cplus_scope_push(&inner, (void *)(intptr_t)3, rec_drop, CPLUS_CLEANUP_RESOURCE);
  cplus_scope_push(&inner, (void *)(intptr_t)4, rec_drop, CPLUS_CLEANUP_RESOURCE);
  cplus_scope_cleanup(&inner); /* inner first: 4 then 3 */
  cplus_scope_cleanup(&outer); /* outer next: 2 then 1 */
  return (g_drop_n == 4 && g_drop_order[0] == 4 && g_drop_order[1] == 3 &&
          g_drop_order[2] == 2 && g_drop_order[3] == 1) ? 0 : 1;
}
static int cleanup_managed(void) {
  cplus_scope s = {0};
  void *p = cplus_alloc(8);
  cplus_scope_push(&s, p, NULL, CPLUS_CLEANUP_MANAGED);
  cplus_scope_cleanup(&s); /* runtime calls cplus_release(p) */
  return 0;
}
static int cleanup_managed_drop_uaf(void) {
  cplus_scope s = {0};
  void *p = cplus_alloc(8);
  /* Supply a non-NULL drop so the runtime's `c->ptr && c->drop` guard passes;
   * for MANAGED kind the runtime ignores drop and calls cplus_release(p). */
  cplus_scope_push(&s, p, plain_drop, CPLUS_CLEANUP_MANAGED);
  cplus_scope_cleanup(&s);
  cplus_check_live(p); /* must be dangling now */
  return FAIL_SENTINEL;
}
static int cleanup_fatal_double_free(void) {
  cplus_scope s = {0};
  void *p = cplus_alloc(8);
  cplus_scope_push(&s, p, NULL, CPLUS_CLEANUP_MANAGED);
  cplus_scope_push(&s, p, NULL, CPLUS_CLEANUP_MANAGED); /* same ptr twice */
  cplus_scope_cleanup(&s); /* two releases -> double free */
  return FAIL_SENTINEL;
}
static int cleanup_mixed(void) {
  cplus_scope s = {0};
  g_drop_n = 0;
  void *p = cplus_alloc(8);
  cplus_scope_push(&s, p, NULL, CPLUS_CLEANUP_MANAGED);
  cplus_scope_push(&s, (void *)(intptr_t)9, rec_drop, CPLUS_CLEANUP_RESOURCE);
  cplus_scope_cleanup(&s);
  return g_drop_n == 1 ? 0 : 1;
}

/* ---- cplus_resource_check_borrow --------------------------------------- */
static int borrow_valid(void) {
  int x = 0;
  cplus_resource_check_borrow(&x, "f", 13, "b");
  return 0;
}
static int borrow_fatal_null(void) {
  cplus_resource_check_borrow(NULL, "f", 14, "b");
  return FAIL_SENTINEL;
}
static int borrow_fatal_null_expr(void) {
  cplus_resource_check_borrow(NULL, NULL, 0, NULL);
  return FAIL_SENTINEL;
}

/* ---- mutex primitives --------------------------------------------------- */
static int mutex_valid(void) {
  void *m = cplus_mutex_new();
  if (!m) return 1;
  cplus_mutex_lock(m);
  cplus_mutex_unlock(m);
  cplus_mutex_free(m);
  return 0;
}
static int mutex_null_ops(void) {
  cplus_mutex_lock(NULL);
  cplus_mutex_unlock(NULL);
  cplus_mutex_free(NULL);
  return 0;
}
static int mutex_twice(void) {
  void *a = cplus_mutex_new();
  void *b = cplus_mutex_new();
  if (!a || !b) return 1;
  cplus_mutex_lock(a); cplus_mutex_unlock(a);
  cplus_mutex_lock(b); cplus_mutex_unlock(b);
  cplus_mutex_free(a); cplus_mutex_free(b);
  return 0;
}

/* ---- TLS primitives ----------------------------------------------------- */
static int tls_valid(void) {
  void *s = cplus_tls_new();
  if (!s) return 1;
  if (cplus_tls_get(s) != NULL) return 2;
  cplus_tls_set(s, (void *)(intptr_t)0x1234);
  if (cplus_tls_get(s) != (void *)(intptr_t)0x1234) return 3;
  cplus_tls_free(s);
  return 0;
}
static int tls_null_ops(void) {
  if (cplus_tls_get(NULL) != NULL) return 1;
  cplus_tls_set(NULL, (void *)(intptr_t)1);
  cplus_tls_free(NULL);
  return 0;
}
static int tls_overwrite(void) {
  void *s = cplus_tls_new();
  if (!s) return 1;
  cplus_tls_set(s, (void *)(intptr_t)1);
  cplus_tls_set(s, (void *)(intptr_t)2);
  if (cplus_tls_get(s) != (void *)(intptr_t)2) { cplus_tls_free(s); return 2; }
  cplus_tls_free(s);
  return 0;
}
static int tls_two_slots(void) {
  void *a = cplus_tls_new();
  void *b = cplus_tls_new();
  if (!a || !b) return 1;
  cplus_tls_set(a, (void *)(intptr_t)11);
  cplus_tls_set(b, (void *)(intptr_t)22);
  int ok = cplus_tls_get(a) == (void *)(intptr_t)11 &&
           cplus_tls_get(b) == (void *)(intptr_t)22;
  cplus_tls_free(a); cplus_tls_free(b);
  return ok ? 0 : 2;
}

/* ---- print entry points ------------------------------------------------- */
static int print_int(void) { cplus_print_value(CPLUS_PRINT_INT, 42); cplus_print_end(); return 0; }
static int print_uint(void) { cplus_print_value(CPLUS_PRINT_UINT, 7u); cplus_print_end(); return 0; }
static int print_double(void) { cplus_print_value(CPLUS_PRINT_DOUBLE, 2.5); cplus_print_end(); return 0; }
static int print_char(void) { cplus_print_value(CPLUS_PRINT_CHAR, 'A'); cplus_print_end(); return 0; }
static int print_bool(void) { cplus_print_value(CPLUS_PRINT_BOOL, 1); cplus_print_end(); return 0; }
static int print_str(void) { cplus_print_value(CPLUS_PRINT_STR, "hi"); cplus_print_end(); return 0; }
static int print_null_str(void) { cplus_print_value(CPLUS_PRINT_STR, (const char *)NULL); cplus_print_end(); return 0; }
static int print_unknown(void) { cplus_print_value(CPLUS_PRINT_UNKNOWN, 0); cplus_print_end(); return 0; }
static int print_multi(void) {
  cplus_print_value(CPLUS_PRINT_INT, 1);
  cplus_print_value(CPLUS_PRINT_INT, 2);
  cplus_print_end();
  return 0;
}
static int print_end_only(void) { cplus_print_end(); return 0; }

/* ---- scan entry points -------------------------------------------------- */
static int scan_int(void) {
  int x = 0;
  cplus_scan_value(&x, sizeof(x), CPLUS_SCAN_INT);
  return x == 42 ? 0 : 1;
}
static int scan_str_bounded(void) {
  char b[8];
  memset(b, 0, sizeof(b));
  cplus_scan_value(b, 4, CPLUS_SCAN_STR);
  return strcmp(b, "abc") == 0 ? 0 : 1;
}
static int scan_float(void) {
  float f = 0.f;
  cplus_scan_value(&f, sizeof(f), CPLUS_SCAN_FLOAT);
  return f == 3.5f ? 0 : 1;
}
static int scan_null_fatal(void) {
  cplus_scan_value(NULL, sizeof(int), CPLUS_SCAN_INT);
  return FAIL_SENTINEL;
}
static int scan_unsupported_fatal(void) {
  int x = 0;
  cplus_scan_value(&x, sizeof(x), (cplus_scan_kind)999);
  return FAIL_SENTINEL;
}
static int scan_eof_fatal(void) {
  int x = 0;
  cplus_scan_value(&x, sizeof(x), CPLUS_SCAN_INT);
  return FAIL_SENTINEL;
}

/* ---- header-level helpers / macros -------------------------------------- */
static int helper_rotl(void) {
  if (cplus_rotl32(0x80000000u, 1) != 1u) return 1;
  if (cplus_rotl32(0x12345678u, 0) != 0x12345678u) return 2;
  if (cplus_rotl32(0x12345678u, 32) != 0x12345678u) return 3;
  if (cplus_rotl32(0x12345678u, 33) != cplus_rotl32(0x12345678u, 1)) return 4;
  return 0;
}
static int helper_rotr(void) {
  if (cplus_rotr32(1u, 1) != 0x80000000u) return 1;
  if (cplus_rotr32(0x12345678u, 0) != 0x12345678u) return 2;
  if (cplus_rotr32(0x12345678u, 32) != 0x12345678u) return 3;
  return 0;
}
static int helper_move(void) {
  struct holder { void *p; int tag; } s;
  s.p = cplus_alloc(8);
  s.tag = 7;
  void *orig_p = s.p;
  int orig_tag = s.tag;
  struct holder m = cplus_move(s);
  if (m.p != orig_p || m.tag != orig_tag) { cplus_release(m.p); return 1; }
  if (s.p != NULL || s.tag != 0) { cplus_release(m.p); return 2; }
  cplus_release(m.p);
  return 0;
}
static int helper_new(void) {
  int *p = cplus_new(int);
  if (!p || *p != 0) return 1;
  cplus_release(p);
  return 0;
}
static int helper_new_array(void) {
  int *a = cplus_new_array(int, 5);
  if (!a) return 1;
  for (int i = 0; i < 5; ++i) if (a[i] != 0) return 2;
  cplus_release(a);
  return 0;
}
static int helper_countof(void) {
  int arr[7];
  (void)arr;
  return countof(arr) == 7 ? 0 : 1;
}

/* -------------------------------------------------------------------------
 * Case table
 * ------------------------------------------------------------------------- */
typedef struct {
  const char *id;
  const char *api;
  const char *dim;
  int (*fn)(void);
  int expect_exit;
  const char *expect_out; /* NULL => do not check stdout */
  int expect_diag;        /* 1 => stderr must contain the fatal diagnostic */
  const char *stdin_text; /* NULL => inherit stdin; else written to child stdin */
} case_t;

#define N(x) (sizeof(x) / sizeof((x)[0]))

static const case_t CASES[] = {
  /* cplus_fatal -- full error-code domain */
  {"fatal_101","cplus_fatal","error-code-domain",fatal_101,101,NULL,1,NULL},
  {"fatal_102","cplus_fatal","error-code-domain",fatal_102,102,NULL,1,NULL},
  {"fatal_103","cplus_fatal","error-code-domain",fatal_103,103,NULL,1,NULL},
  {"fatal_104","cplus_fatal","error-code-domain",fatal_104,104,NULL,1,NULL},
  {"fatal_105","cplus_fatal","error-code-domain",fatal_105,105,NULL,1,NULL},
  {"fatal_106","cplus_fatal","error-code-domain",fatal_106,106,NULL,1,NULL},
  {"fatal_107","cplus_fatal","error-code-domain",fatal_107,107,NULL,1,NULL},
  {"fatal_108","cplus_fatal","error-code-domain",fatal_108,108,NULL,1,NULL},
  {"fatal_109","cplus_fatal","error-code-domain",fatal_109,109,NULL,1,NULL},
  {"fatal_nullargs","cplus_fatal","null",fatal_nullargs,107,NULL,1,NULL},

  /* cplus_check_bounds */
  {"bounds_valid","cplus_check_bounds","valid",bounds_valid,0,NULL,0,NULL},
  {"bounds_null_base_valid","cplus_check_bounds","null",bounds_null_base_valid,0,NULL,0,NULL},
  {"bounds_fatal_eq","cplus_check_bounds","fatal",bounds_fatal_eq,101,NULL,1,NULL},
  {"bounds_fatal_zero","cplus_check_bounds","fatal",bounds_fatal_zero,101,NULL,1,NULL},
  {"bounds_fatal_huge","cplus_check_bounds","fatal",bounds_fatal_huge,101,NULL,1,NULL},

  /* cplus_check_nonnull */
  {"nonnull_valid","cplus_check_nonnull","valid",nonnull_valid,0,NULL,0,NULL},
  {"nonnull_fatal_null","cplus_check_nonnull","null-fatal",nonnull_fatal_null,102,NULL,1,NULL},
  {"nonnull_fatal_null_expr","cplus_check_nonnull","null-fatal",nonnull_fatal_null_expr,102,NULL,1,NULL},

  /* cplus_alloc */
  {"alloc_valid","cplus_alloc","valid",alloc_valid,0,NULL,0,NULL},
  {"alloc_zero","cplus_alloc","valid",alloc_zero,0,NULL,0,NULL},
  {"alloc_large","cplus_alloc","valid",alloc_large,0,NULL,0,NULL},
  {"alloc_many","cplus_alloc","valid",alloc_many,0,NULL,0,NULL},

  /* cplus_zalloc */
  {"zalloc_zeroed","cplus_zalloc","valid",zalloc_zeroed,0,NULL,0,NULL},
  {"zalloc_zero_size","cplus_zalloc","valid",zalloc_zero_size,0,NULL,0,NULL},

  /* cplus_alloc_array */
  {"array_valid","cplus_alloc_array","valid",array_valid,0,NULL,0,NULL},
  {"array_zero_count","cplus_alloc_array","valid",array_zero_count,0,NULL,0,NULL},
  {"array_zero_elem","cplus_alloc_array","valid",array_zero_elem,0,NULL,0,NULL},
  {"array_fatal_overflow","cplus_alloc_array","fatal",array_fatal_overflow,107,NULL,1,NULL},

  /* cplus_realloc */
  {"realloc_valid","cplus_realloc","valid",realloc_valid,0,NULL,0,NULL},
  {"realloc_null","cplus_realloc","null",realloc_null,0,NULL,0,NULL},
  {"realloc_zero","cplus_realloc","valid",realloc_zero,0,NULL,0,NULL},
  {"realloc_zero_null","cplus_realloc","null",realloc_zero_null,0,NULL,0,NULL},
  {"realloc_shrink","cplus_realloc","valid",realloc_shrink,0,NULL,0,NULL},
  {"realloc_fatal_uaf","cplus_realloc","use-after-free-fatal",realloc_fatal_uaf,105,NULL,1,NULL},

  /* cplus_zero */
  {"zero_valid","cplus_zero","valid",zero_valid,0,NULL,0,NULL},
  {"zero_null","cplus_zero","null",zero_null,0,NULL,0,NULL},
  {"zero_len0","cplus_zero","valid",zero_len0,0,NULL,0,NULL},

  /* cplus_release */
  {"release_valid","cplus_release","valid",release_valid,0,NULL,0,NULL},
  {"release_null","cplus_release","null",release_null,0,NULL,0,NULL},
  {"release_fatal_double","cplus_release","double-cleanup-fatal",release_fatal_double,104,NULL,1,NULL},
  {"release_fatal_stack","cplus_release","invalid-handle-fatal",release_fatal_stack,105,NULL,1,NULL},
  {"release_fatal_static","cplus_release","invalid-handle-fatal",release_fatal_static,105,NULL,1,NULL},

  /* cplus_alloc_bytes */
  {"bytes_valid","cplus_alloc_bytes","valid",bytes_valid,0,NULL,0,NULL},
  {"bytes_fatal_null","cplus_alloc_bytes","null-fatal",bytes_fatal_null,105,NULL,1,NULL},
  {"bytes_fatal_freed","cplus_alloc_bytes","use-after-free-fatal",bytes_fatal_freed,105,NULL,1,NULL},
  {"bytes_fatal_stack","cplus_alloc_bytes","invalid-handle-fatal",bytes_fatal_stack,105,NULL,1,NULL},

  /* cplus_check_live */
  {"live_valid","cplus_check_live","valid",live_valid,0,NULL,0,NULL},
  {"live_fatal_null","cplus_check_live","null-fatal",live_fatal_null,105,NULL,1,NULL},
  {"live_fatal_freed","cplus_check_live","use-after-free-fatal",live_fatal_freed,105,NULL,1,NULL},
  {"live_fatal_stack","cplus_check_live","invalid-handle-fatal",live_fatal_stack,105,NULL,1,NULL},

  /* cplus_alloc_count */
  {"count_valid","cplus_alloc_count","valid",count_valid,0,NULL,0,NULL},
  {"count_elem1","cplus_alloc_count","valid",count_elem1,0,NULL,0,NULL},
  {"count_fatal_zero_elem","cplus_alloc_count","fatal",count_fatal_zero_elem,105,NULL,1,NULL},
  {"count_fatal_null","cplus_alloc_count","null-fatal",count_fatal_null,105,NULL,1,NULL},
  {"count_fatal_freed","cplus_alloc_count","use-after-free-fatal",count_fatal_freed,105,NULL,1,NULL},

  /* cplus_check_index */
  {"index_valid","cplus_check_index","valid",index_valid,0,NULL,0,NULL},
  {"index_fatal_eq","cplus_check_index","fatal",index_fatal_eq,101,NULL,1,NULL},
  {"index_fatal_zero","cplus_check_index","fatal",index_fatal_zero,101,NULL,1,NULL},
  {"index_fatal_huge","cplus_check_index","fatal",index_fatal_huge,101,NULL,1,NULL},

  /* cplus_scope_push */
  {"push_valid","cplus_scope_push","valid",push_valid,0,NULL,0,NULL},
  {"push_null_scope","cplus_scope_push","null",push_null_scope,0,NULL,0,NULL},
  {"push_null_drop","cplus_scope_push","null",push_null_drop,0,NULL,0,NULL},
  {"push_null_ptr","cplus_scope_push","null",push_null_ptr,0,NULL,0,NULL},

  /* cplus_scope_cleanup */
  {"cleanup_valid","cplus_scope_cleanup","valid",cleanup_valid,0,NULL,0,NULL},
  {"cleanup_reverse","cplus_scope_cleanup","valid",cleanup_reverse,0,NULL,0,NULL},
  {"cleanup_double","cplus_scope_cleanup","double-cleanup",cleanup_double,0,NULL,0,NULL},
  {"cleanup_null_scope","cplus_scope_cleanup","null",cleanup_null_scope,0,NULL,0,NULL},
  {"cleanup_nested","cplus_scope_cleanup","nested-scopes",cleanup_nested,0,NULL,0,NULL},
  {"cleanup_managed","cplus_scope_cleanup","valid",cleanup_managed,0,NULL,0,NULL},
  {"cleanup_managed_drop_uaf","cplus_scope_cleanup","valid",cleanup_managed_drop_uaf,105,NULL,1,NULL},
  {"cleanup_fatal_double_free","cplus_scope_cleanup","double-cleanup-fatal",cleanup_fatal_double_free,104,NULL,1,NULL},
  {"cleanup_mixed","cplus_scope_cleanup","valid",cleanup_mixed,0,NULL,0,NULL},

  /* cplus_resource_check_borrow */
  {"borrow_valid","cplus_resource_check_borrow","valid",borrow_valid,0,NULL,0,NULL},
  {"borrow_fatal_null","cplus_resource_check_borrow","null-fatal",borrow_fatal_null,106,NULL,1,NULL},
  {"borrow_fatal_null_expr","cplus_resource_check_borrow","null-fatal",borrow_fatal_null_expr,106,NULL,1,NULL},

  /* mutex */
  {"mutex_valid","cplus_mutex_new/lock/unlock/free","valid",mutex_valid,0,NULL,0,NULL},
  {"mutex_null_ops","cplus_mutex_lock/unlock/free","null",mutex_null_ops,0,NULL,0,NULL},
  {"mutex_twice","cplus_mutex_new","valid",mutex_twice,0,NULL,0,NULL},

  /* TLS */
  {"tls_valid","cplus_tls_new/get/set/free","valid",tls_valid,0,NULL,0,NULL},
  {"tls_null_ops","cplus_tls_get/set/free","null",tls_null_ops,0,NULL,0,NULL},
  {"tls_overwrite","cplus_tls_set","valid",tls_overwrite,0,NULL,0,NULL},
  {"tls_two_slots","cplus_tls_new","valid",tls_two_slots,0,NULL,0,NULL},

  /* print */
  {"print_int","cplus_print_value","valid",print_int,0,"42\n",0,NULL},
  {"print_uint","cplus_print_value","valid",print_uint,0,"7\n",0,NULL},
  {"print_double","cplus_print_value","valid",print_double,0,"2.5\n",0,NULL},
  {"print_char","cplus_print_value","valid",print_char,0,"A\n",0,NULL},
  {"print_bool","cplus_print_value","valid",print_bool,0,"true\n",0,NULL},
  {"print_str","cplus_print_value","valid",print_str,0,"hi\n",0,NULL},
  {"print_null_str","cplus_print_value","null",print_null_str,0,"(null)\n",0,NULL},
  {"print_unknown","cplus_print_value","error-code-domain",print_unknown,0,"<unprintable>\n",0,NULL},
  {"print_multi","cplus_print_value","valid",print_multi,0,"1 2\n",0,NULL},
  {"print_end_only","cplus_print_end","valid",print_end_only,0,"\n",0,NULL},

  /* scan */
  {"scan_int","cplus_scan_value","valid",scan_int,0,NULL,0,"42\n"},
  {"scan_str_bounded","cplus_scan_value","valid",scan_str_bounded,0,NULL,0,"abcdef\n"},
  {"scan_float","cplus_scan_value","valid",scan_float,0,NULL,0,"3.5\n"},
  {"scan_null_fatal","cplus_scan_value","null-fatal",scan_null_fatal,109,NULL,1,"42\n"},
  {"scan_unsupported_fatal","cplus_scan_value","error-code-domain",scan_unsupported_fatal,108,NULL,1,"1\n"},
  {"scan_eof_fatal","cplus_scan_value","fatal",scan_eof_fatal,109,NULL,1,""},

  /* header helpers / macros */
  {"helper_rotl","cplus_rotl32","valid",helper_rotl,0,NULL,0,NULL},
  {"helper_rotr","cplus_rotr32","valid",helper_rotr,0,NULL,0,NULL},
  {"helper_move","cplus_move","valid",helper_move,0,NULL,0,NULL},
  {"helper_new","cplus_new","valid",helper_new,0,NULL,0,NULL},
  {"helper_new_array","cplus_new_array","valid",helper_new_array,0,NULL,0,NULL},
  {"helper_countof","countof","valid",helper_countof,0,NULL,0,NULL},
};

/* -------------------------------------------------------------------------
 * Runner
 * ------------------------------------------------------------------------- */
static void watchdog(int sig) { (void)sig; _exit(WATCHDOG_EXIT); }

int main(int argc, char **argv) {
  const char *env = getenv("EDGE100_OUT");
  if (env && *env) g_out = env;
  int verbose = (argc > 1 && strcmp(argv[1], "-v") == 0);

  int total = (int)N(CASES), passed = 0, failed = 0;

  for (int i = 0; i < total; ++i) {
    const case_t *c = &CASES[i];
    char outp[512], errp[512], inp[512];
    path_for(outp, sizeof(outp), c->id, "stdout");
    path_for(errp, sizeof(errp), c->id, "stderr");
    path_for(inp, sizeof(inp), c->id, "stdin");

    /* Parent prepares child stdin file (if any). */
    if (c->stdin_text) {
      FILE *f = fopen(inp, "wb");
      if (f) { fputs(c->stdin_text, f); fclose(f); }
    }

    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid < 0) {
      printf("CASE %s FAIL fork-failed\n", c->id);
      ++failed;
      continue;
    }
    if (pid == 0) {
      /* Child: redirect stdout/stderr/stdin, bound runtime with alarm. */
      freopen(outp, "wb", stdout);
      freopen(errp, "wb", stderr);
      if (c->stdin_text) freopen(inp, "rb", stdin);
      signal(SIGALRM, watchdog);
      alarm(2);
      int verdict = c->fn();
      /* _exit() bypasses stdio flush; print cases rely on redirected stdout. */
      fflush(stdout);
      fflush(stderr);
      _exit(verdict);
    }

    /* Parent: wait with no fancy polling; child alarm bounds the wait. */
    int st = 0;
    pid_t got = waitpid(pid, &st, 0);
    int rc = (got == pid && WIFEXITED(st)) ? WEXITSTATUS(st) : -1;

    int ok = 1;
    if (rc != c->expect_exit) ok = 0;
    if (ok && c->expect_diag) {
      if (!file_contains(errp, "C+ runtime error: ")) ok = 0;
    } else if (ok && !c->expect_diag) {
      if (file_nonempty(errp)) ok = 0; /* unexpected fatal diagnostic */
    }
    if (ok && c->expect_out) {
      char got_out[4096];
      long n = read_file(outp, got_out, (long)sizeof(got_out));
      if (n < 0 || strcmp(got_out, c->expect_out) != 0) ok = 0;
    }

    if (ok) { ++passed; if (verbose) printf("CASE %s PASS exit=%d\n", c->id, rc); }
    else {
      ++failed;
      printf("CASE %s FAIL exit=%d expect=%d api=%s dim=%s\n",
             c->id, rc, c->expect_exit, c->api, c->dim);
    }
  }

  /* Distinct API count (treat grouped names as their listed entry points). */
  int apis = 27;
  printf("EDGE100 runtime %d %d %d %d\n", apis, total, passed, failed);
  return 0;
}
