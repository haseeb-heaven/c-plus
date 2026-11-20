/*===-- cplus_runtime.c - C+ runtime implementation ---------------*- C -*-===
 *
 * The C+ runtime.  This file is compiled into every C+ program that uses
 * managed memory, RAII resources or the concurrency primitives.
 *
 * Design constraints:
 *   - It must be usable from plain C (legacy mode) without changes.
 *   - It must never itself corrupt memory.  Every failure path terminates
 *     the process with a deterministic exit code.
 *   - It must not depend on a C++ runtime so that C+ programs can link
 *     against the ordinary C standard library only.
 *
 *===----------------------------------------------------------------------===*/

#include "cplus/cplus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#include <unistd.h>
#define CPLUS_POSIX 1
#endif

/* -------------------------------------------------------------------------
 * Diagnostics
 * ------------------------------------------------------------------------- */

static void cplus_print_fatal(cplus_exit_code code, const char *kind,
                              const char *file, int line, const char *msg) {
  fflush(stdout);
  fprintf(stderr, "C+ runtime error: %s", kind ? kind : "safety violation");
  if (file) {
    fprintf(stderr, " at %s:%d", file, line);
  }
  if (msg && *msg) {
    fprintf(stderr, ": %s", msg);
  }
  fputc('\n', stderr);
  fflush(stderr);
  _Exit((int)code);
}

void cplus_fatal(cplus_exit_code code, const char *kind, const char *file,
                 int line, const char *msg) {
  cplus_print_fatal(code, kind, file, line, msg);
}

void cplus_check_bounds(const void *base, size_t count, size_t index,
                        const char *file, int line, const char *expr) {
  (void)base;
  if (index < count)
    return;
  char buf[256];
  snprintf(buf, sizeof(buf), "index %zu is out of bounds for %zu elements (%s)",
           index, count, expr ? expr : "");
  cplus_print_fatal(CPLUS_EXIT_BOUNDS, "out-of-bounds access", file, line, buf);
}

void cplus_check_nonnull(const void *ptr, const char *file, int line,
                         const char *expr) {
  if (ptr != NULL)
    return;
  char buf[256];
  snprintf(buf, sizeof(buf), "null pointer dereference (%s)",
           expr ? expr : "");
  cplus_print_fatal(CPLUS_EXIT_NULL_POINTER, "null pointer dereference", file,
                    line, buf);
}

void cplus_resource_check_borrow(const void *self, const char *file, int line,
                                 const char *expr) {
  if (self != NULL)
    return;
  char buf[256];
  snprintf(buf, sizeof(buf), "borrowed resource is null (%s)",
           expr ? expr : "");
  cplus_print_fatal(CPLUS_EXIT_OWNERSHIP, "invalid resource borrow", file,
                    line, buf);
}

/* -------------------------------------------------------------------------
 * Managed memory
 *
 * Managed allocations are tracked in a side table so that invalid and double
 * releases are detected instead of corrupting the heap.  The metadata is
 * never stored in front of the user pointer and is never freed, so a stale
 * pointer can be looked up safely.
 * ------------------------------------------------------------------------- */

typedef struct cplus_alloc_record {
  void *ptr;
  size_t size;
  int released;
  struct cplus_alloc_record *next;
} cplus_alloc_record;

static cplus_alloc_record *cplus_alloc_head = NULL;

#ifdef CPLUS_POSIX
static pthread_mutex_t cplus_alloc_mutex = PTHREAD_MUTEX_INITIALIZER;
static void cplus_alloc_lock(void) { pthread_mutex_lock(&cplus_alloc_mutex); }
static void cplus_alloc_unlock(void) {
  pthread_mutex_unlock(&cplus_alloc_mutex);
}
#else
static void cplus_alloc_lock(void) {}
static void cplus_alloc_unlock(void) {}
#endif

