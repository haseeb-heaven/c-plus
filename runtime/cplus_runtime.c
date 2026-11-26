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
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <setjmp.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* Thread-local storage keyword.  Generated C+ code and the runtime agree on
 * __thread for GNU/Clang builds; MSVC uses __declspec(thread). */
#if defined(_MSC_VER)
#define CPLUS_TLS __declspec(thread)
#else
#define CPLUS_TLS __thread
#endif

static void cplus_print_fatal(cplus_exit_code code, const char *kind,
                              const char *file, int line, const char *msg);

/* C+ exception frames. The stack is thread-local and holds only ordinary C
 * pointers; raising with no active try is a deterministic fatal error. */
static CPLUS_TLS CPlusExceptionFrame *cplus_exception_current = NULL;

/* Per-thread print formatting state.  Every print(...) invocation owns one
 * buffer; nesting (a value-producing helper that prints itself) uses a stack
 * of slots.  All of it is thread-local so concurrent print() calls can never
 * realloc a buffer another thread is writing to or flushing (issue #72). */
#define CPLUS_PRINT_MAX_DEPTH 64
static CPLUS_TLS char *cplus_print_buf[CPLUS_PRINT_MAX_DEPTH];
static CPLUS_TLS size_t cplus_print_cap[CPLUS_PRINT_MAX_DEPTH];
static CPLUS_TLS size_t cplus_print_len[CPLUS_PRINT_MAX_DEPTH];
static CPLUS_TLS int cplus_print_first[CPLUS_PRINT_MAX_DEPTH];
static CPLUS_TLS int cplus_print_depth = 0;

/* Snapshot of the print nesting depth at each active try frame.  A throw
 * longjmps past cplus_print_end(), so without this the depth would leak and
 * later prints would write into a stale slot.  This mirror stack is internal:
 * generated try/catch lowering declares CPlusExceptionFrame itself (sometimes
 * without including cplus/cplus.h), so its public layout is left unchanged. */
typedef struct cplus_exc_print_state {
  CPlusExceptionFrame *frame;
  int print_depth;
  struct cplus_exc_print_state *prev;
} cplus_exc_print_state;
static CPLUS_TLS cplus_exc_print_state *cplus_exc_print_stack = NULL;

static void cplus_exc_print_enter(CPlusExceptionFrame *frame) {
  cplus_exc_print_state *s = (cplus_exc_print_state *)malloc(sizeof(*s));
  if (!s)
    cplus_print_fatal(CPLUS_EXIT_RESOURCE, "out of memory", NULL, 0,
                      "cannot save print state for try frame");
  s->frame = frame;
  s->print_depth = cplus_print_depth;
  s->prev = cplus_exc_print_stack;
  cplus_exc_print_stack = s;
}

static void cplus_exc_print_leave(void) {
  cplus_exc_print_state *s = cplus_exc_print_stack;
  if (!s)
    return;
  cplus_exc_print_stack = s->prev;
  cplus_print_depth = s->print_depth;
  free(s);
}

void cplus_exception_enter(CPlusExceptionFrame *frame) {
  if (!frame)
    return;
  frame->prev = cplus_exception_current;
  cplus_exception_current = frame;
  cplus_exc_print_enter(frame);
}

void cplus_exception_pop(void) {
  if (cplus_exception_current) {
    cplus_exception_current = cplus_exception_current->prev;
    cplus_exc_print_leave();
  }
}

void cplus_exception_raise(int code, const char *message, const char *file,
                           int line) {
  if (!cplus_exception_current)
    cplus_print_fatal(CPLUS_EXIT_UNSUPPORTED, "unhandled C+ exception", file,
                      line, "throw outside a try block");
  CPlusExceptionFrame *active = cplus_exception_current;
  cplus_exception_current = active->prev;
  /* Unwind print nesting to the try frame, so a throw from inside print(...)
   * cannot leave a leaked depth slot behind (issue #72). */
  cplus_exc_print_leave();
  active->exc.code = code;
  active->exc.message = message;
  active->exc.file = file;
  active->exc.line = line;
  longjmp(active->env, 1);
}

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

/* CPLUS_FORCE_NO_THREAD_BACKEND is a test-only switch: it compiles the same
 * runtime with no thread backend so the non-POSIX fallback can be verified to
 * fail loudly instead of silently returning NULL (issue #75). */
#if !defined(CPLUS_FORCE_NO_THREAD_BACKEND) &&                                  \
    (defined(__unix__) || defined(__APPLE__))
#include <pthread.h>
#include <unistd.h>
#define CPLUS_POSIX 1
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

/* Usable size of a malloc block.  Used only to *clamp* a caller-supplied
 * registration extent so that a registry size can never authorize a copy or a
 * length query past the real allocation.  Returns 0 on platforms without a
 * query or when the pointer is not a malloc block (portable fallback: keep the
 * caller's value). */
#if defined(__APPLE__)
#include <malloc/malloc.h>
static size_t cplus_malloc_capacity(const void *p) {
  return p ? (size_t)malloc_size(p) : 0u;
}
#define CPLUS_HAVE_MALLOC_CAPACITY 1
#elif defined(__linux__)
#include <malloc.h>
static size_t cplus_malloc_capacity(const void *p) {
  return p ? (size_t)malloc_usable_size((void *)p) : 0u;
}
#define CPLUS_HAVE_MALLOC_CAPACITY 1
#else
static size_t cplus_malloc_capacity(const void *p) {
  (void)p;
  return 0u;
}
#define CPLUS_HAVE_MALLOC_CAPACITY 0
#endif

/* Never let a caller-supplied size exceed the block the allocator actually
 * handed out.  A smaller declared size is kept verbatim. */
static size_t cplus_clamp_registered_size(const void *p, size_t bytes) {
  size_t usable = cplus_malloc_capacity(p);
  if (usable != 0u && bytes > usable)
    return usable;
  return bytes;
}

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
  if (isSigned) {
    if (b == 0)
      cplus_arith_fatal("division by zero",
                        remainder ? "remainder by zero" : "division by zero");
    if (bits > 1 && bits <= 64 && b == -1 &&
        a == (int64_t)(-((__int128)1 << (bits - 1))))
      cplus_arith_fatal("signed integer overflow",
                        remainder ? "remainder overflows the source type"
                                  : "division overflows the source type");
    return remainder ? a % b : a / b;
  }

  /* Unsigned operands arrive as int64_t bit patterns.  Doing the division
   * on those C types would perform signed idiv: values with bit 63 set would
   * be corrupted and INT64_MIN / UINT64_MAX would trap with SIGFPE. */
  uint64_t ua = (uint64_t)a;
  uint64_t ub = (uint64_t)b;
  if (bits > 0 && bits < 64) {
    uint64_t mask = (((uint64_t)1) << bits) - 1;
    ua &= mask;
    ub &= mask;
  }
  if (ub == 0)
    cplus_arith_fatal("division by zero",
                      remainder ? "remainder by zero" : "division by zero");
  uint64_t result = remainder ? ua % ub : ua / ub;
  return (int64_t)result;
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
 * negative left operand and any bit that would reach or cross the sign bit,
 * matching Clang's EmitShl predicates. */
