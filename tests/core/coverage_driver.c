#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <stdint.h>
#include "cplus/cplus.h"

static int drops = 0;
static jmp_buf fatal_jmp;
static void drop_one(void *p) { (void)p; drops++; }
static void fatal_hook(cplus_exit_code code, const char *kind) {
  (void)code; (void)kind;
  longjmp(fatal_jmp, 1);
}

#define TRY_FATAL(stmt) do { if (setjmp(fatal_jmp) == 0) { stmt; } } while (0)

int main(void) {
  cplus_fatal_hook = fatal_hook;
  cplus_runtime_log_level = 0;

  void *p = cplus_alloc(16);
  void *z = cplus_zalloc(8);
  int *a = cplus_new_array(int, 4);
  int *one = cplus_new(int);
  int *grown = cplus_realloc(p, 32);
  cplus_zero(z, 8);

  cplus_check_live(a);
  (void)cplus_alloc_bytes(a);
  (void)cplus_alloc_count(a, sizeof(int));
  (void)cplus_check_index(2, 4, __FILE__, __LINE__, "ok");
  cplus_check_bounds(a, 4, 1, __FILE__, __LINE__, "ok");
  cplus_check_nonnull(a, __FILE__, __LINE__, "ok");
  cplus_resource_check_borrow(a, __FILE__, __LINE__, "ok");

  cplus_scope scope = {0};
  cplus_scope_push(&scope, one, drop_one, CPLUS_CLEANUP_RESOURCE);
  cplus_scope_cleanup(&scope);

  void *m = cplus_mutex_new();
  cplus_mutex_lock(m);
  cplus_mutex_unlock(m);
  cplus_mutex_free(m);
  void *slot = cplus_tls_new();
  cplus_tls_set(slot, a);
  (void)cplus_tls_get(slot);
  cplus_tls_free(slot);

  char buf[16];
  (void)cplus_strcpy(buf, sizeof(buf), "core");
  (void)cplus_strnlen(buf, sizeof(buf));
  (void)cplus_memcpy(buf + 1, buf, 4);
  (void)cplus_memmove(buf, buf + 1, 3);

  /* Fatal paths, intercepted by the test hook. */
  TRY_FATAL((void)cplus_check_index(9, 3, __FILE__, __LINE__, "bad"));
  TRY_FATAL(cplus_check_bounds(a, 4, 9, __FILE__, __LINE__, "bad"));
  TRY_FATAL(cplus_check_nonnull(NULL, __FILE__, __LINE__, "bad"));
  TRY_FATAL(cplus_resource_check_borrow(NULL, __FILE__, __LINE__, "bad"));
  TRY_FATAL((void)cplus_alloc_array(SIZE_MAX, sizeof(int)));
  TRY_FATAL((void)cplus_strcpy(buf, 1, "toolong"));
  TRY_FATAL((void)cplus_memcpy(NULL, buf, 1));
  TRY_FATAL((void)cplus_memmove(NULL, buf, 1));
  TRY_FATAL((void)cplus_strnlen(NULL, 4));
  {
    int stack = 0;
    TRY_FATAL(cplus_release(&stack));
  }
  TRY_FATAL(cplus_release(a));
  int *final = cplus_new(int);
  cplus_release(final);

  /* Allocator growth and zero-size paths. */
  void *many[64];
  for (int i = 0; i < 64; i++) many[i] = cplus_alloc(8);
  for (int i = 0; i < 64; i++) cplus_release(many[i]);
  void *empty = cplus_alloc(0);
  cplus_release(empty);
  void *emptyz = cplus_zalloc(0);
  cplus_release(emptyz);
  void *rnull = cplus_realloc(NULL, 8);
  cplus_release(rnull);
  void *rzero = cplus_realloc(cplus_alloc(4), 0);

  /* I/O paths. */
  print("coverage", 1, 2.5, 'x');
  int si = 0; double sd = 0; char ss[16] = {0};
  scan(si, sd, ss);

  /* More error queries. */
  int stack2 = 0;
  TRY_FATAL((void)cplus_alloc_bytes(&stack2));
  TRY_FATAL((void)cplus_alloc_count(&stack2, 0));
  TRY_FATAL((void)cplus_check_live(&stack2));
  (void)cplus_scope_push(NULL, NULL, NULL, CPLUS_CLEANUP_RESOURCE);
  cplus_scope_cleanup(NULL);
  TRY_FATAL(cplus_fatal(CPLUS_EXIT_UNSUPPORTED, "test", __FILE__, __LINE__,
                        "hook"));

  (void)rzero;
  cplus_release(z);
  cplus_release(grown);
  return drops == 1 ? 0 : 1;
}