void *cplus_alloc(size_t size) {
  if (size == 0)
    size = 1;
  void *p = malloc(size);
  if (!p) {
    cplus_print_fatal(CPLUS_EXIT_RESOURCE, "out of memory", NULL, 0,
                      "managed allocation failed");
  }
  cplus_alloc_record *r = (cplus_alloc_record *)malloc(sizeof(*r));
  if (!r) {
    free(p);
    cplus_print_fatal(CPLUS_EXIT_RESOURCE, "out of memory", NULL, 0,
                      "allocation bookkeeping failed");
  }
  r->ptr = p;
  r->size = size;
  r->released = 0;

  cplus_alloc_lock();
  /* If the allocator reused the address of a released block, recycle its
   * record.  Keeping released records lets us report a lost stale pointer as
   * a double release instead of silently accepting it. */
  cplus_alloc_record *reuse = NULL;
  for (cplus_alloc_record *it = cplus_alloc_head; it; it = it->next) {
    if (it->released && it->ptr == p) {
      reuse = it;
      break;
    }
  }
  if (reuse) {
    reuse->size = size;
    reuse->released = 0;
    free(r);
    cplus_alloc_unlock();
    return p;
  }
  r->next = cplus_alloc_head;
  cplus_alloc_head = r;
  cplus_alloc_unlock();
  return p;
}

void *cplus_zalloc(size_t size) {
  void *p = cplus_alloc(size);
  memset(p, 0, size ? size : 1);
  return p;
}

void *cplus_realloc(void *ptr, size_t size) {
  if (!ptr)
    return cplus_alloc(size);
  if (size == 0) {
    cplus_release(ptr);
    return NULL;
  }
  /* Shared realloc is not supported: managed allocations are scope-owned.
   * Allocate a new block of the requested size instead of risking an
   * invalidated pointer. */
  void *fresh = cplus_alloc(size);
  cplus_alloc_record *r = NULL;
  cplus_alloc_lock();
  for (cplus_alloc_record *it = cplus_alloc_head; it; it = it->next) {
    if (it->ptr == ptr) {
      r = it;
      break;
    }
  }
  if (r && !r->released) {
    memcpy(fresh, ptr, size < r->size ? size : r->size);
  }
  cplus_alloc_unlock();
  return fresh;
}

void cplus_zero(void *p, size_t size) {
  if (p && size)
    memset(p, 0, size);
}

void cplus_release(void *ptr) {
  if (!ptr)
    return;

  cplus_alloc_lock();
  cplus_alloc_record *r = NULL;
  for (cplus_alloc_record *it = cplus_alloc_head; it; it = it->next) {
    if (it->ptr == ptr) {
      r = it;
      break;
    }
  }
  if (!r) {
    cplus_alloc_unlock();
    cplus_print_fatal(CPLUS_EXIT_TYPE_CONFUSION, "invalid free", NULL, 0,
                      "pointer was not produced by the C+ allocator");
  }
  if (r->released) {
    cplus_alloc_unlock();
    cplus_print_fatal(CPLUS_EXIT_DOUBLE_FREE, "double free", NULL, 0,
                      "managed memory released twice");
  }
  r->released = 1;
  void *raw = r->ptr;
  cplus_alloc_unlock();

  free(raw);
}

size_t cplus_check_index(size_t index, size_t count, const char *file,
                         int line, const char *expr) {
  if (index < count)
    return index;
  char buf[256];
  snprintf(buf, sizeof(buf), "index %zu is out of bounds for %zu elements (%s)",
           index, count, expr ? expr : "");
  cplus_print_fatal(CPLUS_EXIT_BOUNDS, "out-of-bounds access", file, line, buf);
  return index;
}

/* -------------------------------------------------------------------------
 * Scope guards (RAII)
 * ------------------------------------------------------------------------- */

int cplus_scope_push(cplus_scope *scope, void *ptr, void (*drop)(void *),
                     cplus_cleanup_kind kind) {
  if (!scope)
    return -1;
  cplus_cleanup *c = (cplus_cleanup *)malloc(sizeof(cplus_cleanup));
  if (!c) {
    cplus_print_fatal(CPLUS_EXIT_RESOURCE, "out of memory", NULL, 0,
                      "cannot register cleanup record");
  }
  c->ptr = ptr;
  c->drop = drop;
  c->kind = kind;
  c->next = scope->head;
  scope->head = c;
  return 0;
}

