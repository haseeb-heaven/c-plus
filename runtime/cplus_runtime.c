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
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>


size_t __cplus_bounds_check(size_t index, size_t length, const char *name) {
  if (index < length)
    return index;
  fprintf(stderr,
          "C+ error: index %zu is outside the bounds of '%s' (length %zu)\n",
          index, name ? name : "array", length);
  abort();
}

int cplus_runtime_log_level = 99; /* enabled by generated program constructor */

static void cplus_log_impl(int level, const char *file, int line,
                           const char *func, const char *fmt, ...) {
  if (level < cplus_runtime_log_level)
    return;
  va_list ap;
  va_start(ap, fmt);
  fprintf(stdout, "[%d] ", level);
  vfprintf(stdout, fmt, ap);
  fprintf(stdout, "  (%s:%d in %s)\n", file, line, func);
  va_end(ap);
}
#undef cplus_log
#define cplus_log(level, ...)                                                  \
  cplus_log_impl((int)(level), __FILE__, __LINE__, __func__, __VA_ARGS__)

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
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

void (*cplus_fatal_hook)(cplus_exit_code, const char *) = NULL;

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
  if (cplus_fatal_hook)
    cplus_fatal_hook(code, kind);
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

void cplus_check_ptr_formation(const void *base, size_t count, int64_t index,
                               const char *file, int line, const char *expr) {
  (void)base;
  (void)expr;
  if (index >= 0 && (uintmax_t)index <= (uintmax_t)count)
    return;
  char buf[192];
  snprintf(buf, sizeof(buf),
           "pointer offset %lld is outside [0, %llu] elements",
           (long long)index, (unsigned long long)count);
  cplus_print_fatal(CPLUS_EXIT_BOUNDS, "out-of-bounds pointer formation", file,
                    line, buf);
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

/* -------------------------------------------------------------------------
 * Native checked signed arithmetic
 *
 * The exact mathematical result is computed in 128 bits, so the check itself
 * never overflows.  We compare against the width of the source expression,
 * not int64_t: `int a = INT_MAX; a + 1` must trap even though it fits in 64
 * bits.  This is independent of UBSan and is emitted only for expressions the
 * compiler cannot fold to a constant.
 * ------------------------------------------------------------------------- */
static int64_t cplus_checked_result(__int128 r, unsigned bits,
                                    unsigned isSigned,
                                    const char *operation) {
  if (bits == 0 || bits > 64 || !isSigned)
    return (int64_t)r; /* unsigned arithmetic is defined to wrap */
  __int128 lo = -((__int128)1 << (bits - 1));
  __int128 hi = ((__int128)1 << (bits - 1)) - 1;
  if (r < lo || r > hi) {
    char buf[160];
    snprintf(buf, sizeof(buf),
             "%s result does not fit in a %u-bit signed integer", operation,
             bits);
    cplus_print_fatal(CPLUS_EXIT_ARITHMETIC, "signed integer overflow", NULL, 0,
                      buf);
  }
  return (int64_t)r;
}

static void cplus_arith_fatal(const char *kind, const char *msg) {
  cplus_print_fatal(CPLUS_EXIT_ARITHMETIC, kind, NULL, 0, msg);
}

int64_t __cplus_checked_add_i64(int64_t a, int64_t b, unsigned bits,
                                unsigned isSigned) {
  return cplus_checked_result((__int128)a + (__int128)b, bits, isSigned,
                              "addition");
}

int64_t __cplus_checked_sub_i64(int64_t a, int64_t b, unsigned bits,
                                unsigned isSigned) {
  return cplus_checked_result((__int128)a - (__int128)b, bits, isSigned,
                              "subtraction");
}

int64_t __cplus_checked_mul_i64(int64_t a, int64_t b, unsigned bits,
                                unsigned isSigned) {
  return cplus_checked_result((__int128)a * (__int128)b, bits, isSigned,
                              "multiplication");
}

/* Division and remainder: reject a zero divisor and the signed INT_MIN/-1
 * overflow.  Predicates follow Clang's EmitUndefinedBehaviorIntegerDivAndRemCheck
 * and UBSan's handleDivremOverflowImpl (Apache-2.0 WITH LLVM-exception),
 * implemented here without any sanitizer dependency. */
static int64_t cplus_checked_divrem(int64_t a, int64_t b, unsigned bits,
                                    unsigned isSigned, int remainder) {
  if (b == 0)
    cplus_arith_fatal("division by zero",
                      remainder ? "remainder by zero" : "division by zero");
  if (isSigned && bits > 1 && bits <= 64 && b == -1 &&
      a == (int64_t)(-((__int128)1 << (bits - 1))))
    cplus_arith_fatal("signed integer overflow",
                      remainder ? "remainder overflows the source type"
                                : "division overflows the source type");
  return remainder ? a % b : a / b;
}

int64_t __cplus_checked_div_i64(int64_t a, int64_t b, unsigned bits,
                                unsigned isSigned) {
  return cplus_checked_divrem(a, b, bits, isSigned, 0);
}

int64_t __cplus_checked_rem_i64(int64_t a, int64_t b, unsigned bits,
                                unsigned isSigned) {
  return cplus_checked_divrem(a, b, bits, isSigned, 1);
}

/* Shifts: enforce 0 <= count < width.  For signed left shift also reject a
 * negative left operand and any non-zero bit shifted out of the value bits,
 * matching Clang's EmitShl predicates. */
static int64_t cplus_checked_shift(int64_t a, int64_t b, unsigned bits,
                                   unsigned isSigned, int left) {
  if (bits == 0 || bits > 64)
    return left ? (int64_t)((uint64_t)a << (uint64_t)b)
                : (int64_t)(a >> (uint64_t)b);
  if (b < 0 || (unsigned long long)b >= bits) {
    char buf[160];
    snprintf(buf, sizeof(buf), "shift count %lld is outside [0, %u)",
             (long long)b, bits);
    cplus_arith_fatal("invalid shift", buf);
  }
  if (left && isSigned) {
    if (a < 0)
      cplus_arith_fatal("invalid shift",
                        "left shift of a negative signed value");
    uint64_t ua = (uint64_t)a;
    unsigned count = (unsigned)b;
    uint64_t limit = (~(uint64_t)0) >> (64 - bits);
    if ((ua << count) & ~limit)
      cplus_arith_fatal("invalid shift",
                        "left shift loses bits above the source width");
  }
  return left ? (int64_t)((uint64_t)a << (uint64_t)b)
              : (int64_t)(a >> (uint64_t)b);
}

int64_t __cplus_checked_shl_i64(int64_t a, int64_t b, unsigned bits,
                                unsigned isSigned) {
  return cplus_checked_shift(a, b, bits, isSigned, 1);
}

int64_t __cplus_checked_shr_i64(int64_t a, int64_t b, unsigned bits,
                                unsigned isSigned) {
  return cplus_checked_shift(a, b, bits, isSigned, 0);
}

/* Value-based narrowing check.  Predicates follow Clang's
 * EmitIntegerTruncationCheckHelper (any-extend then compare) and GSL's
 * narrow (round-trip equality), implemented here in plain C with no
 * sanitizer runtime.  Only the value is checked; a narrow *type* is fine. */
int64_t __cplus_checked_narrow_i64(uint64_t value, unsigned sb, unsigned ss,
                                   unsigned db, unsigned ds) {
  __int128 src = (__int128)value;
  if (ss && sb > 0 && sb < 64 && ((value >> (sb - 1)) & 1u))
    src = (__int128)(int64_t)(value | (~((uint64_t)0) << sb));
  uint64_t mask = db >= 64 ? ~((uint64_t)0)
                           : ((((uint64_t)1) << db) - 1);
  uint64_t truncated = value & mask;
  __int128 dst = (__int128)truncated;
  if (ds && db > 0 && db < 64 && ((truncated >> (db - 1)) & 1u))
    dst = (__int128)(int64_t)(truncated | (~((uint64_t)0) << db));
  if (src != dst) {
    char buf[192];
    snprintf(buf, sizeof(buf),
             "implicit conversion from a %u-bit %s value changed it to a "
             "%u-bit %s value",
             sb, ss ? "signed" : "unsigned", db, ds ? "signed" : "unsigned");
    cplus_print_fatal(CPLUS_EXIT_CONVERSION, "implicit integer conversion",
                      NULL, 0, buf);
  }
  return (int64_t)dst;
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
 * Managed allocations are tracked in a hash table so that invalid and double
 * releases are detected without touching freed memory.  The metadata lives
 * beside the allocation, never in front of the user pointer.
 *
 * A small direct-mapped cache remembers recently freed addresses, which lets
 * us report a double release even when malloc has already handed the same
 * address out again.  It is bounded, so bookkeeping stays O(1).
 * ------------------------------------------------------------------------- */

#define CPLUS_LIVE_INITIAL 64u
#define CPLUS_RECENT_SLOTS 4096u
#define CPLUS_TOMBSTONE ((void *)(uintptr_t)-1)

typedef struct cplus_live_slot {
  void *ptr;
  size_t size;
} cplus_live_slot;

static cplus_live_slot *cplus_live = NULL;
static size_t cplus_live_cap = 0;
static size_t cplus_live_count = 0;
static size_t cplus_live_deleted = 0;
static void *cplus_recent[CPLUS_RECENT_SLOTS];

/* Opt-in temporal-safety experiment: keep released blocks out of malloc's
 * reuse pool for a bounded time so stale aliases are detected instead of
 * being misattributed to a new allocation.  Disabled by default. */
#define CPLUS_QUARANTINE_SLOTS 1024u
static void *cplus_quarantine[CPLUS_QUARANTINE_SLOTS];
static size_t cplus_quarantine_next = 0;
static size_t cplus_quarantine_count = 0;

static int cplus_quarantine_enabled(void) {
  const char *env = getenv("CPLUS_QUARANTINE");
  return env && env[0] != '0';
}

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

static size_t cplus_hash(const void *p) {
  uintptr_t x = (uintptr_t)p;
  x ^= x >> 30;
  x *= (uintptr_t)0xbf58476d1ce4e5b9ULL;
  x ^= x >> 27;
  x *= (uintptr_t)0x94d049bb133111ebULL;
  x ^= x >> 31;
  return (size_t)x;
}

static size_t cplus_recent_index(const void *p) {
  return (size_t)(cplus_hash(p) & (CPLUS_RECENT_SLOTS - 1u));
}

static cplus_live_slot *cplus_find_live(const void *p) {
  if (cplus_live_cap == 0)
    return NULL;
  size_t mask = cplus_live_cap - 1;
  size_t i = cplus_hash(p) & mask;
  for (size_t probe = 0; probe <= mask; ++probe) {
    void *slot = cplus_live[i].ptr;
    if (slot == p)
      return &cplus_live[i];
    if (slot == NULL)
      return NULL;
    i = (i + 1) & mask;
  }
  return NULL;
}

static void cplus_live_insert(const void *p, size_t size) {
  size_t mask = cplus_live_cap - 1;
  size_t i = cplus_hash(p) & mask;
  while (cplus_live[i].ptr != NULL && cplus_live[i].ptr != CPLUS_TOMBSTONE)
    i = (i + 1) & mask;
  if (cplus_live[i].ptr == CPLUS_TOMBSTONE)
    --cplus_live_deleted;
  cplus_live[i].ptr = (void *)p;
  cplus_live[i].size = size;
  ++cplus_live_count;
}

/* Called with the allocator lock held. */
static void cplus_live_grow(void) {
  size_t cap = cplus_live_cap ? cplus_live_cap * 2 : CPLUS_LIVE_INITIAL;
  cplus_live_slot *fresh = (cplus_live_slot *)calloc(cap, sizeof(*fresh));
  if (!fresh) {
    cplus_print_fatal(CPLUS_EXIT_RESOURCE, "out of memory", NULL, 0,
                      "allocation bookkeeping failed");
  }
  for (size_t k = 0; k < cplus_live_cap; ++k) {
    void *p = cplus_live[k].ptr;
    if (p != NULL && p != CPLUS_TOMBSTONE) {
      size_t mask = cap - 1;
      size_t i = cplus_hash(p) & mask;
      while (fresh[i].ptr != NULL)
        i = (i + 1) & mask;
      fresh[i] = cplus_live[k];
    }
  }
  free(cplus_live);
  cplus_live = fresh;
  cplus_live_cap = cap;
  cplus_live_deleted = 0;
}

void *cplus_alloc(size_t size) {
  if (size <= 0)
    size = 1;
  void *p = malloc(size);
  if (!p) {
    cplus_print_fatal(CPLUS_EXIT_RESOURCE, "out of memory", NULL, 0,
                      "managed allocation failed");
  }
  cplus_alloc_lock();
  if (cplus_live_count + cplus_live_deleted + 1 > cplus_live_cap * 3 / 4)
    cplus_live_grow();
  cplus_live_insert(p, size);
  cplus_alloc_unlock();
  return p;
}

void *cplus_zalloc(size_t size) {
  void *p = cplus_alloc(size);
  memset(p, 0, size ? size : 1);
  return p;
}

void *cplus_alloc_array(size_t count, size_t element_size) {
  if (element_size == 0)
    element_size = 1;
  if (count > SIZE_MAX / element_size)
    cplus_log(CPLUS_LOG_ERROR, "allocation overflow count=%zu elem=%zu", count,
              element_size);
  if (count > SIZE_MAX / element_size)
    cplus_print_fatal(CPLUS_EXIT_RESOURCE, "allocation overflow", NULL, 0,
                      "array size multiplication overflows size_t");
  return cplus_zalloc(count * element_size);
}

void *cplus_realloc(void *ptr, size_t size) {
  cplus_log(CPLUS_LOG_DEBUG, "cplus_realloc ptr=%p size=%zu", ptr, size);
  if (!ptr)
    return cplus_alloc(size);
  if (size == 0) {
    cplus_release(ptr);
    return NULL;
  }
  /* cplus_realloc is an ownership transfer: the old pointer is released and
   * every old alias becomes a dangling pointer, so stale use is detected. */
  void *fresh = cplus_alloc(size);
  cplus_alloc_lock();
  cplus_live_slot *slot = cplus_find_live(ptr);
  if (slot)
    memcpy(fresh, ptr, size < slot->size ? size : slot->size);
  cplus_alloc_unlock();
  cplus_release(ptr);
  return fresh;
}

void cplus_zero(void *p, size_t size) {
  if (p && size)
    memset(p, 0, size);
}

void cplus_release(void *ptr) {
  if (!ptr)
    return;
  cplus_log(CPLUS_LOG_DEBUG, "cplus_release ptr=%p", ptr);

  cplus_alloc_lock();
  cplus_live_slot *slot = cplus_find_live(ptr);
  if (!slot) {
    int recently_freed = cplus_recent[cplus_recent_index(ptr)] == ptr;
    cplus_alloc_unlock();
    if (recently_freed)
      cplus_print_fatal(CPLUS_EXIT_DOUBLE_FREE, "double free", NULL, 0,
                        "managed memory released twice");
    cplus_print_fatal(CPLUS_EXIT_TYPE_CONFUSION, "invalid free", NULL, 0,
                      "pointer was not produced by the C+ allocator");
  }
  slot->ptr = CPLUS_TOMBSTONE;
  --cplus_live_count;
  ++cplus_live_deleted;
  cplus_recent[cplus_recent_index(ptr)] = ptr;
  if (cplus_quarantine_enabled()) {
    void *oldest = cplus_quarantine[cplus_quarantine_next];
    cplus_quarantine[cplus_quarantine_next] = ptr;
    cplus_quarantine_next = (cplus_quarantine_next + 1) %
                            CPLUS_QUARANTINE_SLOTS;
    if (cplus_quarantine_count < CPLUS_QUARANTINE_SLOTS)
      ++cplus_quarantine_count;
    cplus_alloc_unlock();
    if (oldest)
      free(oldest); /* bounded: at most CPLUS_QUARANTINE_SLOTS delayed frees */
    return;
  }
  cplus_alloc_unlock();

  free(ptr);
}

void cplus_check_live(const void *ptr) {
  if (!ptr)
    cplus_print_fatal(CPLUS_EXIT_TYPE_CONFUSION, "dangling pointer", NULL, 0,
                      "null pointer is not a live allocation");
  cplus_alloc_lock();
  cplus_live_slot *slot = cplus_find_live(ptr);
  cplus_alloc_unlock();
  if (!slot)
    cplus_log(CPLUS_LOG_WARN, "dangling pointer query ptr=%p", ptr);
  if (!slot)
    cplus_print_fatal(CPLUS_EXIT_TYPE_CONFUSION, "dangling pointer", NULL, 0,
                      "pointer is no longer a live C+ allocation");
}

size_t cplus_alloc_bytes(const void *ptr) {
  cplus_log(CPLUS_LOG_TRACE, "cplus_alloc_bytes ptr=%p", ptr);
  if (!ptr)
    cplus_print_fatal(CPLUS_EXIT_TYPE_CONFUSION, "invalid length query", NULL,
                      0, "null pointer has no allocation length");
  cplus_alloc_lock();
  cplus_live_slot *slot = cplus_find_live(ptr);
  size_t bytes = slot ? slot->size : 0;
  cplus_alloc_unlock();
  if (!slot)
    cplus_print_fatal(CPLUS_EXIT_TYPE_CONFUSION, "invalid length query", NULL,
                      0, "pointer is not a live C+ allocation");
  return bytes;
}

size_t cplus_alloc_count(const void *ptr, size_t elem_size) {
  cplus_log(CPLUS_LOG_TRACE, "cplus_alloc_count ptr=%p elem=%zu", ptr,
            elem_size);
  if (elem_size == 0)
    cplus_print_fatal(CPLUS_EXIT_TYPE_CONFUSION, "invalid length query", NULL,
                      0, "element size must be greater than zero");
  return cplus_alloc_bytes(ptr) / elem_size;
}

size_t cplus_check_index(size_t index, size_t count, const char *file,
                         int line, const char *expr) {
  cplus_log(CPLUS_LOG_DEBUG, "cplus_check_index index=%zu count=%zu", index,
            count);
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
  cplus_log(CPLUS_LOG_TRACE, "cplus_scope_push scope=%p ptr=%p", scope, ptr);
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
  cplus_log(CPLUS_LOG_DEBUG, "cplus_scope_cleanup scope=%p", scope);
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

/* -------------------------------------------------------------------------
 * Modern I/O: type-generic print/scan
 *
 * The state struct below is a C struct with functions operating on it - the
 * same shape a C+ `class` lowers to.  It remembers whether a value has
 * already been printed on the current line so print() can insert spaces
 * without a format string.
 * ------------------------------------------------------------------------- */

static cplus_print_state cplus_print_line = {1};

static void cplus_print_separator(void) {
  if (cplus_print_line.first)
    cplus_print_line.first = 0;
  else
    putchar(' ');
}

void cplus_print_value(cplus_print_kind kind, ...) {
  cplus_log(CPLUS_LOG_TRACE, "cplus_print_value kind=%d", (int)kind);
  va_list ap;
  va_start(ap, kind);
  cplus_print_separator();

  switch (kind) {
  case CPLUS_PRINT_INT:
    printf("%d", va_arg(ap, int));
    break;
  case CPLUS_PRINT_UINT:
    printf("%u", va_arg(ap, unsigned int));
    break;
  case CPLUS_PRINT_LONG:
    printf("%ld", va_arg(ap, long));
    break;
  case CPLUS_PRINT_ULONG:
    printf("%lu", va_arg(ap, unsigned long));
    break;
  case CPLUS_PRINT_LLONG:
    printf("%lld", va_arg(ap, long long));
    break;
  case CPLUS_PRINT_ULLONG:
    printf("%llu", va_arg(ap, unsigned long long));
    break;
  case CPLUS_PRINT_DOUBLE:
    printf("%g", va_arg(ap, double));
    break;
  case CPLUS_PRINT_LDOUBLE:
    printf("%Lg", va_arg(ap, long double));
    break;
  case CPLUS_PRINT_CHAR:
    printf("%c", (char)va_arg(ap, int));
    break;
  case CPLUS_PRINT_BOOL:
    printf("%s", va_arg(ap, int) ? "true" : "false");
    break;
  case CPLUS_PRINT_STR: {
    const char *text = va_arg(ap, const char *);
    fputs(text ? text : "(null)", stdout);
    break;
  }
  default:
    fputs("<unprintable>", stdout);
    break;
  }
  va_end(ap);
}

void cplus_print_end(void) {
  putchar('\n');
  cplus_print_line.first = 1;
}

void cplus_scan_value(void *dest, size_t size, cplus_scan_kind kind) {
  int rc = 0;
  cplus_log(CPLUS_LOG_DEBUG, "cplus_scan_value dest=%p size=%zu kind=%d", dest,
            size, (int)kind);
  if (!dest)
    cplus_print_fatal(CPLUS_EXIT_IO, "input error", NULL, 0,
                      "scan destination is null");

  switch (kind) {
  case CPLUS_SCAN_INT:
    rc = scanf("%d", (int *)dest);
    break;
  case CPLUS_SCAN_UINT:
    rc = scanf("%u", (unsigned int *)dest);
    break;
  case CPLUS_SCAN_LONG:
    rc = scanf("%ld", (long *)dest);
    break;
  case CPLUS_SCAN_ULONG:
    rc = scanf("%lu", (unsigned long *)dest);
    break;
  case CPLUS_SCAN_LLONG:
    rc = scanf("%lld", (long long *)dest);
    break;
  case CPLUS_SCAN_ULLONG:
    rc = scanf("%llu", (unsigned long long *)dest);
    break;
  case CPLUS_SCAN_FLOAT:
    rc = scanf("%f", (float *)dest);
    break;
  case CPLUS_SCAN_DOUBLE:
    rc = scanf("%lf", (double *)dest);
    break;
  case CPLUS_SCAN_CHAR:
    rc = scanf(" %c", (char *)dest);
    break;
  case CPLUS_SCAN_STR: {
    char token[4096];
    rc = scanf("%4095s", token);
    if (rc == 1 && size > 0) {
      size_t n = strlen(token);
      if (n >= size)
        n = size - 1;
      memcpy(dest, token, n);
      ((char *)dest)[n] = '\0';
    }
    break;
  }
  default:
    cplus_print_fatal(CPLUS_EXIT_UNSUPPORTED, "input error", NULL, 0,
                      "unsupported scan type");
  }

  if (rc != 1)
    cplus_print_fatal(CPLUS_EXIT_IO, "input error", NULL, 0,
                      "failed to read a value from stdin");
}