static int64_t cplus_checked_shift(int64_t a, int64_t b, unsigned bits,
                                   unsigned isSigned, int left) {
  uint64_t ua = (uint64_t)a;
  uint64_t count = (uint64_t)b;
  if (bits == 0 || bits > 64) {
    /* Keep the historical wrapping result for an invalid width, but never
     * invoke undefined behaviour by shifting by 64 or more. */
    if (count >= 64)
      return 0;
    return left ? (int64_t)(ua << count)
                : (isSigned ? (int64_t)(a >> (unsigned)count)
                            : (int64_t)(ua >> count));
  }
  if (b < 0 || count >= bits) {
    char buf[160];
    snprintf(buf, sizeof(buf), "shift count %lld is outside [0, %u)",
             (long long)b, bits);
    cplus_arith_fatal("invalid shift", buf);
  }
  if (!isSigned && bits < 64)
    ua &= (((uint64_t)1) << bits) - 1;
  if (left && isSigned) {
    if (a < 0)
      cplus_arith_fatal("invalid shift",
                        "left shift of a negative signed value");
    /* The largest representable result is 2^(bits-1)-1.  Shifting in
     * __int128 keeps the bound and the comparison exact for bits == 64,
     * where a uint64_t mask would hide the sign bit entirely; count is
     * always < bits <= 64, so neither shift is undefined. */
    __int128 max = (((__int128)1) << (bits - 1)) - 1;
    if ((__int128)a > (max >> count))
      cplus_arith_fatal("invalid shift",
                        "left shift reaches the sign bit of the source type");
  }
  return left ? (int64_t)(ua << count)
              : (isSigned ? (int64_t)(a >> (unsigned)count)
                          : (int64_t)(ua >> count));
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
  __int128 src;
  if (ss && sb > 0) {
    /* A signed source arrives sign-extended to 64 bits.  For a full-width
     * source, conversion through int64_t is what sign-extends bit 63 into
     * __int128; a plain (__int128)value would zero-extend it instead. */
    if (sb >= 64)
      src = (__int128)(int64_t)value;
    else if ((value >> (sb - 1)) & 1u)
      src = (__int128)(int64_t)(value | (~((uint64_t)0) << sb));
    else
      src = (__int128)value;
  } else {
    src = (__int128)value;
  }
  uint64_t mask = db >= 64 ? ~((uint64_t)0)
                           : ((((uint64_t)1) << db) - 1);
  uint64_t truncated = value & mask;
  __int128 dst;
  if (ds && db >= 64)
    /* Same sign-extension rule for a full-width signed destination. */
    dst = (__int128)(int64_t)truncated;
  else if (ds && db > 0 && ((truncated >> (db - 1)) & 1u))
    dst = (__int128)(int64_t)(truncated | (~((uint64_t)0) << db));
  else
    dst = (__int128)truncated;
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

/* Floating-point -> integer conversion.  The compiler emits an inline
 * range/NAN predicate and calls this helper only on the failure path, so the
 * hot path needs no cross-TU call.  The full predicate is repeated here so
 * the symbol is also correct when called directly.  No __int128 is needed:
 * every bound is an exactly representable double power of two. */
int64_t __cplus_checked_f2i_i64(double value, unsigned bits, unsigned isSigned) {
  double lo, hi;
  char buf[192];

  if (bits == 0 || bits > 64) {
    bits = 64;
    isSigned = 1;
  }
  if (isSigned) {
    if (bits == 64) {
      lo = -9223372036854775808.0;
      hi = 9223372036854775808.0;
    } else {
      int b = (int)bits, i;
      double p = 1.0;
      for (i = 0; i < b - 1; ++i)
        p *= 2.0;
      lo = -p;
      hi = p;
    }
  } else {
    if (bits == 64) {
      lo = 0.0;
      hi = 18446744073709551616.0;
    } else {
      int b = (int)bits, i;
      double p = 1.0;
      for (i = 0; i < b; ++i)
        p *= 2.0;
      lo = 0.0;
      hi = p;
    }
  }

  if (!isnan(value) && value >= lo && value < hi) {
    if (!isSigned && bits == 64)
      return (int64_t)(uint64_t)value; /* preserve the 64-bit bit pattern */
    return (int64_t)value;
  }
  snprintf(buf, sizeof(buf),
           "floating-point value does not fit in a %u-bit %s integer", bits,
           isSigned ? "signed" : "unsigned");
  cplus_print_fatal(CPLUS_EXIT_CONVERSION, "floating-point conversion", NULL, 0,
                    buf);
  return 0;
}

/* Allocation-extent multiplication.  size_t overflow silently under-allocates
 * the object, so the check runs before the allocator is called. */
size_t __cplus_checked_size_mul(size_t a, size_t b) {
  if (b != 0 && a > ((size_t)-1) / b) {
    char buf[160];
    snprintf(buf, sizeof(buf),
             "allocation size %lu * %lu overflows size_t",
             (unsigned long)a, (unsigned long)b);
    cplus_print_fatal(CPLUS_EXIT_ARITHMETIC, "allocation size overflow", NULL,
                      0, buf);
  }
  return a * b;
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
 * Temporal (generation) model
 * ---------------------------
 * Every registration gets a monotonically increasing generation stored in the
 * side table.  A direct-mapped recent-free cache records the (address,
 * generation) pair of each retired allocation, and a persistent retired-address
 * ledger records the generation under which each address was last retired.
 *
 * The ledger is the important part: the recent cache and the quarantine ring
 * are bounded and evict, but the ledger does not.  A release of an address that
 * is no longer live is therefore a deterministic double free (104) even long
 * after the recent cache has been overwritten, and a stale liveness query is a
 * deterministic dangling-pointer failure (105).  A live-table hit is still a
 * legitimate release, so ordinary alloc/free churn produces no false positives.
 *
 * OPEN LIMIT (F2 - stale free at a REUSED address; no pointer ABI change):
 *   Ordinary C pointers carry no generation token.  If allocation A at address
 *   X is freed and malloc later hands X out again as live allocation B, a
 *   stale integer alias of A passed to cplus_release/cplus_free_registered is
 *   bit-identical to a genuine pointer to B: cplus_find_live(X) finds B's live
 *   slot and the release is accepted.  The retired ledger cannot help because
 *   the address is legitimately live again, and rejecting it would produce
 *   false positives on every normal reuse.
 *
 *   An earlier attempt to close F2 by refusing to re-register retired
 *   addresses (malloc retry loops plus a quarantine of rejected blocks) was
 *   reverted: on macOS allocator reuse it cost ~31x on alloc_churn while still
 *   not being sound once the bounded ring wrapped.  Closing F2 requires an ABI
 *   change (a compiler-supplied provenance token or a fat pointer).  Ordinary
 *   dereferences are not instrumented by this runtime either.
 *
 * Synchronization / open limits (wave 3)
 * ---------------------------------------
 *   Every read/write of the live table, the retired ledger, the recent cache,
 *   the quarantine ring and the generation/counter globals is serialized by
 *   cplus_alloc_mutex, so concurrent operations on *different* pointers have
 *   no data races.  Each public entry point takes the lock for exactly one
 *   critical section; helpers named below are called only with it held.
 *   Operating on the *same* pointer concurrently (e.g. releasing while a
 *   realloc of that pointer is in flight) is a caller-level race, exactly as
 *   it is for malloc/free; the side table cannot make that sequence atomic.
 *
 *   Interior pointers: the side table is keyed on base addresses only.  A free
 *   or liveness query through base+k cannot match the live slot for base, so
 *   it is a deterministic 105/type-confusion failure, not a silent success.
 *   The recorded size is used for copy bounds and the length query, not as a
 *   per-byte ownership map; proving interior ownership for every address
 *   would need a different (ABI-changing) representation.
 *
 *   Caller-supplied sizes are clamped to the block's real usable size where
 *   the platform exposes one (malloc_size / malloc_usable_size), so a false
 *   size cannot authorize an overread.  On platforms without that query the
 *   caller's value is kept (portable fallback).
 *
 *   Re-registration of a stale retired address is rejected deterministically
 *   (105): accepting it would re-arm a genuine second free of an already
 *   released block.  A retired address is stale when it is still in quarantine
 *   or the platform allocator query reports no live block there; a proven
 *   fresh malloc of the same address is accepted.  Fresh managed reuse is
 *   unaffected because it goes through cplus_alloc / in-place realloc.
 *
 *   Retired-ledger cap (CPLUS_RETIRED_MAX, 1048576): once full, a one-time
 *   stderr warning is printed, cplus_retired_wrapped becomes sticky and
 *   cplus_retired_ledger_full() reports it.  New retirements then evict one
 *   slot round-robin instead of being dropped, so the address retired *now*
 *   stays detectable as 104.  The evicted address loses ledger coverage
 *   (cplus_address_retired() honestly returns 0 for it): that is a bounded
 *   resource limit, not a silent correctness claim.
 * ------------------------------------------------------------------------- */

#define CPLUS_LIVE_INITIAL 64u
#define CPLUS_RECENT_SLOTS 4096u
#define CPLUS_RETIRED_INITIAL 256u
/* Upper bound on retired ledger entries.  Beyond this we evict round-robin
 * (never drop a retirement), set cplus_retired_wrapped (sticky) and print a
 * one-time warning; the evicted address loses ledger coverage.  The cap is a
 * fixed 1,048,576 addresses (documented above). */
#define CPLUS_RETIRED_MAX 1048576u
#define CPLUS_TOMBSTONE ((void *)(uintptr_t)-1)

typedef struct cplus_live_slot {
  void *ptr;
  size_t size;
  uint64_t generation;
} cplus_live_slot;

typedef struct cplus_recent_slot {
  void *ptr;
  uint64_t generation;
} cplus_recent_slot;

typedef struct cplus_retired_slot {
  void *ptr;
  uint64_t generation;
} cplus_retired_slot;

/* Generation zero is reserved for "none"; the counter starts at one. */
static uint64_t cplus_generation = 1u;

static cplus_live_slot *cplus_live = NULL;
static size_t cplus_live_cap = 0;
static size_t cplus_live_count = 0;
static size_t cplus_live_deleted = 0;
static cplus_recent_slot cplus_recent[CPLUS_RECENT_SLOTS];

static cplus_retired_slot *cplus_retired = NULL;
static size_t cplus_retired_cap = 0;
static size_t cplus_retired_count = 0;
static size_t cplus_retired_deleted = 0;
static int cplus_retired_wrapped = 0;
/* Once the ledger is full, new retirements evict this slot round-robin so
 * double-free detection keeps working instead of silently degrading to 105. */
static size_t cplus_retired_evict_cursor = 0;

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

/* Called with the allocator lock held.  If a pointer is (re)registered, any
 * stale quarantine entry for that address must go away: ring eviction would
 * otherwise free a block that is live again.  O(ring slots), registration
 * path only - never on the cplus_alloc/cplus_release hot path. */
static void cplus_quarantine_drop(const void *p) {
  for (size_t k = 0; k < CPLUS_QUARANTINE_SLOTS; ++k) {
    if (cplus_quarantine[k] == p) {
      cplus_quarantine[k] = NULL;
      if (cplus_quarantine_count > 0u)
        --cplus_quarantine_count;
      return;
    }
  }
}

static int cplus_quarantine_contains(const void *p) {
  for (size_t k = 0; k < CPLUS_QUARANTINE_SLOTS; ++k)
    if (cplus_quarantine[k] == p)
      return 1;
  return 0;
}

static uint64_t cplus_retired_generation(const void *p); /* defined below */

/* Called with the allocator lock held.  A retired address is a stale alias
 * unless the allocator proves a *new* block currently lives there.  macOS
 * malloc_size()/glibc malloc_usable_size() return 0 for a freed block and a
 * positive size for a live one, which distinguishes S1/S9 (stale alias)
 * from a legitimate fresh malloc that reuses an address.  A block still held
 * in our quarantine has not been returned to malloc yet and is always stale.
 * Without a platform query we reject conservatively. */
static int cplus_retired_address_stale(const void *p) {
  if (cplus_retired_generation(p) == 0u)
    return 0;
  if (cplus_quarantine_contains(p))
    return 1;
#if CPLUS_HAVE_MALLOC_CAPACITY
  return cplus_malloc_capacity(p) == 0u;
#else
  return 1;
#endif
}

#if defined(CPLUS_FORCE_NO_THREAD_BACKEND)
/* Test-only path: no threads exist, so the side table needs no locking.  The
 * exported mutex/TLS primitives still fail loudly below. */
static void cplus_alloc_lock(void) {}
static void cplus_alloc_unlock(void) {}
#elif defined(CPLUS_POSIX)
static pthread_mutex_t cplus_alloc_mutex = PTHREAD_MUTEX_INITIALIZER;
static void cplus_alloc_lock(void) { pthread_mutex_lock(&cplus_alloc_mutex); }
static void cplus_alloc_unlock(void) {
  pthread_mutex_unlock(&cplus_alloc_mutex);
}

/* fork() in a multi-threaded process inherits only the calling thread.  If
 * that fork happens while another thread holds cplus_alloc_mutex, the child
 * would inherit a locked mutex with no thread able to unlock it, and the
 * first managed-memory call in the child would deadlock.  The standard
 * prepare/parent/child handshake makes the snapshot consistent: prepare
 * waits for the side-table lock, parent releases it, and the child
 * reinitializes it (safe because the child has exactly one thread).  These
 * are internal static hooks; no exported symbol or ABI changes. */
static void cplus_atfork_prepare(void) { cplus_alloc_lock(); }
static void cplus_atfork_parent(void) { cplus_alloc_unlock(); }
static void cplus_atfork_child(void) {
  pthread_mutex_init(&cplus_alloc_mutex, NULL);
}
__attribute__((constructor)) static void cplus_atfork_install(void) {
  pthread_atfork(cplus_atfork_prepare, cplus_atfork_parent, cplus_atfork_child);
}
#elif defined(_WIN32)
/* Win32 has real threads: the side table must be serialized here too, not
 * just in the exported mutex API.  A statically initialized SRWLOCK needs no
 * one-time init and works before main() runs. */
static SRWLOCK cplus_alloc_srw = SRWLOCK_INIT;
static void cplus_alloc_lock(void) { AcquireSRWLockExclusive(&cplus_alloc_srw); }
static void cplus_alloc_unlock(void) {
  ReleaseSRWLockExclusive(&cplus_alloc_srw);
}
#else
/* No thread backend at all: single-threaded programs are still usable. */
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

/* Called with the allocator lock held.  Never returns zero. */
static uint64_t cplus_next_generation(void) {
  if (cplus_generation == UINT64_MAX)
    cplus_generation = 1u; /* wrap only after 2^64 registrations */
  return cplus_generation++;
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

static void cplus_live_insert(const void *p, size_t size, uint64_t generation) {
  size_t mask = cplus_live_cap - 1;
  size_t i = cplus_hash(p) & mask;
  while (cplus_live[i].ptr != NULL && cplus_live[i].ptr != CPLUS_TOMBSTONE)
    i = (i + 1) & mask;
  if (cplus_live[i].ptr == CPLUS_TOMBSTONE)
    --cplus_live_deleted;
  cplus_live[i].ptr = (void *)p;
  cplus_live[i].size = size;
  cplus_live[i].generation = generation;
  ++cplus_live_count;
}

/* Persistent retired-address ledger --------------------------------------- */

static size_t cplus_retired_index(const void *p) {
  return (size_t)(cplus_hash(p) & (cplus_retired_cap - 1u));
}

/* Called with the allocator lock held. */
static cplus_retired_slot *cplus_find_retired(const void *p) {
  if (cplus_retired_cap == 0)
    return NULL;
  size_t mask = cplus_retired_cap - 1;
  size_t i = cplus_retired_index(p);
  for (size_t probe = 0; probe <= mask; ++probe) {
    void *slot = cplus_retired[i].ptr;
    if (slot == p)
      return &cplus_retired[i];
    if (slot == NULL)
      return NULL;
    i = (i + 1) & mask;
  }
  return NULL;
}

/* Called with the allocator lock held. */
static uint64_t cplus_retired_generation(const void *p) {
  cplus_retired_slot *slot = cplus_find_retired(p);
  return slot ? slot->generation : 0u;
}

/* Called with the allocator lock held. */
static void cplus_retired_grow(void) {
  size_t cap = cplus_retired_cap ? cplus_retired_cap * 2u
                                 : CPLUS_RETIRED_INITIAL;
  /* Cap the *entry count* at CPLUS_RETIRED_MAX, but keep the hash table at
   * twice that size.  A completely full open-addressed table makes every
   * unsuccessful lookup O(cap); the 2x slack keeps lookups short and leaves
   * room for eviction tombstones. */
  if (cap > CPLUS_RETIRED_MAX * 2u)
    cap = CPLUS_RETIRED_MAX * 2u;
  cplus_retired_slot *fresh =
      (cplus_retired_slot *)calloc(cap, sizeof(*fresh));
  if (!fresh) {
    cplus_print_fatal(CPLUS_EXIT_RESOURCE, "out of memory", NULL, 0,
                      "retired-address ledger failed");
  }
  for (size_t k = 0; k < cplus_retired_cap; ++k) {
    void *p = cplus_retired[k].ptr;
    if (p != NULL && p != CPLUS_TOMBSTONE) {
      size_t mask = cap - 1u;
      size_t i = (size_t)(cplus_hash(p) & mask);
      while (fresh[i].ptr != NULL)
        i = (i + 1) & mask;
      fresh[i] = cplus_retired[k];
    }
  }
  free(cplus_retired);
  cplus_retired = fresh;
  cplus_retired_cap = cap;
  cplus_retired_deleted = 0;
}

/* Called with the allocator lock held.  Records the generation under which an
 * address was retired.  A repeated retirement updates the recorded generation;
 * the address may legitimately be reused by a later allocation. */
static void cplus_retired_insert(const void *p, uint64_t generation) {
  cplus_retired_slot *slot = cplus_find_retired(p);
  if (slot) {
    slot->generation = generation;
    return;
  }
  if (cplus_retired_count + cplus_retired_deleted + 1u >= CPLUS_RETIRED_MAX) {
    if (!cplus_retired_wrapped) {
      cplus_retired_wrapped = 1;
      fprintf(stderr,
              "C+ runtime warning: retired-address ledger is full (%u "
              "entries); further retirements evict entries round-robin\n",
              (unsigned)CPLUS_RETIRED_MAX);
      fflush(stderr);
    }
    /* Bounded ledger: evict one occupied slot round-robin rather than drop
     * the retirement.  The victim becomes a tombstone (so other probe chains
     * stay intact) and p is inserted on its own hash probe sequence, which
     * keeps every entry findable.  The evicted address loses ledger coverage
     * (cplus_address_retired() honestly reports 0 for it), but the address
     * retired *now* remains detectable as a double free (104). */
    /* Tombstones from many evictions would eventually fill the table again;
     * rebuild it (dropping tombstones, resetting deleted) when they get high. */
    if (cplus_retired_deleted > cplus_retired_cap / 4u)
      cplus_retired_grow();
    size_t mask = cplus_retired_cap - 1u;
    size_t victim = cplus_retired_evict_cursor++ & mask;
    for (size_t probe = 0; probe <= mask; ++probe) {
      void *q = cplus_retired[victim].ptr;
      if (q != NULL && q != CPLUS_TOMBSTONE && q != p)
        break;
      victim = (victim + 1) & mask;
    }
    void *vq = cplus_retired[victim].ptr;
    if (vq != NULL && vq != CPLUS_TOMBSTONE) {
      cplus_retired[victim].ptr = CPLUS_TOMBSTONE;
      --cplus_retired_count;
      ++cplus_retired_deleted;
    }
    size_t i = cplus_retired_index(p);
    while (cplus_retired[i].ptr != NULL &&
           cplus_retired[i].ptr != CPLUS_TOMBSTONE)
      i = (i + 1) & mask;
    if (cplus_retired[i].ptr == CPLUS_TOMBSTONE)
      --cplus_retired_deleted;
    cplus_retired[i].ptr = (void *)p;
    cplus_retired[i].generation = generation;
    ++cplus_retired_count;
    return;
  }
  if (cplus_retired_cap < CPLUS_RETIRED_MAX * 2u &&
      (cplus_retired_count + cplus_retired_deleted + 1u) * 4u >
          cplus_retired_cap * 3u)
    cplus_retired_grow();
  size_t mask = cplus_retired_cap - 1u;
  size_t i = cplus_retired_index(p);
  while (cplus_retired[i].ptr != NULL &&
         cplus_retired[i].ptr != CPLUS_TOMBSTONE)
    i = (i + 1) & mask;
  if (cplus_retired[i].ptr == CPLUS_TOMBSTONE)
    --cplus_retired_deleted;
  cplus_retired[i].ptr = (void *)p;
  cplus_retired[i].generation = generation;
  ++cplus_retired_count;
}

/* Called with the allocator lock held.  Only grow when the *live* set needs
 * the room.  Tombstone churn (allocate/free one object repeatedly) used to
 * double the table every time the load threshold was crossed, growing without
 * bound until calloc() failed (issue #73).  When the live set is still below
 * half the table, rehash into the SAME capacity instead: that drops every
 * tombstone in O(cap) and keeps memory usage constant. */
static void cplus_live_grow(void) {
  size_t cap = cplus_live_cap;
  if (cplus_live_cap == 0) {
    cap = CPLUS_LIVE_INITIAL;
  } else if (cplus_live_count * 2u >= cplus_live_cap) {
    /* Genuine live-set growth.  Guard the multiplication so a corrupt or
     * enormous table cannot wrap size_t and calloc a too-small buffer. */
    if (cplus_live_cap > SIZE_MAX / 2u)
      cplus_print_fatal(CPLUS_EXIT_RESOURCE, "out of memory", NULL, 0,
                        "allocation bookkeeping capacity overflow");
    cap = cplus_live_cap * 2u;
  }
  /* else: cap == cplus_live_cap; rehash in place to purge tombstones. */
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

void *cplus_register_allocation(void *p, size_t bytes, const char *file,
                                int line) {
  if (!p)
    return p;
  cplus_alloc_lock();
  cplus_live_slot *existing = cplus_find_live(p);
  if (existing) {
    cplus_alloc_unlock();
    cplus_print_fatal(CPLUS_EXIT_DOUBLE_FREE, "allocation registration", file,
                      line, "pointer is already a live C+ allocation");
  }
  /* S1/S9: a stale integer alias of an already-retired block would re-arm a
   * real second free.  Reject a retired address unless the allocator proves
   * a fresh block now lives at that address (legitimate malloc reuse). */
  if (cplus_retired_address_stale(p)) {
    cplus_alloc_unlock();
    cplus_print_fatal(CPLUS_EXIT_TYPE_CONFUSION, "invalid free", file, line,
                      "retired address cannot be re-registered");
  }
  bytes = cplus_clamp_registered_size(p, bytes);
  cplus_quarantine_drop(p);
  uint64_t gen = cplus_next_generation();
  if (cplus_live_count + cplus_live_deleted + 1 > cplus_live_cap * 3 / 4)
    cplus_live_grow();
  cplus_live_insert(p, bytes, gen);
  cplus_alloc_unlock();
  return p;
}

void *cplus_realloc_registered(void *oldp, void *newp, size_t bytes,
                               const char *file, int line) {
  if (!newp)
    return newp; /* realloc failed: the old block is still live */
  cplus_alloc_lock();
  /* A transfer target that is already live as a different allocation must be
   * rejected before touching the old slot.  Inserting a second live entry for
   * the same address would leave a stale live entry after one of the two is
   * released (a caller-supplied registration/size mismatch).  newp == oldp is
   * the legitimate in-place realloc case and is allowed.  Note (F2): raw
   * realloc() may also return a previously retired address; an old alias at a
   * reused address remains indistinguishable from the new allocation - see
   * the file header. */
  cplus_live_slot *old = oldp ? cplus_find_live(oldp) : NULL;
  cplus_live_slot *dest = cplus_find_live(newp);
  if (dest && dest != old) {
    cplus_alloc_unlock();
    cplus_print_fatal(CPLUS_EXIT_DOUBLE_FREE, "allocation registration", file,
                      line, "pointer is already a live C+ allocation");
  }
  /* A non-live transfer target that is a stale retired alias would re-arm a
   * real double free.  The in-place case (newp == oldp, live) and a proven
   * fresh malloc reuse are legitimate and are allowed. */
  if (!dest && cplus_retired_address_stale(newp)) {
    cplus_alloc_unlock();
    cplus_print_fatal(CPLUS_EXIT_TYPE_CONFUSION, "invalid free", file, line,
                      "retired address cannot be re-registered");
  }
  bytes = cplus_clamp_registered_size(newp, bytes);
  cplus_quarantine_drop(newp);
  if (old) {
    uint64_t old_generation = old->generation;
    old->ptr = CPLUS_TOMBSTONE;
    --cplus_live_count;
    ++cplus_live_deleted;
    cplus_recent[cplus_recent_index(oldp)].ptr = oldp;
    cplus_recent[cplus_recent_index(oldp)].generation = old_generation;
    cplus_retired_insert(oldp, old_generation);
  }
  uint64_t gen = cplus_next_generation();
  if (cplus_live_count + cplus_live_deleted + 1 > cplus_live_cap * 3 / 4)
    cplus_live_grow();
  cplus_live_insert(newp, bytes, gen);
  cplus_alloc_unlock();
  return newp;
}

void cplus_free_registered(void *p, const char *file, int line) {
  if (!p)
    return;
  cplus_alloc_lock();
  /* The retired ledger is consulted on a live-table miss only.  A live hit is
   * a legitimate release of the current generation, so normal reuse of an
   * address produces no false double-free; a release of an address that is no
   * longer live is deterministic even after the recent cache evicted. */
  uint64_t retired_generation = cplus_retired_generation(p);
  cplus_live_slot *slot = cplus_find_live(p);
  if (!slot) {
    cplus_recent_slot *recent = &cplus_recent[cplus_recent_index(p)];
    int recently_freed = recent->ptr == p;
    cplus_alloc_unlock();
    if (retired_generation != 0u || recently_freed) {
      cplus_print_fatal(CPLUS_EXIT_DOUBLE_FREE, "double free", file, line,
                        "allocation released twice");
    }
    // A pointer whose allocation was produced OUTSIDE the tracked paths
    // (a helper returning malloc/strdup/realloc, a third-party library) is
    // ordinary C and must keep ordinary C behaviour. Aborting here broke
    // real unmodified C applications: the rewriter wraps `free()` for ABI
    // preservation but cannot see allocations made in another TU. Managed
    // releases (cplus_release) and tracked double frees above still trap.
    free(p);
    return;
  }
  uint64_t generation = slot->generation;
  slot->ptr = CPLUS_TOMBSTONE;
  --cplus_live_count;
  ++cplus_live_deleted;
  cplus_recent[cplus_recent_index(p)].ptr = p;
  cplus_recent[cplus_recent_index(p)].generation = generation;
  cplus_retired_insert(p, generation);
  /* Free while the side-table lock is still held.  Releasing it first lets a
   * concurrent registration insert the same address into the live table
   * before the free lands, leaving a stale live entry and a real double free. */
  free(p);
  cplus_alloc_unlock();
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
  /* The same two guards as cplus_register_allocation(): a malloc result must
   * never already be live (a second slot would survive one release), and a
   * stale quarantine entry for this address must be dropped so ring eviction
   * cannot free a block that is live again (issue #76). */
  if (cplus_find_live(p)) {
    cplus_alloc_unlock();
    cplus_print_fatal(CPLUS_EXIT_DOUBLE_FREE, "allocation registration", NULL,
                      0, "allocator returned an already-live address");
  }
  cplus_quarantine_drop(p);
  uint64_t gen = cplus_next_generation();
  if (cplus_live_count + cplus_live_deleted + 1 > cplus_live_cap * 3 / 4)
    cplus_live_grow();
  cplus_live_insert(p, size, gen);
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
  /* Validate the transfer source BEFORE allocating anything.  The old code
   * called cplus_alloc() first and only then discovered an invalid ptr,
   * leaking the fresh block and consuming a generation on the trap path
   * (issues #76/#110).  The entire lookup/copy/retire/insert/free sequence
   * now runs in one critical section, so a concurrent release cannot free
   * ptr while the copy is in flight or observe a half-retired live slot,
   * and no second thread can double-retire ptr in the memcpy/free window.
   * The plain pointer signature is unchanged and errno keeps its malloc
   * semantics: untouched on success, ENOMEM from malloc() on failure. */
  cplus_alloc_lock();
  cplus_live_slot *slot = cplus_find_live(ptr);
  if (!slot) {
    uint64_t retired = cplus_retired_generation(ptr);
    cplus_alloc_unlock();
    cplus_print_fatal(retired ? CPLUS_EXIT_DOUBLE_FREE
                              : CPLUS_EXIT_TYPE_CONFUSION,
                      "invalid free", NULL, 0,
                      "realloc target is not a live C+ allocation");
  }
  void *fresh = malloc(size);
  if (!fresh) {
    cplus_alloc_unlock();
    cplus_print_fatal(CPLUS_EXIT_RESOURCE, "out of memory", NULL, 0,
                      "managed reallocation failed");
  }
  size_t n = size < slot->size ? size : slot->size;
  /* Belt-and-braces: even a wrongly registered or stale extent must never
   * authorize a copy past the block malloc actually handed out. */
  size_t usable = cplus_malloc_capacity(ptr);
  if (usable != 0u && n > usable)
    n = usable;
  memcpy(fresh, ptr, n);

  /* Retire the old slot with its own generation.  Do this before any table
   * rebuild (which would invalidate `slot`) and never touch `slot` again. */
  uint64_t old_generation = slot->generation;
  slot->ptr = CPLUS_TOMBSTONE;
  --cplus_live_count;
  ++cplus_live_deleted;
  cplus_recent[cplus_recent_index(ptr)].ptr = ptr;
  cplus_recent[cplus_recent_index(ptr)].generation = old_generation;
  cplus_retired_insert(ptr, old_generation);

  if (cplus_find_live(fresh)) {
    free(fresh);
    cplus_alloc_unlock();
    cplus_print_fatal(CPLUS_EXIT_DOUBLE_FREE, "allocation registration", NULL,
                      0, "allocator returned an already-live address");
  }
  cplus_quarantine_drop(fresh);
  uint64_t gen = cplus_next_generation();
  if (cplus_live_count + cplus_live_deleted + 1 > cplus_live_cap * 3 / 4)
    cplus_live_grow();
  cplus_live_insert(fresh, size, gen);

  /* Mirror cplus_release(): when quarantine is enabled the old block is
   * retained for a bounded time instead of being returned to malloc. */
  if (cplus_quarantine_enabled()) {
    void *oldest = cplus_quarantine[cplus_quarantine_next];
    cplus_quarantine[cplus_quarantine_next] = ptr;
    cplus_quarantine_next =
        (cplus_quarantine_next + 1) % CPLUS_QUARANTINE_SLOTS;
    if (cplus_quarantine_count < CPLUS_QUARANTINE_SLOTS)
      ++cplus_quarantine_count;
    if (oldest)
      free(oldest);
  } else {
    free(ptr);
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
  cplus_log(CPLUS_LOG_DEBUG, "cplus_release ptr=%p", ptr);

  cplus_alloc_lock();
  /* Retired-ledger hit on a live-table miss is authoritative and survives
   * cache eviction; a live hit is always a legitimate release. */
  uint64_t retired_generation = cplus_retired_generation(ptr);
  cplus_live_slot *slot = cplus_find_live(ptr);
  if (!slot) {
    cplus_recent_slot *recent = &cplus_recent[cplus_recent_index(ptr)];
    int recently_freed = recent->ptr == ptr;
    cplus_alloc_unlock();
    if (retired_generation != 0u || recently_freed)
      cplus_print_fatal(CPLUS_EXIT_DOUBLE_FREE, "double free", NULL, 0,
                        "managed memory released twice");
    cplus_print_fatal(CPLUS_EXIT_TYPE_CONFUSION, "invalid free", NULL, 0,
                      "pointer was not produced by the C+ allocator");
  }
  uint64_t generation = slot->generation;
  slot->ptr = CPLUS_TOMBSTONE;
  --cplus_live_count;
  ++cplus_live_deleted;
  cplus_recent[cplus_recent_index(ptr)].ptr = ptr;
  cplus_recent[cplus_recent_index(ptr)].generation = generation;
  cplus_retired_insert(ptr, generation);
  if (cplus_quarantine_enabled()) {
    void *oldest = cplus_quarantine[cplus_quarantine_next];
    cplus_quarantine[cplus_quarantine_next] = ptr;
    cplus_quarantine_next = (cplus_quarantine_next + 1) %
                            CPLUS_QUARANTINE_SLOTS;
    if (cplus_quarantine_count < CPLUS_QUARANTINE_SLOTS)
      ++cplus_quarantine_count;
    if (oldest)
      free(oldest); /* bounded: at most CPLUS_QUARANTINE_SLOTS delayed frees */
    cplus_alloc_unlock();
    return;
  }
  /* Keep the lock across free(): see cplus_free_registered above. */
  free(ptr);
  cplus_alloc_unlock();
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

/* Additive generation introspection.  These do not change any existing ABI. */

uint64_t cplus_allocation_generation(const void *ptr) {
  if (!ptr)
    return 0u;
  cplus_alloc_lock();
  cplus_live_slot *slot = cplus_find_live(ptr);
  uint64_t gen = slot ? slot->generation : 0u;
  cplus_alloc_unlock();
  return gen;
}

uint64_t cplus_epoch(void) {
  cplus_alloc_lock();
  uint64_t epoch = cplus_generation;
  cplus_alloc_unlock();
  return epoch;
}

int cplus_address_retired(const void *ptr) {
  if (!ptr)
    return 0;
  cplus_alloc_lock();
  int retired = cplus_retired_generation(ptr) != 0u;
  cplus_alloc_unlock();
  return retired;
}

/* Additive introspection for the live-address table.  Lets a regression test
 * verify that tombstone churn rehashes in place rather than growing the table
 * without bound (issue #73).  Any argument may be NULL. */
void cplus_live_table_stats(size_t *capacity, size_t *live,
                            size_t *tombstones) {
  cplus_alloc_lock();
  if (capacity)
    *capacity = cplus_live_cap;
  if (live)
    *live = cplus_live_count;
  if (tombstones)
    *tombstones = cplus_live_deleted;
  cplus_alloc_unlock();
}

/* Additive introspection for the bounded retired ledger.  Once this returns
 * non-zero, the cap has been reached and further retirements evict an older
 * entry round-robin; the evicted address is then no longer covered by the
 * ledger. */
int cplus_retired_ledger_full(void) {
  cplus_alloc_lock();
  int full = cplus_retired_wrapped;
  cplus_alloc_unlock();
  return full;
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
    /* MANAGED cleanups release via cplus_release() and legitimately carry a
     * NULL drop callback; requiring c->drop skipped them entirely, hiding a
     * double-managed release (EDGE100 runtime finding). */
    if (c->ptr && (c->drop || c->kind == CPLUS_CLEANUP_MANAGED)) {
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
#elif defined(_WIN32)
typedef struct cplus_mutex {
  CRITICAL_SECTION cs;
} cplus_mutex;

typedef struct cplus_tls_slot {
  DWORD key;
} cplus_tls_slot;

void *cplus_mutex_new(void) {
  cplus_mutex *m = (cplus_mutex *)malloc(sizeof(cplus_mutex));
  if (!m) {
    cplus_print_fatal(CPLUS_EXIT_RESOURCE, "out of memory", NULL, 0,
                      "cannot create mutex");
  }
  InitializeCriticalSection(&m->cs);
  return m;
}

void cplus_mutex_lock(void *mp) {
  if (!mp)
    return;
  EnterCriticalSection(&((cplus_mutex *)mp)->cs);
}

void cplus_mutex_unlock(void *mp) {
  if (!mp)
    return;
  LeaveCriticalSection(&((cplus_mutex *)mp)->cs);
}

void cplus_mutex_free(void *mp) {
  if (!mp)
    return;
  DeleteCriticalSection(&((cplus_mutex *)mp)->cs);
  free(mp);
}

void *cplus_tls_new(void) {
  cplus_tls_slot *s = (cplus_tls_slot *)malloc(sizeof(cplus_tls_slot));
  if (!s) {
    cplus_print_fatal(CPLUS_EXIT_RESOURCE, "out of memory", NULL, 0,
                      "cannot create thread-local slot");
  }
  DWORD key = TlsAlloc();
  if (key == TLS_OUT_OF_INDEXES) {
    free(s);
    cplus_print_fatal(CPLUS_EXIT_RESOURCE, "TLS failure", NULL, 0,
                      "TlsAlloc failed");
  }
  s->key = key;
  return s;
}

void *cplus_tls_get(void *slotp) {
  if (!slotp)
    return NULL;
  return TlsGetValue(((cplus_tls_slot *)slotp)->key);
}

void cplus_tls_set(void *slotp, void *value) {
  if (!slotp)
    return;
  if (!TlsSetValue(((cplus_tls_slot *)slotp)->key, value)) {
    cplus_print_fatal(CPLUS_EXIT_RESOURCE, "TLS failure", NULL, 0,
                      "TlsSetValue failed");
  }
}

void cplus_tls_free(void *slotp) {
  if (!slotp)
    return;
  TlsFree(((cplus_tls_slot *)slotp)->key);
  free(slotp);
}
#else
/* No thread backend.  Never pretend locking succeeded: a silent no-op mutex
 * turns mutual exclusion into a data race with no diagnostic (issue #75). */
void *cplus_mutex_new(void) {
  cplus_print_fatal(CPLUS_EXIT_UNSUPPORTED, "unsupported thread backend", NULL,
                    0, "cplus_mutex_new requires POSIX threads or Win32");
  return NULL;
}
void cplus_mutex_lock(void *m) {
  (void)m;
  cplus_print_fatal(CPLUS_EXIT_UNSUPPORTED, "unsupported thread backend", NULL,
                    0, "cplus_mutex_lock requires POSIX threads or Win32");
}
void cplus_mutex_unlock(void *m) {
  (void)m;
  cplus_print_fatal(CPLUS_EXIT_UNSUPPORTED, "unsupported thread backend", NULL,
                    0, "cplus_mutex_unlock requires POSIX threads or Win32");
}
void cplus_mutex_free(void *m) {
  (void)m;
  cplus_print_fatal(CPLUS_EXIT_UNSUPPORTED, "unsupported thread backend", NULL,
                    0, "cplus_mutex_free requires POSIX threads or Win32");
}
void *cplus_tls_new(void) {
  cplus_print_fatal(CPLUS_EXIT_UNSUPPORTED, "unsupported thread backend", NULL,
                    0, "cplus_tls_new requires POSIX threads or Win32");
  return NULL;
}
void *cplus_tls_get(void *s) {
  (void)s;
  cplus_print_fatal(CPLUS_EXIT_UNSUPPORTED, "unsupported thread backend", NULL,
                    0, "cplus_tls_get requires POSIX threads or Win32");
  return NULL;
}
void cplus_tls_set(void *s, void *v) {
  (void)s;
  (void)v;
  cplus_print_fatal(CPLUS_EXIT_UNSUPPORTED, "unsupported thread backend", NULL,
                    0, "cplus_tls_set requires POSIX threads or Win32");
}
void cplus_tls_free(void *s) {
  (void)s;
  cplus_print_fatal(CPLUS_EXIT_UNSUPPORTED, "unsupported thread backend", NULL,
                    0, "cplus_tls_free requires POSIX threads or Win32");
}
#endif

/* -------------------------------------------------------------------------
 * Modern I/O: type-generic print/scan
 *
 * The state struct below is a C struct with functions operating on it - the
 * same shape a C+ `class` lowers to.  It remembers whether a value has
 * already been printed on the current line so print() can insert spaces
 * without a format string.
 * ------------------------------------------------------------------------- */

/* Each `print(...)` invocation owns one output buffer.  The state arrays are
 * declared thread-local near the top of this file; values are appended to the
 * current thread's buffer and flushed by cplus_print_end(), so arguments
 * evaluated while printing can never interleave with this invocation's own
 * output.  The buffer grows on demand (no silent truncation); if growth fails,
 * the pending bytes and the new chunk are written directly, which preserves
 * data at the cost of the interleaving guarantee in that out-of-memory case. */

void cplus_print_begin(void) {
  if (cplus_print_depth < CPLUS_PRINT_MAX_DEPTH) {
    int d = cplus_print_depth++;
    cplus_print_len[d] = 0;
    cplus_print_first[d] = 1;
  }
}

static void cplus_print_append(int d, const char *text, size_t n) {
  if (n == 0)
    return;
  if (cplus_print_len[d] + n + 1 > cplus_print_cap[d]) {
    size_t want = cplus_print_len[d] + n + 1;
    size_t cap = cplus_print_cap[d] ? cplus_print_cap[d] : 128;
    char *grown;
    while (cap < want) {
      if (cap > (size_t)-1 / 2) { cap = want; break; }
      cap *= 2;
    }
    grown = (char *)realloc(cplus_print_buf[d], cap);
    if (!grown) {
      /* Last-resort direct write: never drop caller data silently. */
      if (cplus_print_len[d] > 0)
        fwrite(cplus_print_buf[d], 1, cplus_print_len[d], stdout);
      cplus_print_len[d] = 0;
      fwrite(text, 1, n, stdout);
      return;
    }
    cplus_print_buf[d] = grown;
    cplus_print_cap[d] = cap;
  }
  memcpy(cplus_print_buf[d] + cplus_print_len[d], text, n);
  cplus_print_len[d] += n;
  cplus_print_buf[d][cplus_print_len[d]] = '\0';
}

void cplus_print_end(void) {
  if (cplus_print_depth > 0) {
    int d = cplus_print_depth - 1;
    size_t n = cplus_print_len[d];
    if (n > 0) {
      /* One fwrite for the whole line (including '\n'): stdio serializes each
       * call, so independent per-thread buffers cannot produce interleaved
       * partial lines.  append() always reserved room for the terminator. */
      cplus_print_buf[d][n] = '\n';
      fwrite(cplus_print_buf[d], 1, n + 1, stdout);
    } else {
      fwrite("\n", 1, 1, stdout);
    }
    cplus_print_len[d] = 0;
    cplus_print_first[d] = 1;
    --cplus_print_depth;
  }
}

void cplus_print_value(cplus_print_kind kind, ...) {
  cplus_log(CPLUS_LOG_TRACE, "cplus_print_value kind=%d", (int)kind);
  int d = cplus_print_depth > 0 ? cplus_print_depth - 1 : 0;
  va_list ap;
  va_start(ap, kind);
  if (!cplus_print_first[d])
    cplus_print_append(d, " ", 1);
  cplus_print_first[d] = 0;
  char numbuf[64];
  switch (kind) {
  case CPLUS_PRINT_INT:
    snprintf(numbuf, sizeof(numbuf), "%d", va_arg(ap, int));
    break;
  case CPLUS_PRINT_UINT:
    snprintf(numbuf, sizeof(numbuf), "%u", va_arg(ap, unsigned int));
    break;
  case CPLUS_PRINT_LONG:
    snprintf(numbuf, sizeof(numbuf), "%ld", va_arg(ap, long));
    break;
  case CPLUS_PRINT_ULONG:
    snprintf(numbuf, sizeof(numbuf), "%lu", va_arg(ap, unsigned long));
    break;
  case CPLUS_PRINT_LLONG:
    snprintf(numbuf, sizeof(numbuf), "%lld", va_arg(ap, long long));
    break;
  case CPLUS_PRINT_ULLONG:
    snprintf(numbuf, sizeof(numbuf), "%llu", va_arg(ap, unsigned long long));
    break;
  case CPLUS_PRINT_DOUBLE:
    snprintf(numbuf, sizeof(numbuf), "%g", va_arg(ap, double));
    break;
  case CPLUS_PRINT_LDOUBLE:
    snprintf(numbuf, sizeof(numbuf), "%Lg", va_arg(ap, long double));
    break;
  case CPLUS_PRINT_CHAR:
    snprintf(numbuf, sizeof(numbuf), "%c", (char)va_arg(ap, int));
    break;
  case CPLUS_PRINT_BOOL:
    snprintf(numbuf, sizeof(numbuf), "%s", va_arg(ap, int) ? "true" : "false");
    break;
  case CPLUS_PRINT_STR: {
    const char *text = va_arg(ap, const char *);
    /* Append strings directly: they can be arbitrarily long, unlike the
     * numeric scratch buffer below. */
    cplus_print_append(d, text ? text : "(null)",
                       text ? strlen(text) : 7u);
    va_end(ap);
    return;
  }
  default:
    snprintf(numbuf, sizeof(numbuf), "%s", "<unprintable>");
    break;
  }
  va_end(ap);
  cplus_print_append(d, numbuf, strlen(numbuf));
}

static const char *cplus_scan_type_name(size_t size, int is_unsigned) {
  if (size == 1)
    return is_unsigned ? "unsigned char" : "signed char";
  if (size == sizeof(short))
    return is_unsigned ? "unsigned short" : "short";
  if (size == sizeof(int))
    return is_unsigned ? "unsigned int" : "int";
  if (size == sizeof(long))
    return is_unsigned ? "unsigned long" : "long";
  if (size == sizeof(long long))
    return is_unsigned ? "unsigned long long" : "long long";
  return is_unsigned ? "unsigned integer" : "integer";
}

static void cplus_scan_signed_checked(void *dest, size_t size,
                                      const char *expr, const char *file,
                                      int line) {
  char token[128];
  char detail[384];
  int rc = scanf("%127s", token);
  char *end = NULL;
  long long v;
  long long minv = LLONG_MIN;
  long long maxv = LLONG_MAX;
  const char *target = (expr && *expr) ? expr : "variable";
  const char *tname = cplus_scan_type_name(size, 0);
  if (rc != 1) {
    snprintf(detail, sizeof(detail),
             "could not read input for '%s' (%s) - input ended unexpectedly",
             target, tname);
    cplus_print_fatal(CPLUS_EXIT_IO, "input error", file, line, detail);
  }
  errno = 0;
  v = strtoll(token, &end, 0);
  if (end == token || *end != '\0') {
    snprintf(detail, sizeof(detail),
             "cannot store '%s' into '%s' (%s) - expected a valid integer, not text/extra characters",
             token, target, tname);
    cplus_print_fatal(CPLUS_EXIT_IO, "input error", file, line, detail);
  }
  if (size == 1) {
    minv = SCHAR_MIN;
    maxv = SCHAR_MAX;
  } else if (size == sizeof(short)) {
    minv = SHRT_MIN;
    maxv = SHRT_MAX;
  } else if (size == sizeof(int)) {
    minv = INT_MIN;
    maxv = INT_MAX;
  } else if (size == sizeof(long)) {
    minv = LONG_MIN;
    maxv = LONG_MAX;
  } else if (size != sizeof(long long)) {
    snprintf(detail, sizeof(detail),
             "unsupported destination size (%zu bytes) for '%s'", size, target);
    cplus_print_fatal(CPLUS_EXIT_UNSUPPORTED, "input error", file, line, detail);
  }
  if (errno == ERANGE || v < minv || v > maxv) {
    snprintf(detail, sizeof(detail),
             "cannot store '%s' into '%s' (%s, valid range %lld..%lld) - the number is too large/small for this variable type and would overflow memory",
             token, target, tname, minv, maxv);
    cplus_print_fatal(CPLUS_EXIT_CONVERSION, "input overflow", file, line,
                      detail);
  }
  if (size == 1) {
    *(signed char *)dest = (signed char)v;
  } else if (size == sizeof(short)) {
    *(short *)dest = (short)v;
  } else if (size == sizeof(int)) {
    *(int *)dest = (int)v;
  } else if (size == sizeof(long)) {
    *(long *)dest = (long)v;
  } else {
    *(long long *)dest = v;
  }
}

static void cplus_scan_unsigned_checked(void *dest, size_t size,
                                        const char *expr, const char *file,
                                        int line) {
  char token[128];
  char detail[384];
  int rc = scanf("%127s", token);
  char *end = NULL;
  unsigned long long v;
  unsigned long long maxv = ULLONG_MAX;
  const char *target = (expr && *expr) ? expr : "variable";
  const char *tname = cplus_scan_type_name(size, 1);
  if (rc != 1) {
    snprintf(detail, sizeof(detail),
             "could not read input for '%s' (%s) - input ended unexpectedly",
             target, tname);
    cplus_print_fatal(CPLUS_EXIT_IO, "input error", file, line, detail);
  }
  if (token[0] == '-') {
    snprintf(detail, sizeof(detail),
             "cannot store '%s' into '%s' (%s) - negative numbers are not allowed in an unsigned variable",
             token, target, tname);
    cplus_print_fatal(CPLUS_EXIT_CONVERSION, "input overflow", file, line,
                      detail);
  }
  errno = 0;
  v = strtoull(token, &end, 0);
  if (end == token || *end != '\0') {
    snprintf(detail, sizeof(detail),
             "cannot store '%s' into '%s' (%s) - expected a valid unsigned integer, not text/extra characters",
             token, target, tname);
    cplus_print_fatal(CPLUS_EXIT_IO, "input error", file, line, detail);
  }
  if (size == 1) {
    maxv = UCHAR_MAX;
  } else if (size == sizeof(unsigned short)) {
    maxv = USHRT_MAX;
  } else if (size == sizeof(unsigned int)) {
    maxv = UINT_MAX;
  } else if (size == sizeof(unsigned long)) {
    maxv = ULONG_MAX;
  } else if (size != sizeof(unsigned long long)) {
    snprintf(detail, sizeof(detail),
             "unsupported destination size (%zu bytes) for '%s'", size, target);
    cplus_print_fatal(CPLUS_EXIT_UNSUPPORTED, "input error", file, line, detail);
  }
  if (errno == ERANGE || v > maxv) {
    snprintf(detail, sizeof(detail),
             "cannot store '%s' into '%s' (%s, valid range 0..%llu) - the number is too large for this variable type and would overflow memory",
             token, target, tname, maxv);
    cplus_print_fatal(CPLUS_EXIT_CONVERSION, "input overflow", file, line,
                      detail);
  }
  if (size == 1) {
    *(unsigned char *)dest = (unsigned char)v;
  } else if (size == sizeof(unsigned short)) {
    *(unsigned short *)dest = (unsigned short)v;
  } else if (size == sizeof(unsigned int)) {
    *(unsigned int *)dest = (unsigned int)v;
  } else if (size == sizeof(unsigned long)) {
    *(unsigned long *)dest = (unsigned long)v;
  } else {
    *(unsigned long long *)dest = v;
  }
}

/* #111: the integer scanners reject a destination whose byte width does not
 * match the target type.  Floating point, character and string scans must
 * apply the same rule BEFORE reading stdin: scanf("%f"/"%lf"/"%c") writes a
 * fixed number of bytes and cannot be told how large the real destination
 * is, so an undersized `size` would otherwise be an unchecked write. */
static void cplus_scan_exact_size(size_t size, size_t required,
                                  const char *target, const char *file,
                                  int line) {
  if (size == required)
    return;
  char detail[256];
  snprintf(detail, sizeof(detail),
           "scan destination '%s' has size %zu but %zu byte(s) are required "
           "for this type",
           target, size, required);
  cplus_print_fatal(CPLUS_EXIT_UNSUPPORTED, "input error", file, line, detail);
}

static void cplus_scan_min_size(size_t size, size_t required,
                                const char *target, const char *file,
                                int line) {
  if (size >= required)
    return;
  char detail[256];
  snprintf(detail, sizeof(detail),
           "scan destination '%s' has size %zu but at least %zu byte(s) are "
           "required for this type",
           target, size, required);
  cplus_print_fatal(CPLUS_EXIT_UNSUPPORTED, "input error", file, line, detail);
}

/* #111: read one whitespace-delimited token into `dest` with a hard capacity
 * of size-1 bytes (the last byte is reserved for the NUL).  There is no fixed
 * 4095-byte staging buffer: every byte of the token is consumed even when it
 * does not fit, so a long word can never leave a tail in stdin that a later
 * scan() would mistake for a separate input.  The delimiter itself is pushed
 * back, exactly as scanf("%s") leaves it pending for the next conversion. */
static void cplus_scan_string_checked(void *dest, size_t size,
                                      const char *target, const char *file,
                                      int line) {
  char *out = (char *)dest;
  const size_t capacity = size - 1u; /* size >= 1 is validated by the caller */
  size_t kept = 0u;
  size_t total = 0u;
  int overflow = 0;
  int ch;
  char detail[384];

  do {
    ch = fgetc(stdin);
  } while (ch != EOF && isspace((unsigned char)ch));

  if (ch == EOF) {
    snprintf(detail, sizeof(detail),
             "could not read input for '%s' (string) - input ended "
             "unexpectedly",
             target);
    cplus_print_fatal(CPLUS_EXIT_IO, "input error", file, line, detail);
  }

  while (ch != EOF && !isspace((unsigned char)ch)) {
    if (kept < capacity)
      out[kept++] = (char)ch;
    else
      overflow = 1;
    ++total;
    ch = fgetc(stdin);
  }
  out[kept] = '\0'; /* kept <= size-1, so this is always in bounds */
  if (ch != EOF)
    ungetc(ch, stdin);

  if (overflow) {
    snprintf(detail, sizeof(detail),
             "cannot store the %zu-byte input token into '%s' (string "
             "destination of %zu byte(s), at most %zu character(s)) - the "
             "whole token was consumed so the next scan is not desynchronised",
             total, target, size, capacity);
    cplus_print_fatal(CPLUS_EXIT_BOUNDS, "input error", file, line, detail);
  }
}

void cplus_scan_value_at(void *dest, size_t size, cplus_scan_kind kind,
                         const char *expr, const char *file, int line) {
  int rc = 0;
  char detail[256];
  const char *target = (expr && *expr) ? expr : "variable";
  cplus_log(CPLUS_LOG_DEBUG, "cplus_scan_value dest=%p size=%zu kind=%d", dest,
            size, (int)kind);
  if (!dest) {
    snprintf(detail, sizeof(detail),
             "scan destination '%s' is null (would dereference NULL)", target);
    cplus_print_fatal(CPLUS_EXIT_IO, "input error", file, line, detail);
  }

  switch (kind) {
  case CPLUS_SCAN_INT:
  case CPLUS_SCAN_LONG:
  case CPLUS_SCAN_LLONG:
    cplus_scan_signed_checked(dest, size, target, file, line);
    return;
  case CPLUS_SCAN_UINT:
  case CPLUS_SCAN_ULONG:
  case CPLUS_SCAN_ULLONG:
    cplus_scan_unsigned_checked(dest, size, target, file, line);
    return;
  case CPLUS_SCAN_FLOAT:
    cplus_scan_exact_size(size, sizeof(float), target, file, line);
    rc = scanf("%f", (float *)dest);
    break;
  case CPLUS_SCAN_DOUBLE:
    cplus_scan_exact_size(size, sizeof(double), target, file, line);
    rc = scanf("%lf", (double *)dest);
    break;
  case CPLUS_SCAN_CHAR:
    cplus_scan_min_size(size, 1u, target, file, line);
    rc = scanf(" %c", (char *)dest);
    break;
  case CPLUS_SCAN_STR:
    cplus_scan_min_size(size, 1u, target, file, line);
    cplus_scan_string_checked(dest, size, target, file, line);
    return;
  default:
    cplus_print_fatal(CPLUS_EXIT_UNSUPPORTED, "input error", file, line,
                      "unsupported scan type");
  }

  if (rc != 1) {
    snprintf(detail, sizeof(detail),
             "failed to read a valid value for '%s' from stdin", target);
    cplus_print_fatal(CPLUS_EXIT_IO, "input error", file, line, detail);
  }
}

void cplus_scan_value(void *dest, size_t size, cplus_scan_kind kind) {
  cplus_scan_value_at(dest, size, kind, NULL, NULL, 0);
}