void cplus_scope_cleanup(cplus_scope *scope) {
  if (!scope)
    return;
  /* Cleanups run in reverse order of registration: the most recently
   * constructed resource is destroyed first. */
  cplus_cleanup *c = scope->head;
  scope->head = NULL;
  while (c) {
    cplus_cleanup *next = c->next;
    if (c->ptr && c->drop) {
      if (c->kind == CPLUS_CLEANUP_MANAGED)
        cplus_release(c->ptr);
      else
        c->drop(c->ptr);
    }
    free(c);
    c = next;
  }
}

/* -------------------------------------------------------------------------
 * Concurrency primitives
 * ------------------------------------------------------------------------- */

#ifdef CPLUS_POSIX
typedef struct cplus_mutex {
  pthread_mutex_t m;
} cplus_mutex;

typedef struct cplus_tls_slot {
  pthread_key_t key;
} cplus_tls_slot;

void *cplus_mutex_new(void) {
  cplus_mutex *m = (cplus_mutex *)malloc(sizeof(cplus_mutex));
  if (!m) {
    cplus_print_fatal(CPLUS_EXIT_RESOURCE, "out of memory", NULL, 0,
                      "cannot create mutex");
  }
  if (pthread_mutex_init(&m->m, NULL) != 0) {
    free(m);
    cplus_print_fatal(CPLUS_EXIT_RESOURCE, "mutex failure", NULL, 0,
                      "pthread_mutex_init failed");
  }
  return m;
}

void cplus_mutex_lock(void *mp) {
  if (!mp)
    return;
  cplus_mutex *m = (cplus_mutex *)mp;
  if (pthread_mutex_lock(&m->m) != 0) {
    cplus_print_fatal(CPLUS_EXIT_RESOURCE, "mutex failure", NULL, 0,
                      "pthread_mutex_lock failed");
  }
}

void cplus_mutex_unlock(void *mp) {
  if (!mp)
    return;
  cplus_mutex *m = (cplus_mutex *)mp;
  if (pthread_mutex_unlock(&m->m) != 0) {
    cplus_print_fatal(CPLUS_EXIT_RESOURCE, "mutex failure", NULL, 0,
                      "pthread_mutex_unlock failed");
  }
}

void cplus_mutex_free(void *mp) {
  if (!mp)
    return;
  cplus_mutex *m = (cplus_mutex *)mp;
  pthread_mutex_destroy(&m->m);
  free(m);
}

void *cplus_tls_new(void) {
  cplus_tls_slot *s = (cplus_tls_slot *)malloc(sizeof(cplus_tls_slot));
  if (!s) {
    cplus_print_fatal(CPLUS_EXIT_RESOURCE, "out of memory", NULL, 0,
                      "cannot create thread-local slot");
  }
  if (pthread_key_create(&s->key, NULL) != 0) {
    free(s);
    cplus_print_fatal(CPLUS_EXIT_RESOURCE, "TLS failure", NULL, 0,
                      "pthread_key_create failed");
  }
  return s;
}

void *cplus_tls_get(void *slotp) {
  if (!slotp)
    return NULL;
  cplus_tls_slot *s = (cplus_tls_slot *)slotp;
  return pthread_getspecific(s->key);
}

void cplus_tls_set(void *slotp, void *value) {
  if (!slotp)
    return;
  cplus_tls_slot *s = (cplus_tls_slot *)slotp;
  if (pthread_setspecific(s->key, value) != 0) {
    cplus_print_fatal(CPLUS_EXIT_RESOURCE, "TLS failure", NULL, 0,
                      "pthread_setspecific failed");
  }
}

void cplus_tls_free(void *slotp) {
  if (!slotp)
    return;
  cplus_tls_slot *s = (cplus_tls_slot *)slotp;
  pthread_key_delete(s->key);
  free(s);
}
#else
void *cplus_mutex_new(void) { return NULL; }
void cplus_mutex_lock(void *m) { (void)m; }
void cplus_mutex_unlock(void *m) { (void)m; }
void cplus_mutex_free(void *m) { (void)m; }
void *cplus_tls_new(void) { return NULL; }
void *cplus_tls_get(void *s) { (void)s; return NULL; }
void cplus_tls_set(void *s, void *v) { (void)s; (void)v; }
void cplus_tls_free(void *s) { (void)s; }
#endif
