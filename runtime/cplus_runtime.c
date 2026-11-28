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

/* Issue #184: pthread_getattr_np() is GNU-specific and must be declared
 * before the first system header on Linux.  Define it here, ahead of
 * cplus.h, which already pulls in libc headers. */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif

#include "cplus/cplus.h"
#include <errno.h>
#include <math.h>
#include <setjmp.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#if defined(_WIN32)
#include <windows.h>
#endif

/* C+ exception frames. The stack is thread-local and holds only ordinary C
 * pointers; raising with no active try is a deterministic fatal error.
 *
 * Generation tokens (defense-in-depth)
 * ------------------------------------
 * A try frame normally lives on the stack of the function that entered it.
 * If that function exits without cplus_exception_pop (an early
 * return/break/goto/continue that a compiler lowering missed), the frame dies
 * with it and a later longjmp through its saved jmp_buf would jump into dead
 * stack (SIGBUS/hang/wrong handler).  Each enter issues a non-zero generation
 * that is mirrored in a thread-local "live token"; pop (including the
 * exit-edge cleanup emitted by the compiler) and raise retire the frame by
 * clearing its generation.  A raise validates both the token and, for frames
 * that are stack-resident, that the owning stack frame is still an ancestor
 * of the current call.  Either stale case is a deterministic
 * CPLUS_EXIT_UNSUPPORTED fatal, never a longjmp through a dead frame. */
static void cplus_print_fatal(cplus_exit_code code, const char *kind,
                              const char *file, int line, const char *msg);
static __thread CPlusExceptionFrame *cplus_exception_current = NULL;
static __thread uint64_t cplus_exception_epoch = 0u; /* last issued generation */
static __thread uint64_t cplus_exception_live = 0u; /* live head's generation */

/* Opt-in auto-runtime, enabled by generated constructors built with
 * -DCPLUS_AUTO_RUNTIME=1 (see cplus_set_auto_runtime).  Off by default so
 * every safety failure keeps its original deterministic _Exit diagnostic and
 * exit code; enabling it never changes a pointer's representation. */
static int cplus_auto_runtime_enabled = 0;

/* A stack frame that owns a try frame is an ancestor of cplus_exception_raise
 * and therefore sits at a higher address on a downward-growing stack.  A
 * frame at/below the current stack pointer is dead.  Frames in static storage
 * (some harnesses use one) are far outside any plausible stack window and are
 * validated by their generation token alone. */
#define CPLUS_EXCEPTION_STACK_WINDOW ((uintptr_t)1u << 26) /* 64 MiB */

static int cplus_exception_frame_dead(const CPlusExceptionFrame *frame) {
  volatile char probe = 0;
  uintptr_t sp = (uintptr_t)&probe;
  uintptr_t fa = (uintptr_t)frame;
  uintptr_t delta = fa > sp ? fa - sp : sp - fa;
  if (delta > CPLUS_EXCEPTION_STACK_WINDOW)
    return 0; /* not stack-resident: rely on the generation token */
  return fa <= sp; /* owning scope is not an ancestor => frame is dead */
}

void cplus_exception_enter(CPlusExceptionFrame *frame) {
  if (!frame)
    return;
  if (cplus_exception_epoch == UINT64_MAX)
    cplus_exception_epoch = 0u; /* generation zero is reserved for "retired" */
  frame->generation = ++cplus_exception_epoch;
  frame->prev = cplus_exception_current;
  cplus_exception_current = frame;
  cplus_exception_live = frame->generation;
}

void cplus_exception_pop(void) {
  CPlusExceptionFrame *frame = cplus_exception_current;
  if (!frame)
    return;
  frame->generation = 0u; /* retire: a later raise must reject this frame */
  cplus_exception_current = frame->prev;
  cplus_exception_live =
      cplus_exception_current ? cplus_exception_current->generation : 0u;
}

void cplus_exception_raise(int code, const char *message, const char *file,
                           int line) {
  if (!cplus_exception_current)
    cplus_print_fatal(CPLUS_EXIT_UNSUPPORTED, "unhandled C+ exception", file,
                      line, "throw outside a try block");
  CPlusExceptionFrame *active = cplus_exception_current;
  /* Never longjmp through a frame that is no longer live.  A cleared or
   * mismatched generation means pop/raise already retired it (including
   * exit-edge cleanup after return/break/goto/continue); a dead stack
   * residence means its owning scope exited without cleanup. */
  if (active->generation == 0u || active->generation != cplus_exception_live)
    cplus_print_fatal(CPLUS_EXIT_UNSUPPORTED, "stale C+ exception frame", file,
                      line, "exception frame generation is no longer live");
  if (cplus_exception_frame_dead(active))
    cplus_print_fatal(CPLUS_EXIT_UNSUPPORTED, "stale C+ exception frame", file,
                      line, "exception frame's scope has exited");
  cplus_exception_current = active->prev;
  cplus_exception_live =
      cplus_exception_current ? cplus_exception_current->generation : 0u;
  active->generation = 0u;
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

#if defined(__unix__) || defined(__APPLE__)
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#define CPLUS_POSIX 1
#endif
/* Issue #184: conservative global-word root scan.  The dyld image header is
 * used rather than _mh_execute_header because modern macOS linkers split the
 * data segment and the latter is not always the image the globals live in. */
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <mach-o/loader.h>
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

/* Issue #234: glibc's malloc_usable_size() is only defined for a live block;
 * probing a freed pointer reads its chunk header (non-zero for tcache/fastbin
 * chunks, SIGSEGV for mmap chunks, an ASan UAF under sanitizers).  Only Apple's
 * malloc_size() is used as the "address currently live in malloc?" probe in
 * cplus_retired_address_stale().  The test suite can force this conservative
 * path on any host with CPLUS_NO_RETIRED_CAPACITY_PROBE. */
#if defined(__APPLE__) && !defined(CPLUS_NO_RETIRED_CAPACITY_PROBE)
#define CPLUS_HAVE_RETIRED_CAPACITY_PROBE 1
#else
#define CPLUS_HAVE_RETIRED_CAPACITY_PROBE 0
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

void cplus_set_auto_runtime(int enabled) {
  cplus_auto_runtime_enabled = enabled ? 1 : 0;
}

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
  /* Opt-in auto-runtime: the diagnostic above is printed unchanged, then a
   * live try frame receives the failure as a C+ exception so a catch handler
   * can run and execution continues after the try.  A frame counts as live
   * only when its generation matches the thread's live token and its owning
   * stack scope still encloses us - the same checks cplus_exception_raise
   * applies, so raising here cannot re-enter this function (no infinite
   * recursion through the stale-frame fatal paths).  With auto-runtime off,
   * or with no usable handler frame, keep the original deterministic _Exit.
   * Memory-corruption defects therefore never silently continue: without a
   * handler the process still exits with the original code. */
  if (cplus_auto_runtime_enabled && cplus_exception_current &&
      cplus_exception_current->generation != 0u &&
      cplus_exception_current->generation == cplus_exception_live &&
      !cplus_exception_frame_dead(cplus_exception_current)) {
    cplus_exception_raise((int)code, kind, file, line);
    /* cplus_exception_raise longjmps on a live frame; it does not return. */
  }
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
  if (!isSigned) {
    /* The helper's parameters are int64_t, but an unsigned division must be
     * performed on the unsigned bit pattern (UINT64_MAX / 2 is not -1 / 2). */
    uint64_t ua = (uint64_t)a, ub = (uint64_t)b;
    return (int64_t)(remainder ? ua % ub : ua / ub);
  }
  if (bits > 1 && bits <= 64 && b == -1 &&
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
  if (bits == 0 || bits > 64) {
    /* No source width is known: operate on the full 64-bit pattern.  Use a
     * logical shift for unsigned operands (never sign-extend) and normalize
     * the count on widths where C would otherwise have UB. */
    unsigned count = (unsigned)b & 63u;
    return left ? (int64_t)((uint64_t)a << count)
                : (int64_t)(isSigned ? (a >> count)
                                     : ((uint64_t)a >> count));
  }
  if (b < 0 || (unsigned long long)b >= bits) {
    char buf[160];
    snprintf(buf, sizeof(buf), "shift count %lld is outside [0, %u)",
             (long long)b, bits);
    cplus_arith_fatal("invalid shift", buf);
  }
  if (!left && !isSigned)
    /* Logical right shift: never sign-extend an unsigned operand. */
    return (int64_t)((uint64_t)a >> (uint64_t)b);
  if (left) {
    if (isSigned && a < 0)
      cplus_arith_fatal("invalid shift",
                        "left shift of a negative signed value");
    uint64_t ua = (uint64_t)a;
    unsigned count = (unsigned)b;
    uint64_t valueBits = bits == 64 ? ~((uint64_t)0)
                                    : ((((uint64_t)1) << bits) - 1);
    uint64_t signBit = (uint64_t)1 << (bits - 1);
    /* Check the real shifted value BEFORE masking it to the source width:
     * masking first discards exactly the bits whose loss must be rejected. */
    uint64_t shifted = ua << count;
    /* Round-trip check catches bits lost past bit 63 when bits == 64: in that
     * case ~valueBits is 0, so the mask check alone sees nothing (issue #234). */
    if ((shifted & ~valueBits) || (shifted >> count) != ua ||
        (isSigned && (shifted & signBit)))
      cplus_arith_fatal("invalid shift",
                        "left shift loses bits above the source width");
    return (int64_t)(shifted & valueBits);
  }
  return (int64_t)(a >> (uint64_t)b);
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
  if (ss && sb > 0) {
    if (sb == 64)
      src = (__int128)(int64_t)value;
    else if ((value >> (sb - 1)) & 1u)
      src = (__int128)(int64_t)(value | (~((uint64_t)0) << sb));
  }
  uint64_t mask = db >= 64 ? ~((uint64_t)0)
                           : ((((uint64_t)1) << db) - 1);
  uint64_t truncated = value & mask;
  __int128 dst = (__int128)truncated;
  if (ds && db > 0) {
    if (db == 64)
      dst = (__int128)(int64_t)truncated;
    else if ((truncated >> (db - 1)) & 1u)
      dst = (__int128)(int64_t)(truncated | (~((uint64_t)0) << db));
  }
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
 *   scattered slot instead of being dropped, so the address retired *now*
 *   stays detectable as 104.  The evicted address loses ledger coverage
 *   (cplus_address_retired() honestly returns 0 for it): that is a bounded
 *   resource limit, not a silent correctness claim.
 * ------------------------------------------------------------------------- */

#define CPLUS_LIVE_INITIAL 64u
#define CPLUS_RECENT_SLOTS 4096u
#define CPLUS_RETIRED_INITIAL 256u
/* Upper bound on retired ledger entries.  Beyond this we evict a scattered
 * slot (never drop a retirement), set cplus_retired_wrapped (sticky) and print a
 * one-time warning; the evicted address loses ledger coverage.  The cap is a
 * fixed 1,048,576 addresses (documented above). */
#define CPLUS_RETIRED_MAX 1048576u
#define CPLUS_TOMBSTONE ((void *)(uintptr_t)-1)

/* EffectiveSan-style segregated size classes (issue #182).  Pure arithmetic:
 * no lock, no pointer-bit trick, no dependence on the allocator's internals.
 * Small allocations get 16-byte granularity up to 256 bytes (classes 1..16);
 * larger ones use power-of-two classes (17 == 512 bytes, 18 == 1024, ...).
 * Class 0 is reserved for a zero-byte extent.  The class is recorded in every
 * live slot by cplus_live_insert(), so all allocation paths that funnel
 * through it (cplus_alloc/zalloc/alloc_array, register_allocation, both
 * realloc paths) carry this out-of-band metadata without a changed ABI. */
#define CPLUS_LOWFAT_SMALL_STEP 16u
#define CPLUS_LOWFAT_SMALL_MAX 256u
#define CPLUS_LOWFAT_SMALL_CLASSES                                            \
  (CPLUS_LOWFAT_SMALL_MAX / CPLUS_LOWFAT_SMALL_STEP) /* 16 */

int cplus_lowfat_size_class(size_t bytes) {
  if (bytes == 0u)
    return 0;
  if (bytes <= CPLUS_LOWFAT_SMALL_MAX)
    return (int)((bytes + CPLUS_LOWFAT_SMALL_STEP - 1u) /
                CPLUS_LOWFAT_SMALL_STEP);
  size_t extent = CPLUS_LOWFAT_SMALL_MAX * 2u; /* 512 */
  int klass = CPLUS_LOWFAT_SMALL_CLASSES + 1;  /* 17 is the 512-byte class */
  while (bytes > extent) {
    ++klass;
    /* The next power would not fit in size_t: every remaining value of
     * `bytes` belongs to this top class. */
    if (extent > SIZE_MAX / 2u)
      return klass;
    extent *= 2u;
  }
  return klass;
}

typedef struct cplus_live_slot {
  void *ptr;
  size_t size;
  uint64_t generation;
  uint32_t size_class; /* additive: derived low-fat class, never user-visible */
  uint64_t type_tag;   /* additive: out-of-band effective type (0 = untagged) */
  /* Issue #181: CETS lock word for disjoint-bounds instrumentation.  Holds
   * `generation` while the allocation is live and is zeroed on release/reuse
   * so every stale alias fails its temporal check in O(1).  Additive field:
   * no public struct or calling convention change. */
  uint64_t shadow_key;
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
/* Once the ledger is full, new retirements evict a slot chosen from a
 * scattered sequence so double-free detection keeps working instead of
 * silently degrading to 105.  The sequence is scattered on purpose: a
 * sequential cursor would evict one dense band of slots, which collapses the
 * open-addressed probe chains and makes the ledger quadratic. */
static uint64_t cplus_retired_evict_cursor = 0;

/* Opt-in temporal-safety experiment: keep released blocks out of malloc's
 * reuse pool for a bounded time so stale aliases are detected instead of
 * being misattributed to a new allocation.  Disabled by default. */
#define CPLUS_QUARANTINE_SLOTS 1024u
static void *cplus_quarantine[CPLUS_QUARANTINE_SLOTS];
static size_t cplus_quarantine_next = 0;
static size_t cplus_quarantine_count = 0;

static int cplus_quarantine_cached = 0;

#if defined(CPLUS_POSIX)
static pthread_once_t cplus_quarantine_once = PTHREAD_ONCE_INIT;
static void cplus_quarantine_init(void) {
  const char *env = getenv("CPLUS_QUARANTINE");
  cplus_quarantine_cached = env && env[0] != '0';
}
static int cplus_quarantine_enabled(void) {
  /* getenv() races with setenv()/putenv()/unsetenv() in another thread, and
   * this is on every cplus_release() hot path.  Sample it exactly once. */
  pthread_once(&cplus_quarantine_once, cplus_quarantine_init);
  return cplus_quarantine_cached;
}
/* Sample before user threads start, so even the first lookup cannot race. */
__attribute__((constructor)) static void cplus_quarantine_preinit(void) {
  pthread_once(&cplus_quarantine_once, cplus_quarantine_init);
}
#elif defined(_WIN32)
static INIT_ONCE cplus_quarantine_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK cplus_quarantine_init_win(PINIT_ONCE once, PVOID param,
                                               PVOID *context) {
  (void)once;
  (void)param;
  (void)context;
  const char *env = getenv("CPLUS_QUARANTINE");
  cplus_quarantine_cached = env && env[0] != '0';
  return TRUE;
}
static int cplus_quarantine_enabled(void) {
  InitOnceExecuteOnce(&cplus_quarantine_once, cplus_quarantine_init_win, NULL,
                      NULL);
  return cplus_quarantine_cached;
}
#else
static int cplus_quarantine_enabled(void) {
  static int initialized = 0;
  if (!initialized) {
    const char *env = getenv("CPLUS_QUARANTINE");
    cplus_quarantine_cached = env && env[0] != '0';
    initialized = 1;
  }
  return cplus_quarantine_cached;
}
#endif

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

/* Issue #184 forward declarations.  The epoch collector is defined below the
 * side-table helpers but is consulted by the retirement/registration paths. */
static int cplus_gc_contains_retired_locked(const void *p);
static int cplus_gc_retire_locked(void *p, size_t size);
static void cplus_gc_tick_locked(void);
static void cplus_reclaim_collect_locked(void);

/* Called with the allocator lock held.  A retired address is a stale alias
 * unless the allocator proves a *new* block currently lives there.  Only
 * Apple's malloc_size() may be used for that liveness probe: glibc's
 * malloc_usable_size() is undefined on a freed pointer (issue #234), so other
 * platforms reject conservatively instead of querying.  A block still held in
 * quarantine or by the epoch collector has not been returned to malloc yet and
 * is always stale. */
static int cplus_retired_address_stale(const void *p) {
  if (cplus_retired_generation(p) == 0u)
    return 0;
  if (cplus_quarantine_contains(p))
    return 1;
  /* Issue #184: in epoch mode a retired block has NOT been returned to
   * malloc, so the capacity query below would call it live.  Treat a block
   * retained by the collector as stale until it is actually reclaimed. */
  if (cplus_gc_contains_retired_locked(p))
    return 1;
#if CPLUS_HAVE_RETIRED_CAPACITY_PROBE
  return cplus_malloc_capacity(p) == 0u;
#else
  return 1;
#endif
}

#ifdef CPLUS_POSIX
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

/* Counts only the linear containment scans used by the introspection API.
 * The `-fcplus-disjoint-bounds` check path never touches this counter; the
 * regression test uses it as O(1)-detection evidence. */
static uint64_t cplus_shadow_scan_count = 0u;

/* Called with cplus_alloc_mutex held.  Find the live slot whose
 * [base, base + size) covers p.  The live table is keyed on BASE only, so an
 * interior pointer needs a containment scan; it is bounded by the live table
 * capacity and used only by the additive introspection API, never by the
 * allocation/dereference hot path.  A corrupt extent that would wrap is
 * ignored rather than trusted. */
static cplus_live_slot *cplus_shadow_find_locked(const void *p) {
  ++cplus_shadow_scan_count;
  if (!p || cplus_live_cap == 0u)
    return NULL;
  uintptr_t addr = (uintptr_t)p;
  for (size_t k = 0; k < cplus_live_cap; ++k) {
    cplus_live_slot *slot = &cplus_live[k];
    void *base = slot->ptr;
    if (!base || base == CPLUS_TOMBSTONE)
      continue;
    uintptr_t b = (uintptr_t)base;
    if (addr >= b && slot->size <= UINTPTR_MAX - b && addr - b < slot->size)
      return slot;
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
  /* Additive disjoint metadata: the low-fat size class is derived once at
   * registration from the (already clamped) extent.  Existing callers and
   * slot semantics are untouched. */
  {
    int klass = cplus_lowfat_size_class(size);
    cplus_live[i].size_class = (uint32_t)(klass >= 0 ? klass : 0);
  }
  cplus_live[i].type_tag = 0u; /* additive: never inherit a previous owner's tag */
  /* Issue #181: (re)arm the CETS lock.  A released slot has shadow_key == 0,
   * so a stale alias can never be mistaken for this new generation. */
  cplus_live[i].shadow_key = generation;
  ++cplus_live_count;
}

/* -------------------------------------------------------------------------
 * Issue #181: side table for `-fcplus-disjoint-bounds` instrumentation.
 *
 * SoftBound-style pointer-SLOT metadata.  The key is the address of the
 * pointer variable (not its value), so a probe is one hash + a bounded
 * linear probe chain: O(1) on average and independent of the number of live
 * allocations.  The introspection containment scan above is never used here.
 *
 * Temporal metadata is a CETS (key, lock) pair.  A managed allocation's lock
 * is its live-table shadow_key word, zeroed on release/reuse.  A stack frame's
 * lock is the generated local frame-key word, zeroed by the cleanup attribute
 * on every return path.  Both invalidations are O(1) for all aliases.
 * ------------------------------------------------------------------------- */

#define CPLUS_SHADOW_SLOT_CAP 4096u
#define CPLUS_SHADOW_SLOT_TOMB ((void *)(uintptr_t)-1)

typedef struct cplus_shadow_slot_entry {
  void *addr;                 /* address of the instrumented pointer slot */
  const void *base;           /* lowest valid byte */
  const void *bound;          /* one past the last valid byte */
  uint64_t *lock;             /* CETS lock word; never dereferenced by callers */
  uint64_t key;               /* non-zero while the object is live */
  const void *frame_lock;     /* non-NULL when tied to a stack frame */
} cplus_shadow_slot_entry;

static cplus_shadow_slot_entry cplus_shadow_slots[CPLUS_SHADOW_SLOT_CAP];

static size_t cplus_shadow_slot_hash(const void *slot) {
  return (size_t)(cplus_hash(slot) & (CPLUS_SHADOW_SLOT_CAP - 1u));
}

static cplus_shadow_slot_entry *
cplus_shadow_slot_find_locked(const void *slot) {
  if (!slot)
    return NULL;
  size_t mask = CPLUS_SHADOW_SLOT_CAP - 1u;
  size_t i = cplus_shadow_slot_hash(slot);
  for (size_t probe = 0; probe <= mask; ++probe) {
    void *addr = cplus_shadow_slots[i].addr;
    if (addr == slot)
      return &cplus_shadow_slots[i];
    if (addr == NULL)
      return NULL;
    i = (i + 1) & mask;
  }
  return NULL;
}

/* Called with cplus_alloc_mutex held.  The next free/tombstoned slot; returns
 * NULL only for a completely full table (metadata is then dropped: the check
 * degrades to plain C rather than trusting a wrong bound). */
static cplus_shadow_slot_entry *cplus_shadow_slot_vacant_locked(void *slot) {
  size_t mask = CPLUS_SHADOW_SLOT_CAP - 1u;
  size_t i = cplus_shadow_slot_hash(slot);
  cplus_shadow_slot_entry *tomb = NULL;
  for (size_t probe = 0; probe <= mask; ++probe) {
    void *addr = cplus_shadow_slots[i].addr;
    if (addr == NULL)
      return tomb ? tomb : &cplus_shadow_slots[i];
    if (addr == CPLUS_SHADOW_SLOT_TOMB && !tomb)
      tomb = &cplus_shadow_slots[i];
    i = (i + 1) & mask;
  }
  return tomb;
}

static void cplus_shadow_slot_put_locked(void *slot, const void *base,
                                         size_t bytes, uint64_t *lock,
                                         uint64_t key,
                                         const void *frame_lock) {
  if (!slot || !base || !lock || key == 0u)
    return;
  cplus_shadow_slot_entry *e = cplus_shadow_slot_find_locked(slot);
  if (!e)
    e = cplus_shadow_slot_vacant_locked(slot);
  if (!e)
    return;
  e->addr = slot;
  e->base = base;
  /* Overflow-safe one-past-the-end extent; registrations clamp their sizes. */
  uintptr_t b = (uintptr_t)base;
  e->bound = bytes > UINTPTR_MAX - b ? (const void *)UINTPTR_MAX
                                     : (const void *)(b + bytes);
  e->lock = lock;
  e->key = key;
  e->frame_lock = frame_lock;
}

static void cplus_shadow_slot_erase_locked(void *slot) {
  cplus_shadow_slot_entry *e = cplus_shadow_slot_find_locked(slot);
  if (!e)
    return;
  e->addr = CPLUS_SHADOW_SLOT_TOMB;
  e->base = NULL;
  e->bound = NULL;
  e->lock = NULL;
  e->key = 0u;
  e->frame_lock = NULL;
}

void cplus_shadow_slot_clear(void *slot) {
  if (!slot)
    return;
  cplus_alloc_lock();
  cplus_shadow_slot_erase_locked(slot);
  cplus_alloc_unlock();
}

void cplus_shadow_slot_seed(void *slot, const void *ptr) {
  if (!slot)
    return;
  cplus_alloc_lock();
  if (!ptr) {
    cplus_shadow_slot_erase_locked(slot);
    cplus_alloc_unlock();
    return;
  }
  /* O(1) exact-base lookup only; interior pointers inherit metadata through
   * cplus_shadow_slot_copy() at the assignment/arithmetic site. */
  cplus_live_slot *live = cplus_find_live(ptr);
  if (!live || live->shadow_key == 0u) {
    cplus_shadow_slot_erase_locked(slot);
    cplus_alloc_unlock();
    return;
  }
  cplus_shadow_slot_put_locked(slot, ptr, live->size, &live->shadow_key,
                               live->shadow_key, NULL);
  cplus_alloc_unlock();
}

void cplus_shadow_slot_define(void *slot, const void *base, size_t bytes,
                              uint64_t *lock, uint64_t key) {
  if (!slot)
    return;
  cplus_alloc_lock();
  if (!base || !lock || key == 0u)
    cplus_shadow_slot_erase_locked(slot);
  else
    cplus_shadow_slot_put_locked(slot, base, bytes, lock, key, NULL);
  cplus_alloc_unlock();
}

void cplus_shadow_slot_copy(void *dst_slot, const void *src_slot) {
  if (!dst_slot || !src_slot)
    return;
  cplus_alloc_lock();
  cplus_shadow_slot_entry snapshot;
  int found = 0;
  const cplus_shadow_slot_entry *src =
      cplus_shadow_slot_find_locked(src_slot);
  if (src) {
    snapshot = *src;
    found = snapshot.addr != CPLUS_SHADOW_SLOT_TOMB;
  }
  if (found)
    cplus_shadow_slot_put_locked(dst_slot, snapshot.base,
                                 (size_t)((uintptr_t)snapshot.bound -
                                          (uintptr_t)snapshot.base),
                                 snapshot.lock, snapshot.key,
                                 snapshot.frame_lock);
  else
    cplus_shadow_slot_erase_locked(dst_slot);
  cplus_alloc_unlock();
}

int cplus_shadow_slot_check(const void *slot, const void *ptr, size_t bytes,
                            const char *file, int line) {
  if (!slot)
    return 1; /* untracked slot: plain-C behaviour, never a false positive */
  cplus_exit_code code = CPLUS_EXIT_OK;
  const char *kind = NULL;
  char msg[256];
  msg[0] = '\0';

  cplus_shadow_slot_entry snap;
  uint64_t current = 0u;
  int tracked = 0;
  cplus_alloc_lock();
  cplus_shadow_slot_entry *e = cplus_shadow_slot_find_locked(slot);
  if (e && e->addr != CPLUS_SHADOW_SLOT_TOMB) {
    snap = *e;
    current = snap.lock ? *snap.lock : 0u;
    tracked = 1;
  }
  cplus_alloc_unlock();

  if (!tracked)
    return 1;

  /* Temporal first: a released object or exited frame has zeroed its lock,
   * so stale aliases trap deterministically even when the address was reused. */
  if (!snap.lock || snap.key == 0u || current != snap.key) {
    code = CPLUS_EXIT_USE_AFTER_FREE;
    kind = "disjoint-bounds temporal violation";
    snprintf(msg, sizeof(msg),
             "pointer slot %p carries stale key %llu (lock holds %llu)",
             (void *)(uintptr_t)slot, (unsigned long long)snap.key,
             (unsigned long long)current);
  } else if (!ptr) {
    code = CPLUS_EXIT_NULL_POINTER;
    kind = "disjoint-bounds null dereference";
    snprintf(msg, sizeof(msg), "pointer slot %p holds NULL",
             (void *)(uintptr_t)slot);
  } else {
    uintptr_t a = (uintptr_t)ptr;
    uintptr_t b = (uintptr_t)snap.base;
    uintptr_t end = (uintptr_t)snap.bound;
    if (a < b || a > end || (uintptr_t)(end - a) < bytes) {
      code = CPLUS_EXIT_BOUNDS;
      kind = "disjoint-bounds spatial violation";
      snprintf(msg, sizeof(msg),
               "access [%p, +%zu) outside tracked extent [%p, %p)",
               (void *)(uintptr_t)ptr, bytes, (void *)(uintptr_t)snap.base,
               (void *)(uintptr_t)snap.bound);
    }
  }
  if (code != CPLUS_EXIT_OK)
    cplus_fatal(code, kind, file, line, msg); /* never returns (deterministic) */
  return code == CPLUS_EXIT_OK;
}

uint64_t cplus_shadow_frame_enter(void) {
  cplus_alloc_lock();
  uint64_t key = cplus_next_generation();
  cplus_alloc_unlock();
  return key;
}

void cplus_shadow_frame_leave(uint64_t *key) {
  if (!key)
    return;
  cplus_alloc_lock();
  *key = 0u; /* O(1) invalidation of every alias carrying this key */
  cplus_alloc_unlock();
}

int cplus_shadow_frame_register(void *slot, uint64_t *key, const void *base,
                                size_t bytes) {
  if (!slot || !key)
    return 0;
  cplus_alloc_lock();
  uint64_t k = *key;
  if (!base || k == 0u) {
    cplus_shadow_slot_erase_locked(slot);
    cplus_alloc_unlock();
    return 0;
  }
  cplus_shadow_slot_put_locked(slot, base, bytes, key, k, key);
  cplus_alloc_unlock();
  return 1;
}

int cplus_shadow_frame_valid(uint64_t key) { return key != 0u ? 1 : 0; }

uint64_t cplus_shadow_scan_probes(void) { return cplus_shadow_scan_count; }

void cplus_shadow_reset_probes(void) { cplus_shadow_scan_count = 0u; }

/* A live-table rehash moves the shadow_key words that instrumented slots use
 * as locks.  Rewrite those lock pointers before the old table is freed. */
static void cplus_shadow_relock_after_grow(cplus_live_slot *old, size_t old_cap,
                                           cplus_live_slot *fresh,
                                           size_t new_cap) {
  if (!old || old_cap == 0u)
    return;
  uintptr_t lo = (uintptr_t)old;
  uintptr_t hi = lo + old_cap * sizeof(*old);
  size_t mask = new_cap - 1u;
  for (size_t s = 0; s < CPLUS_SHADOW_SLOT_CAP; ++s) {
    cplus_shadow_slot_entry *e = &cplus_shadow_slots[s];
    uintptr_t lock = (uintptr_t)e->lock;
    if (!e->lock || lock < lo || lock >= hi)
      continue;
    for (size_t k = 0; k < old_cap; ++k) {
      void *p = old[k].ptr;
      if (!p || p == CPLUS_TOMBSTONE ||
          (uintptr_t)&old[k].shadow_key != lock)
        continue;
      size_t i = cplus_hash(p) & mask;
      while (fresh[i].ptr != NULL)
        i = (i + 1) & mask;
      e->lock = &fresh[i].shadow_key;
      break;
    }
  }
}

/* -------------------------------------------------------------------------
 * EffectiveSan-style segregated size-class VM regions (issue #182)
 *
 * Opt-in with CPLUS_LOWFAT_ALLOC=1 (sampled once, like CPLUS_QUARANTINE).
 * One contiguous reservation is split into equally sized per-class regions;
 * class k's slots all have the same power-of-two extent.  Given any interior
 * address p, the class and slot index are pure shifts of (p - arena), so base
 * and requested size come from parallel out-of-band metadata with NO mutex and
 * NO hash probe.  The arena is published once (release) and never unmapped
 * (RCU-style: readers never have to reclaim), and each slot's state word is
 * double-checked around the metadata load.  Sizes above the largest class, or
 * a failed reservation, fall back to the ordinary tracked allocator, so all
 * pre-existing paths and the bare-pointer ABI are unchanged.
 *
 * Slot claim/recycle is serialized by cplus_alloc_mutex (already held by the
 * managed allocation paths); only the LOOKUP path is lock-free and O(1).
 * ------------------------------------------------------------------------- */

#if defined(_MSC_VER) && !defined(__clang__)
#define CPLUS_LOWFAT_MSVC 1
#include <intrin.h>
#else
#define CPLUS_LOWFAT_MSVC 0
#include <stdatomic.h>
#endif

#if CPLUS_LOWFAT_MSVC
typedef volatile LONG cplus_lf_u32;
static uint32_t cplus_lf_load_u32(cplus_lf_u32 *p) {
  return (uint32_t)InterlockedCompareExchange(p, 0L, 0L);
}
static void cplus_lf_store_u32(cplus_lf_u32 *p, uint32_t v) {
  (void)InterlockedExchange(p, (LONG)v);
}
typedef void *volatile cplus_lf_ptr;
static void *cplus_lf_load_ptr(cplus_lf_ptr *p) {
  return InterlockedCompareExchangePointer(p, NULL, NULL);
}
static void cplus_lf_store_ptr(cplus_lf_ptr *p, void *v) {
  (void)InterlockedExchangePointer(p, v);
}
static volatile LONG cplus_lf_lookups; /* 32-bit: Interlocked is always lock-free */
#define CPLUS_LF_LOOKUP_HIT() ((void)InterlockedIncrement(&cplus_lf_lookups))
#define CPLUS_LF_LOOKUP_COUNT() ((uint64_t)cplus_lf_lookups)
#else
typedef _Atomic uint32_t cplus_lf_u32;
static uint32_t cplus_lf_load_u32(cplus_lf_u32 *p) {
  return atomic_load_explicit(p, memory_order_acquire);
}
static void cplus_lf_store_u32(cplus_lf_u32 *p, uint32_t v) {
  atomic_store_explicit(p, v, memory_order_release);
}
typedef _Atomic(void *) cplus_lf_ptr;
static void *cplus_lf_load_ptr(cplus_lf_ptr *p) {
  return atomic_load_explicit(p, memory_order_acquire);
}
static void cplus_lf_store_ptr(cplus_lf_ptr *p, void *v) {
  atomic_store_explicit(p, v, memory_order_release);
}
static _Atomic uint32_t cplus_lf_lookups; /* 32-bit: lock-free on all targets */
#define CPLUS_LF_LOOKUP_HIT()                                                   \
  ((void)atomic_fetch_add_explicit(&cplus_lf_lookups, 1u, memory_order_relaxed))
#define CPLUS_LF_LOOKUP_COUNT()                                                 \
  ((uint64_t)atomic_load_explicit(&cplus_lf_lookups, memory_order_relaxed))
#endif

/* Class 0 is the zero-byte extent; 1..16 are 16-byte buckets to 256 bytes;
 * 17..24 are power-of-two buckets 512 bytes .. 64 KiB.  Larger requests stay
 * on the ordinary (side-table) allocation path. */
#define CPLUS_LOWFAT_MAX_CLASS 24u
#if UINTPTR_MAX > 0xffffffffULL
#define CPLUS_LOWFAT_REGION_SHIFT 24u /* 16 MiB per class on 64-bit */
#else
#define CPLUS_LOWFAT_REGION_SHIFT 20u /* 1 MiB per class on 32-bit */
#endif
#define CPLUS_LOWFAT_REGION_BYTES ((size_t)1u << CPLUS_LOWFAT_REGION_SHIFT)
#define CPLUS_LOWFAT_ARENA_BYTES                                                \
  ((size_t)(CPLUS_LOWFAT_MAX_CLASS + 1u) * CPLUS_LOWFAT_REGION_BYTES)

typedef struct cplus_lowfat_slot_meta {
  uint64_t generation;      /* CETS key published with the slot */
  uint64_t type_tag;        /* out-of-band effective type, 0 = untagged */
  size_t size;              /* requested extent (<= slot_size) */
  uint32_t size_class;      /* class of the containing region */
  cplus_lf_u32 state;       /* 0 = free, 1 = live (atomic, double-checked) */
  uint32_t reserved;
} cplus_lowfat_slot_meta;

typedef struct cplus_lowfat_region_desc {
  size_t slot_size;
  unsigned shift;           /* log2(slot_size) */
  size_t slot_count;
  cplus_lowfat_slot_meta *meta; /* parallel metadata, never reclaimed */
  size_t *free_stack;           /* recycled slot indices (lock-held) */
  size_t free_top;
  size_t next_slot;             /* monotone fresh-slot cursor */
} cplus_lowfat_region_desc;

static cplus_lowfat_region_desc cplus_lowfat_regions[CPLUS_LOWFAT_MAX_CLASS +
                                                      1u];
static cplus_lf_ptr cplus_lowfat_arena; /* published once, never unmapped */
static int cplus_lowfat_ready;          /* guarded by cplus_alloc_mutex */
static int cplus_lowfat_unavailable;    /* reservation failed; stay on fallback */
static size_t cplus_lowfat_live_slots;  /* guarded by cplus_alloc_mutex */

#if defined(CPLUS_POSIX)
static void *cplus_lowfat_reserve(size_t bytes) {
  void *p = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return p == MAP_FAILED ? NULL : p;
}
static void cplus_lowfat_unreserve(void *p, size_t bytes) {
  (void)bytes;
  (void)munmap(p, bytes);
}
#elif defined(_WIN32)
static void *cplus_lowfat_reserve(size_t bytes) {
  return VirtualAlloc(NULL, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}
static void cplus_lowfat_unreserve(void *p, size_t bytes) {
  (void)bytes;
  (void)VirtualFree(p, 0u, MEM_RELEASE);
}
#else
static void *cplus_lowfat_reserve(size_t bytes) { return calloc(1u, bytes); }
static void cplus_lowfat_unreserve(void *p, size_t bytes) {
  (void)bytes;
  free(p);
}
#endif

/* Environment is sampled exactly once so no lookup or allocation path calls
 * getenv() (which races with setenv/putenv in another thread). */
static int cplus_lowfat_env_enabled = 0;
#if defined(CPLUS_POSIX)
static pthread_once_t cplus_lowfat_once = PTHREAD_ONCE_INIT;
static void cplus_lowfat_sample_env(void) {
  const char *env = getenv(CPLUS_LOWFAT_ALLOC_ENV);
  cplus_lowfat_env_enabled = env && env[0] != '\0' && env[0] != '0';
}
static int cplus_lowfat_env_state(void) {
  pthread_once(&cplus_lowfat_once, cplus_lowfat_sample_env);
  return cplus_lowfat_env_enabled;
}
__attribute__((constructor)) static void cplus_lowfat_preinit(void) {
  pthread_once(&cplus_lowfat_once, cplus_lowfat_sample_env);
}
#elif defined(_WIN32)
static INIT_ONCE cplus_lowfat_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK cplus_lowfat_sample_env_win(PINIT_ONCE once, PVOID param,
                                                 PVOID *context) {
  (void)once;
  (void)param;
  (void)context;
  const char *env = getenv(CPLUS_LOWFAT_ALLOC_ENV);
  cplus_lowfat_env_enabled = env && env[0] != '\0' && env[0] != '0';
  return TRUE;
}
static int cplus_lowfat_env_state(void) {
  InitOnceExecuteOnce(&cplus_lowfat_once, cplus_lowfat_sample_env_win, NULL,
                      NULL);
  return cplus_lowfat_env_enabled;
}
#else
static int cplus_lowfat_env_state(void) {
  static int initialized = 0;
  if (!initialized) {
    const char *env = getenv(CPLUS_LOWFAT_ALLOC_ENV);
    cplus_lowfat_env_enabled = env && env[0] != '\0' && env[0] != '0';
    initialized = 1;
  }
  return cplus_lowfat_env_enabled;
}
#endif

/* -1 means "use the sampled environment"; >= 0 is an explicit override. */
static int cplus_lowfat_override = -1;

int cplus_lowfat_enabled(void) {
  return cplus_lowfat_override >= 0 ? cplus_lowfat_override
                                    : cplus_lowfat_env_state();
}

void cplus_lowfat_set_enabled(int enabled) {
  cplus_lowfat_override = enabled ? 1 : 0;
}

int cplus_lowfat_type_meta_enabled(void) {
  cplus_alloc_lock();
  int ready = cplus_lowfat_ready;
  cplus_alloc_unlock();
  return cplus_lowfat_enabled() && ready;
}

size_t cplus_lowfat_live_count(void) {
  cplus_alloc_lock();
  size_t n = cplus_lowfat_live_slots;
  cplus_alloc_unlock();
  return n;
}

uint64_t cplus_lowfat_lockfree_lookups(void) { return CPLUS_LF_LOOKUP_COUNT(); }

static size_t cplus_lowfat_slot_size(uint32_t klass) {
  if (klass == 0u)
    return 1u;
  if (klass <= CPLUS_LOWFAT_SMALL_CLASSES)
    return (size_t)klass * CPLUS_LOWFAT_SMALL_STEP;
  return (size_t)CPLUS_LOWFAT_SMALL_MAX
         << (unsigned)(klass - CPLUS_LOWFAT_SMALL_CLASSES);
}

static unsigned cplus_lowfat_shift(size_t slot_size) {
  unsigned s = 0u;
  while (((size_t)1u << s) < slot_size)
    ++s;
  return s;
}

/* Called with cplus_alloc_mutex held; safe to call repeatedly.  A failed
 * reservation is sticky so a low-memory process does not retry mmap on every
 * managed allocation. */
static void cplus_lowfat_init_locked(void) {
  if (cplus_lowfat_ready || cplus_lowfat_unavailable)
    return;
  void *base = cplus_lowfat_reserve(CPLUS_LOWFAT_ARENA_BYTES);
  if (!base) {
    cplus_lowfat_unavailable = 1;
    return;
  }
  for (uint32_t k = 0u; k <= CPLUS_LOWFAT_MAX_CLASS; ++k) {
    size_t slot = cplus_lowfat_slot_size(k);
    unsigned shift = cplus_lowfat_shift(slot);
    size_t count = CPLUS_LOWFAT_REGION_BYTES >> shift;
    cplus_lowfat_slot_meta *m =
        (cplus_lowfat_slot_meta *)calloc(count, sizeof(*m));
    size_t *stack = (size_t *)calloc(count, sizeof(*stack));
    if (!m || !stack) {
      free(m);
      free(stack);
      for (uint32_t j = 0u; j < k; ++j) {
        free(cplus_lowfat_regions[j].meta);
        free(cplus_lowfat_regions[j].free_stack);
        cplus_lowfat_regions[j].meta = NULL;
        cplus_lowfat_regions[j].free_stack = NULL;
      }
      cplus_lowfat_unreserve(base, CPLUS_LOWFAT_ARENA_BYTES);
      cplus_lowfat_unavailable = 1;
      return;
    }
    cplus_lowfat_regions[k].slot_size = slot;
    cplus_lowfat_regions[k].shift = shift;
    cplus_lowfat_regions[k].slot_count = count;
    cplus_lowfat_regions[k].meta = m;
    cplus_lowfat_regions[k].free_stack = stack;
    cplus_lowfat_regions[k].free_top = 0u;
    cplus_lowfat_regions[k].next_slot = 0u;
  }
  /* Release publish: every region field above is visible to any reader that
   * subsequently observes the arena. */
  cplus_lf_store_ptr(&cplus_lowfat_arena, base);
  cplus_lowfat_ready = 1;
}

/* Pure O(1) address-to-slot arithmetic: no lock, no allocation, no probe.
 * Returns 1 for any address inside the reserved arena (live slot or padding). */
static int cplus_lowfat_region_of(const void *p, size_t *index_out,
                                  uint32_t *class_out) {
  void *arena = cplus_lf_load_ptr(&cplus_lowfat_arena);
  if (!arena || !p)
    return 0;
  uintptr_t a = (uintptr_t)arena;
  uintptr_t x = (uintptr_t)p;
  if (x < a)
    return 0;
  uintptr_t off = x - a;
  if (off >= (uintptr_t)CPLUS_LOWFAT_ARENA_BYTES)
    return 0;
  uint32_t klass = (uint32_t)(off >> CPLUS_LOWFAT_REGION_SHIFT);
  if (klass > CPLUS_LOWFAT_MAX_CLASS)
    return 0;
  const cplus_lowfat_region_desc *d = &cplus_lowfat_regions[klass];
  size_t within = (size_t)(off & (CPLUS_LOWFAT_REGION_BYTES - 1u));
  size_t index = within >> d->shift;
  if (index >= d->slot_count)
    return 0;
  if (index_out)
    *index_out = index;
  if (class_out)
    *class_out = klass;
  return 1;
}

static void *cplus_lowfat_slot_base(uint32_t klass, size_t index) {
  void *arena = cplus_lf_load_ptr(&cplus_lowfat_arena);
  size_t shift = cplus_lowfat_regions[klass].shift;
  return (void *)((char *)arena + ((size_t)klass << CPLUS_LOWFAT_REGION_SHIFT) +
                  (index << shift));
}

int cplus_lowfat_contains(const void *ptr) {
  size_t index;
  uint32_t klass;
  return cplus_lowfat_region_of(ptr, &index, &klass) ? 1 : 0;
}

/* Lock-free O(1) bounds resolution.  The state word is read before and after
 * the metadata snapshot; writers hold cplus_alloc_mutex and set it to 0 before
 * changing anything, so a reader either sees a consistent live slot or falls
 * back to the side table.  A pointer in a slot's padding (beyond its requested
 * size) is deliberately not reported as an interior pointer. */
static int cplus_lowfat_resolve(const void *ptr, void **base_out,
                                size_t *size_out, uint64_t *gen_out,
                                uint64_t *tag_out, uint32_t *class_out,
                                cplus_lowfat_slot_meta **meta_out) {
  size_t index;
  uint32_t klass;
  if (!cplus_lowfat_region_of(ptr, &index, &klass))
    return 0;
  cplus_lowfat_slot_meta *m = &cplus_lowfat_regions[klass].meta[index];
  if (cplus_lf_load_u32(&m->state) != 1u)
    return 0;
  void *base = cplus_lowfat_slot_base(klass, index);
  size_t size = m->size;
  uint64_t generation = m->generation;
  uint64_t tag = m->type_tag;
  uint32_t size_class = m->size_class;
  if (cplus_lf_load_u32(&m->state) != 1u)
    return 0;
  if (size == 0u || (uintptr_t)ptr - (uintptr_t)base >= (uintptr_t)size)
    return 0;
  if (base_out)
    *base_out = base;
  if (size_out)
    *size_out = size;
  if (gen_out)
    *gen_out = generation;
  if (tag_out)
    *tag_out = tag;
  if (class_out)
    *class_out = size_class;
  if (meta_out)
    *meta_out = m;
  CPLUS_LF_LOOKUP_HIT();
  return 1;
}

/* Called with cplus_alloc_mutex held.  Reserves a slot but leaves its state
 * free until cplus_lowfat_publish_locked() has filled the metadata. */
static void *cplus_lowfat_claim_locked(size_t size) {
  if (!cplus_lowfat_enabled())
    return NULL;
  cplus_lowfat_init_locked();
  if (!cplus_lowfat_ready)
    return NULL;
  int klass = cplus_lowfat_size_class(size);
  if (klass < 0 || (uint32_t)klass > CPLUS_LOWFAT_MAX_CLASS)
    return NULL;
  size_t slot = cplus_lowfat_slot_size((uint32_t)klass);
  if (size == 0u)
    size = 1u;
  if (size > slot)
    return NULL;
  cplus_lowfat_region_desc *d = &cplus_lowfat_regions[klass];
  size_t index;
  if (d->free_top != 0u) {
    index = d->free_stack[--d->free_top];
  } else if (d->next_slot < d->slot_count) {
    index = d->next_slot++;
  } else {
    return NULL; /* class exhausted: ordinary allocator handles this one */
  }
  return cplus_lowfat_slot_base((uint32_t)klass, index);
}

/* Called with cplus_alloc_mutex held, immediately after cplus_live_insert(). */
static void cplus_lowfat_publish_locked(void *p, size_t size, uint64_t gen) {
  size_t index;
  uint32_t klass;
  if (!cplus_lowfat_region_of(p, &index, &klass))
    return;
  cplus_lowfat_slot_meta *m = &cplus_lowfat_regions[klass].meta[index];
  cplus_lf_store_u32(&m->state, 0u);
  m->size = size;
  m->generation = gen;
  m->type_tag = 0u;
  m->size_class = klass;
  cplus_lf_store_u32(&m->state, 1u);
  ++cplus_lowfat_live_slots;
}

/* Called with cplus_alloc_mutex held.  Clears a slot's out-of-band metadata
 * WITHOUT making the address available for reuse.  Issue #184 uses this when
 * a low-fat block is retired under epoch reclamation: the address stays
 * reserved until a conservative collection proves it unreachable. */
static void cplus_lowfat_clear_meta_locked(void *p) {
  size_t index;
  uint32_t klass;
  if (!cplus_lowfat_region_of(p, &index, &klass))
    return;
  if (cplus_lowfat_slot_base(klass, index) != p)
    return; /* interior pointers are not valid release targets */
  cplus_lowfat_slot_meta *m = &cplus_lowfat_regions[klass].meta[index];
  cplus_lf_store_u32(&m->state, 0u);
  m->size = 0u;
  m->generation = 0u;
  m->type_tag = 0u;
  m->size_class = 0u;
  if (cplus_lowfat_live_slots != 0u)
    --cplus_lowfat_live_slots;
}

/* Called with cplus_alloc_mutex held; recycles an exact base pointer. */
static void cplus_lowfat_recycle_locked(void *p) {
  size_t index;
  uint32_t klass;
  if (!cplus_lowfat_region_of(p, &index, &klass))
    return;
  if (cplus_lowfat_slot_base(klass, index) != p)
    return; /* interior pointers are not valid release targets */
  cplus_lowfat_clear_meta_locked(p);
  cplus_lowfat_region_desc *d = &cplus_lowfat_regions[klass];
  if (d->free_top < d->slot_count)
    d->free_stack[d->free_top++] = index;
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
    /* Growers run with cplus_alloc_mutex held.  Unlock before the fatal:
     * CPLUS_AUTO_RUNTIME longjmps out of it into a catch handler, and a
     * locked mutex would deadlock every later allocator call on all threads. */
    cplus_alloc_unlock();
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
              "entries); further retirements evict scattered entries\n",
              (unsigned)CPLUS_RETIRED_MAX);
      fflush(stderr);
    }
    /* Bounded ledger: evict one occupied slot rather than drop
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
    /* Scatter the eviction victims.  A sequential cursor turns the evicted
     * slots into one dense band, and a dense band of occupied slots collapses
     * the open-addressed probe chains into long runs: every later retirement
     * then rescans the band and the ledger turns into a quadratic hot path.
     * A multiplicative stride spreads the victims over the whole table, so
     * the chains stay short however many addresses are retired. */
    cplus_retired_evict_cursor =
        cplus_retired_evict_cursor * 6364136223846793005ULL +
        1442695040888963407ULL;
    size_t victim = (size_t)((cplus_retired_evict_cursor >> 32) & mask);
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

/* Called with the allocator lock held. */
static void cplus_live_grow(void) {
  /* Tombstone churn (allocate/free one pointer in a loop) must rehash at the
   * same capacity instead of doubling forever (unbounded OOM). */
  size_t cap = cplus_live_cap;
  if (cap != 0 && cplus_live_count + 1 <= cap / 2)
    cap = cplus_live_cap;
  else
    cap = cplus_live_cap ? cplus_live_cap * 2 : CPLUS_LIVE_INITIAL;
  cplus_live_slot *fresh = (cplus_live_slot *)calloc(cap, sizeof(*fresh));
  if (!fresh) {
    /* See cplus_retired_grow(): never longjmp out with the allocator locked. */
    cplus_alloc_unlock();
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
  /* Issue #181: instrumented slots hold pointers into the OLD table's CETS
   * lock words.  Re-point them before freeing it. */
  cplus_shadow_relock_after_grow(cplus_live, cplus_live_cap, fresh, cap);
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
    old->shadow_key = 0u; /* issue #181: invalidate every stale alias, O(1) */
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
  slot->shadow_key = 0u; /* issue #181: invalidate every stale alias, O(1) */
  --cplus_live_count;
  ++cplus_live_deleted;
  cplus_recent[cplus_recent_index(p)].ptr = p;
  cplus_recent[cplus_recent_index(p)].generation = generation;
  cplus_retired_insert(p, generation);
  /* Issue #184: epoch mode retains the block out of malloc's reuse pool
   * until a conservative collection proves no root still references it. */
  if (cplus_gc_retire_locked(p, slot->size)) {
    cplus_alloc_unlock();
    return;
  }
  /* Free while the side-table lock is still held.  Releasing it first lets a
   * concurrent registration insert the same address into the live table
   * before the free lands, leaving a stale live entry and a real double free. */
  free(p);
  cplus_alloc_unlock();
}

void *cplus_alloc(size_t size) {
  if (size <= 0)
    size = 1;
  /* Opt-in low-fat slot first.  Claim/publish run under the side-table lock;
   * only the later bounds LOOKUP is lock-free.  A disabled, unavailable or
   * exhausted class falls back to the original malloc path unchanged. */
  cplus_alloc_lock();
  cplus_gc_tick_locked(); /* issue #184: periodic epoch boundary, opt-in only */
  void *p = cplus_lowfat_claim_locked(size);
  if (!p) {
    p = malloc(size);
    if (!p) {
      cplus_alloc_unlock();
      cplus_print_fatal(CPLUS_EXIT_RESOURCE, "out of memory", NULL, 0,
                        "managed allocation failed");
    }
  }
  int lowfat = cplus_lowfat_contains(p);
  /* Match cplus_register_allocation(): an allocator that hands back a live
   * or stale-retired address must fail closed instead of registering a second
   * owner that a later single release cannot retire.  A fresh low-fat slot is
   * not a malloc block and cannot be a retired heap address. */
  if (!lowfat && (cplus_find_live(p) || cplus_retired_address_stale(p))) {
    cplus_alloc_unlock();
    cplus_print_fatal(CPLUS_EXIT_DOUBLE_FREE, "allocation registration", NULL,
                      0, "allocator returned a live or retired address");
  }
  if (!lowfat) {
    cplus_quarantine_drop(p);
    size = cplus_clamp_registered_size(p, size);
  }
  uint64_t gen = cplus_next_generation();
  if (cplus_live_count + cplus_live_deleted + 1 > cplus_live_cap * 3 / 4)
    cplus_live_grow();
  cplus_live_insert(p, size, gen);
  if (lowfat)
    cplus_lowfat_publish_locked(p, size, gen);
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

static void cplus_release_locked(void *ptr); /* defined after cplus_realloc */

void *cplus_realloc(void *ptr, size_t size) {
  cplus_log(CPLUS_LOG_DEBUG, "cplus_realloc ptr=%p size=%zu", ptr, size);
  if (!ptr)
    return cplus_alloc(size);
  if (size == 0) {
    cplus_release(ptr);
    return NULL;
  }
  /* cplus_realloc is an ownership transfer: the old slot is retired with its
   * generation and the fresh block gets a new generation.  Validate the old
   * pointer BEFORE allocating, then re-check it under one lock that is held
   * across the copy and the retirement so a concurrent release/realloc cannot
   * free the source mid-copy or trigger a bogus double-free. */
  cplus_alloc_lock();
  int was_live = cplus_find_live(ptr) != NULL;
  cplus_alloc_unlock();
  if (!was_live)
    cplus_print_fatal(CPLUS_EXIT_DOUBLE_FREE, "realloc", NULL, 0,
                      "pointer is not a live C+ allocation");

  void *fresh = cplus_alloc(size);
  cplus_alloc_lock();
  cplus_live_slot *slot = cplus_find_live(ptr);
  if (!slot) {
    /* fresh is already registered in cplus_live.  Retire it through the
     * normal release path: a raw free() here leaves a live entry pointing at
     * freed heap, so an address reused by a later allocation traps with a
     * spurious double-free - especially when CPLUS_AUTO_RUNTIME longjmps out
     * of the fatal below and execution continues in a catch handler. */
    cplus_release_locked(fresh);
    cplus_alloc_unlock();
    cplus_print_fatal(CPLUS_EXIT_DOUBLE_FREE, "realloc", NULL, 0,
                      "allocation was released while reallocating");
  }
  size_t n = size < slot->size ? size : slot->size;
  /* Belt-and-braces: even a wrongly registered or stale extent must never
   * authorize a copy past the block malloc actually handed out.  A low-fat
   * slot is not a malloc block, so the platform query is skipped for it. */
  size_t usable = cplus_lowfat_contains(ptr) ? 0u : cplus_malloc_capacity(ptr);
  if (usable != 0u && n > usable)
    n = usable;
  memcpy(fresh, ptr, n);
  cplus_release_locked(ptr);
  cplus_alloc_unlock();
  return fresh;
}

void cplus_zero(void *p, size_t size) {
  if (p && size)
    memset(p, 0, size);
}

/* Release an allocation while cplus_alloc_mutex is already held.  Callers
 * that must keep the lock across a copy (cplus_realloc) use this so no other
 * thread can retire/reallocate the same pointer between the copy and free. */
static void cplus_release_locked(void *ptr) {
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
  slot->shadow_key = 0u; /* issue #181: invalidate every stale alias, O(1) */
  --cplus_live_count;
  ++cplus_live_deleted;
  cplus_recent[cplus_recent_index(ptr)].ptr = ptr;
  cplus_recent[cplus_recent_index(ptr)].generation = generation;
  cplus_retired_insert(ptr, generation);
  /* Issue #184: in epoch mode keep the block reserved.  The caller owns the
   * lock here (cplus_release / cplus_realloc), so do not unlock on this path. */
  if (cplus_gc_retire_locked(ptr, slot->size))
    return;
  /* A low-fat slot is reserved VM, not a malloc block: recycle it into its
   * class free stack and clear its out-of-band metadata instead of free()ing
   * it.  Quarantine does not apply (the address is never reused by malloc). */
  if (cplus_lowfat_contains(ptr)) {
    cplus_lowfat_recycle_locked(ptr);
    return;
  }
  if (cplus_quarantine_enabled()) {
    void *oldest = cplus_quarantine[cplus_quarantine_next];
    cplus_quarantine[cplus_quarantine_next] = ptr;
    cplus_quarantine_next = (cplus_quarantine_next + 1) %
                            CPLUS_QUARANTINE_SLOTS;
    if (cplus_quarantine_count < CPLUS_QUARANTINE_SLOTS)
      ++cplus_quarantine_count;
    if (oldest)
      free(oldest); /* bounded: at most CPLUS_QUARANTINE_SLOTS delayed frees */
    return;
  }
  /* Keep the lock across free(): see cplus_free_registered above. */
  free(ptr);
}

void cplus_release(void *ptr) {
  if (!ptr)
    return;
  cplus_log(CPLUS_LOG_DEBUG, "cplus_release ptr=%p", ptr);
  cplus_alloc_lock();
  cplus_release_locked(ptr);
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

/* Additive introspection for the bounded retired ledger.  Once this returns
 * non-zero, the cap has been reached and further retirements evict an older
 * entry; the evicted address is then no longer covered by the
 * ledger. */
int cplus_retired_ledger_full(void) {
  cplus_alloc_lock();
  int full = cplus_retired_wrapped;
  cplus_alloc_unlock();
  return full;
}

/* -------------------------------------------------------------------------
 * Issue #184: opt-in epoch/conservative reclamation
 * ------------------------------------------------------------------------
 * Normally a release returns the block to malloc immediately, so its address
 * can be handed out again while a stale alias still exists.  With
 * CPLUS_RECLAIM_MODE=epoch (or cplus_set_reclaim_mode(1)) a release instead
 * moves the block to the retired list below.  It is NOT returned to malloc
 * until a collection has advanced at least one epoch boundary AND no word in
 * the conservatively scanned roots still points into it.
 *
 * The retired-address ledger from issues #110/#111 is still populated at the
 * moment of retirement, so a second release remains a deterministic 104.
 * Low-fat slots are kept reserved too (their metadata is cleared without
 * pushing the slot onto the class free stack) and are only recycled when the
 * collector proves them unreachable.
 *
 * This is entirely additive: no existing signature, struct layout, pointer
 * representation or default behaviour changes.  All state is guarded by
 * cplus_alloc_mutex; the collector runs with that lock held and never takes
 * another runtime lock.
 * ------------------------------------------------------------------------- */

#define CPLUS_RECLAIM_TICK 256u          /* automatic epoch boundary period */
#define CPLUS_RECLAIM_ROOT_MAX 16384u     /* bounded root-word snapshot */
#define CPLUS_RECLAIM_ROOT_SLOT_MAX 256u  /* explicit global pointer slots */

typedef struct cplus_gc_block {
  void *ptr;
  size_t size;
  uint64_t retire_epoch; /* kept until epoch > retire_epoch */
  int lowfat;            /* reserved VM slot vs malloc block */
  struct cplus_gc_block *next;
} cplus_gc_block;

static cplus_gc_block *cplus_gc_retired = NULL;
static size_t cplus_gc_retired_count = 0u;
static uint64_t cplus_gc_epoch = 1u;
static uint64_t cplus_gc_reclaimed = 0u;
static size_t cplus_gc_alloc_tick = 0u;

static uintptr_t cplus_gc_roots[CPLUS_RECLAIM_ROOT_MAX];
static size_t cplus_gc_root_count = 0u;
static int cplus_gc_roots_overflow = 0;
static void **cplus_gc_root_slots[CPLUS_RECLAIM_ROOT_SLOT_MAX];
static size_t cplus_gc_root_slot_count = 0u;

/* The env var is sampled exactly once (like CPLUS_QUARANTINE) so no hot path
 * calls getenv(), which races with setenv/putenv in another thread. */
static int cplus_gc_cached_mode = 0;
static int cplus_gc_override = -1; /* >= 0 wins over the sampled environment */

static void cplus_gc_sample_env(void) {
  const char *env = getenv("CPLUS_RECLAIM_MODE");
  cplus_gc_cached_mode = env && strcmp(env, "epoch") == 0;
}
#if defined(CPLUS_POSIX)
static pthread_once_t cplus_gc_once = PTHREAD_ONCE_INIT;
static int cplus_gc_env_state(void) {
  pthread_once(&cplus_gc_once, cplus_gc_sample_env);
  return cplus_gc_cached_mode;
}
__attribute__((constructor)) static void cplus_gc_preinit(void) {
  pthread_once(&cplus_gc_once, cplus_gc_sample_env);
}
#elif defined(_WIN32)
static INIT_ONCE cplus_gc_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK cplus_gc_sample_env_win(PINIT_ONCE once, PVOID param,
                                             PVOID *context) {
  (void)once;
  (void)param;
  (void)context;
  cplus_gc_sample_env();
  return TRUE;
}
static int cplus_gc_env_state(void) {
  InitOnceExecuteOnce(&cplus_gc_once, cplus_gc_sample_env_win, NULL, NULL);
  return cplus_gc_cached_mode;
}
#else
static int cplus_gc_env_state(void) {
  static int initialized = 0;
  if (!initialized) {
    cplus_gc_sample_env();
    initialized = 1;
  }
  return cplus_gc_cached_mode;
}
#endif

int cplus_reclaim_enabled(void) {
  return cplus_gc_override >= 0 ? cplus_gc_override : cplus_gc_env_state();
}

void cplus_set_reclaim_mode(int enabled) {
  cplus_gc_override = enabled ? 1 : 0;
}

static int cplus_gc_contains_retired_locked(const void *p) {
  for (cplus_gc_block *node = cplus_gc_retired; node; node = node->next)
    if (node->ptr == p)
      return 1;
  return 0;
}

/* Called with cplus_alloc_mutex held, from a release path that has already
 * written the retired ledger.  Returns 1 when the block was retained by the
 * collector (the caller must NOT free it), 0 on OOM fallback (the caller
 * keeps the original immediate-free behaviour). */
static int cplus_gc_retire_locked(void *p, size_t size) {
  if (!cplus_reclaim_enabled())
    return 0;
  cplus_gc_block *node = (cplus_gc_block *)malloc(sizeof(*node));
  if (!node)
    return 0;
  node->ptr = p;
  node->size = size;
  /* +2 gives every retired block one full epoch boundary of grace even when
   * no root references it: retirement happens in epoch E, the first collect
   * advances to E+1 (retained), the next to E+2 (eligible). */
  node->retire_epoch = cplus_gc_epoch + 2u;
  node->lowfat = cplus_lowfat_contains(p);
  node->next = cplus_gc_retired;
  cplus_gc_retired = node;
  ++cplus_gc_retired_count;
  if (node->lowfat)
    cplus_lowfat_clear_meta_locked(p);
  return 1;
}

static void cplus_gc_tick_locked(void) {
  if (!cplus_reclaim_enabled())
    return;
  if (++cplus_gc_alloc_tick >= CPLUS_RECLAIM_TICK) {
    cplus_gc_alloc_tick = 0u;
    cplus_reclaim_collect_locked();
  }
}

/* Internal static arrays hold retired addresses as bookkeeping.  Never treat
 * our own metadata as a live root, or nothing could ever be reclaimed. */
static int cplus_gc_internal_word(uintptr_t a) {
#define CPLUS_GC_IS_INTERNAL(arr)                                              \
  ((a) >= (uintptr_t)(const void *)(arr) &&                                    \
   (a) < (uintptr_t)(const void *)(arr) + sizeof(arr))
  return CPLUS_GC_IS_INTERNAL(cplus_recent) ||
         CPLUS_GC_IS_INTERNAL(cplus_quarantine) ||
         CPLUS_GC_IS_INTERNAL(cplus_shadow_slots) ||
         CPLUS_GC_IS_INTERNAL(cplus_gc_roots) ||
         CPLUS_GC_IS_INTERNAL(cplus_gc_root_slots);
#undef CPLUS_GC_IS_INTERNAL
}

static void cplus_gc_add_root(uintptr_t word) {
  if (word == 0u)
    return;
  if (cplus_gc_root_count < CPLUS_RECLAIM_ROOT_MAX)
    cplus_gc_roots[cplus_gc_root_count++] = word;
  else
    cplus_gc_roots_overflow = 1; /* conservative: retain everything */
}

/* Read every aligned pointer-sized word in [lo, hi) with memcpy (no aliasing
 * or alignment UB).  A word that merely LOOKS like a heap pointer is enough
 * to retain a block - that is what "conservative" means here. */
static void cplus_gc_scan_words(uintptr_t lo, uintptr_t hi, int skip_internal) {
  const uintptr_t align = sizeof(uintptr_t) - 1u;
  uintptr_t a = (lo + align) & ~align;
  if (hi <= a)
    return;
  for (; hi - a >= sizeof(uintptr_t); a += sizeof(uintptr_t)) {
    if (skip_internal && cplus_gc_internal_word(a))
      continue;
    uintptr_t word = 0u;
    memcpy(&word, (const void *)a, sizeof(word));
    cplus_gc_add_root(word);
  }
}

static void cplus_gc_scan_stack(void) {
  uintptr_t anchor = (uintptr_t)(void *)&anchor;
  uintptr_t lo = anchor;
  uintptr_t hi = anchor + 64u * 1024u; /* portable fallback window */
#if defined(__APPLE__)
  void *base = pthread_get_stackaddr_np(pthread_self());
  size_t bytes = pthread_get_stacksize_np(pthread_self());
  if (base && bytes) {
    hi = (uintptr_t)base;
    lo = hi - bytes;
  }
#elif defined(__linux__)
  pthread_attr_t attr;
  if (pthread_getattr_np(pthread_self(), &attr) == 0) {
    void *base = NULL;
    size_t bytes = 0u;
    if (pthread_attr_getstack(&attr, &base, &bytes) == 0 && bytes &&
        base) {
      lo = (uintptr_t)base;
      hi = lo + bytes;
    }
    pthread_attr_destroy(&attr);
  }
#endif
  if (hi > lo)
    cplus_gc_scan_words(lo, hi, 0);
}

#if defined(__linux__)
/* Weak: the symbols are present in the normal executable link and absent in
 * some static/embedded links.  Missing symbols simply disable the automatic
 * global scan; cplus_reclaim_register_root() still works everywhere. */
extern char __data_start __attribute__((weak));
extern char __bss_start __attribute__((weak));
extern char _edata __attribute__((weak));
extern char _end __attribute__((weak));
#endif

static void cplus_gc_scan_globals(void) {
#if defined(__APPLE__)
  const struct mach_header_64 *mh =
      (const struct mach_header_64 *)_dyld_get_image_header(0);
  static const char *const sections[] = {"__data", "__bss"};
  if (mh) {
    for (size_t i = 0u; i < sizeof(sections) / sizeof(sections[0]); ++i) {
      unsigned long bytes = 0ul;
      const uint8_t *p = getsectiondata(mh, "__DATA", sections[i], &bytes);
      if (p && bytes)
        cplus_gc_scan_words((uintptr_t)p, (uintptr_t)p + bytes, 1);
    }
  }
#elif defined(__linux__)
  uintptr_t lo = 0u;
  uintptr_t hi = 0u;
  if (&__data_start)
    lo = (uintptr_t)(void *)&__data_start;
  else if (&__bss_start)
    lo = (uintptr_t)(void *)&__bss_start;
  if (&_end)
    hi = (uintptr_t)(void *)&_end;
  else if (&_edata)
    hi = (uintptr_t)(void *)&_edata;
  if (lo && hi && hi > lo)
    cplus_gc_scan_words(lo, hi, 1);
#endif
  /* Explicitly registered global slots are scanned on every platform. */
  for (size_t i = 0u; i < cplus_gc_root_slot_count; ++i) {
    void **slot = cplus_gc_root_slots[i];
    if (slot)
      cplus_gc_add_root((uintptr_t)*slot);
  }
}

/* Called with cplus_alloc_mutex held.  One epoch boundary plus a full
 * conservative root snapshot; unreferenced blocks that survived the grace
 * epoch are returned to their allocator. */
static void cplus_reclaim_collect_locked(void) {
  if (!cplus_reclaim_enabled())
    return;
  ++cplus_gc_epoch;
  cplus_gc_root_count = 0u;
  cplus_gc_roots_overflow = 0;
  /* Globals first: the root snapshot is itself a global array, and clearing
   * it before the global scan prevents the collector from rooting its own
   * previous snapshot. */
  cplus_gc_scan_globals();
  cplus_gc_scan_stack();

  cplus_gc_block **link = &cplus_gc_retired;
  while (*link) {
    cplus_gc_block *node = *link;
    int referenced = cplus_gc_roots_overflow ||
                     node->retire_epoch > cplus_gc_epoch;
    if (!referenced) {
      uintptr_t lo = (uintptr_t)node->ptr;
      uintptr_t hi = lo + node->size;
      if (hi < lo)
        hi = UINTPTR_MAX;
      for (size_t k = 0u; k < cplus_gc_root_count; ++k) {
        uintptr_t word = cplus_gc_roots[k];
        if (word >= lo && word < hi) {
          referenced = 1;
          break;
        }
      }
    }
    if (referenced) {
      link = &node->next;
      continue;
    }
    *link = node->next;
    if (node->lowfat)
      cplus_lowfat_recycle_locked(node->ptr);
    else
      free(node->ptr);
    ++cplus_gc_reclaimed;
    if (cplus_gc_retired_count)
      --cplus_gc_retired_count;
    free(node);
  }
}

uint64_t cplus_reclaim_epoch(void) {
  cplus_alloc_lock();
  uint64_t e = cplus_gc_epoch;
  cplus_alloc_unlock();
  return e;
}

uint64_t cplus_reclaim_reclaimed_count(void) {
  cplus_alloc_lock();
  uint64_t n = cplus_gc_reclaimed;
  cplus_alloc_unlock();
  return n;
}

size_t cplus_reclaim_retired_count(void) {
  cplus_alloc_lock();
  size_t n = cplus_gc_retired_count;
  cplus_alloc_unlock();
  return n;
}

int cplus_reclaim_contains_retired(const void *ptr) {
  if (!ptr)
    return 0;
  cplus_alloc_lock();
  int found = cplus_gc_contains_retired_locked(ptr);
  cplus_alloc_unlock();
  return found;
}

void cplus_reclaim_collect(void) {
  cplus_alloc_lock();
  cplus_reclaim_collect_locked();
  cplus_alloc_unlock();
}

void cplus_reclaim_register_root(void **slot) {
  if (!slot)
    return;
  cplus_alloc_lock();
  for (size_t i = 0u; i < cplus_gc_root_slot_count; ++i)
    if (cplus_gc_root_slots[i] == slot) {
      cplus_alloc_unlock();
      return;
    }
  if (cplus_gc_root_slot_count < CPLUS_RECLAIM_ROOT_SLOT_MAX)
    cplus_gc_root_slots[cplus_gc_root_slot_count++] = slot;
  cplus_alloc_unlock();
}

void cplus_reclaim_unregister_root(void **slot) {
  if (!slot)
    return;
  cplus_alloc_lock();
  for (size_t i = 0u; i < cplus_gc_root_slot_count; ++i) {
    if (cplus_gc_root_slots[i] == slot) {
      cplus_gc_root_slots[i] =
          cplus_gc_root_slots[cplus_gc_root_slot_count - 1u];
      --cplus_gc_root_slot_count;
      break;
    }
  }
  cplus_alloc_unlock();
}

/* Additive disjoint shadow metadata (issues #181 / #182).  All of these keep
 * the bare-pointer ABI: they only READ the existing side table and never
 * change a pointer's bits or an existing function's signature. */

int cplus_shadow_lookup(const void *ptr, cplus_shadow_info *info) {
  cplus_shadow_info empty;
  memset(&empty, 0, sizeof(empty));
  if (info)
    *info = empty;
  if (!ptr)
    return 0;
  /* Fast path: low-fat interior pointers resolve in O(1) with no mutex. */
  void *lf_base = NULL;
  size_t lf_size = 0u;
  uint64_t lf_gen = 0u, lf_tag = 0u;
  uint32_t lf_class = 0u;
  cplus_lowfat_slot_meta *lf_meta = NULL;
  if (cplus_lowfat_resolve(ptr, &lf_base, &lf_size, &lf_gen, &lf_tag, &lf_class,
                           &lf_meta)) {
    cplus_shadow_info result;
    memset(&result, 0, sizeof(result));
    result.base = lf_base;
    result.bound = (void *)((char *)lf_base + lf_size);
    result.generation = lf_gen;
    result.lock = (const void *)lf_meta; /* introspection only; never deref */
    result.size_class = lf_class;
    result.flags = CPLUS_SHADOW_LIVE;
    if (ptr != lf_base)
      result.flags |= CPLUS_SHADOW_INTERIOR;
    if (info)
      *info = result;
    return 1;
  }
  cplus_alloc_lock();
  /* O(1) exact-base probe first; only true interior pointers need the counted
   * containment scan (issue #181: keep scans off the instrumented check path). */
  cplus_live_slot *slot = cplus_find_live(ptr);
  if (!slot)
    slot = cplus_shadow_find_locked(ptr);
  if (slot) {
    cplus_shadow_info result;
    memset(&result, 0, sizeof(result));
    result.base = slot->ptr;
    result.bound = (void *)((char *)slot->ptr + slot->size);
    result.generation = slot->generation;
    /* CETS lock address: the live slot that currently holds the key.  It is
     * reported for introspection only; callers must not dereference it. */
    result.lock = slot;
    result.size_class = slot->size_class;
    result.flags = CPLUS_SHADOW_LIVE;
    if (ptr != slot->ptr)
      result.flags |= CPLUS_SHADOW_INTERIOR;
    if (info)
      *info = result;
  }
  cplus_alloc_unlock();
  return slot != NULL ? 1 : 0;
}

int cplus_shadow_base_bound(const void *ptr, void **base, void **bound) {
  cplus_shadow_info info;
  if (!cplus_shadow_lookup(ptr, &info))
    return 0;
  if (base)
    *base = info.base;
  if (bound)
    *bound = info.bound;
  return 1;
}

int cplus_interior_valid(const void *ptr, uint64_t key) {
  if (!ptr || key == 0u)
    return 0;
  void *base = NULL;
  size_t size = 0u;
  uint64_t gen = 0u, tag = 0u;
  uint32_t klass = 0u;
  if (cplus_lowfat_resolve(ptr, &base, &size, &gen, &tag, &klass, NULL))
    return gen == key ? 1 : 0;
  cplus_alloc_lock();
  cplus_live_slot *slot = cplus_shadow_find_locked(ptr);
  int ok = slot != NULL && slot->generation == key;
  cplus_alloc_unlock();
  return ok ? 1 : 0;
}

int cplus_lowfat_class_of(const void *ptr) {
  if (!ptr)
    return -1;
  uint32_t klass = 0u;
  if (cplus_lowfat_resolve(ptr, NULL, NULL, NULL, NULL, &klass, NULL))
    return (int)klass;
  cplus_alloc_lock();
  cplus_live_slot *slot = cplus_shadow_find_locked(ptr);
  int result = slot ? (int)slot->size_class : -1;
  cplus_alloc_unlock();
  return result;
}

int cplus_lowfat_bounds(const void *ptr, void **base, size_t *size) {
  /* Lock-free O(1) path for opted-in low-fat allocations, including interior
   * pointers that have crossed an uninstrumented C ABI boundary. */
  void *lf_base = NULL;
  size_t lf_size = 0u;
  if (cplus_lowfat_resolve(ptr, &lf_base, &lf_size, NULL, NULL, NULL, NULL)) {
    if (base)
      *base = lf_base;
    if (size)
      *size = lf_size;
    return 1;
  }
  cplus_shadow_info info;
  if (!cplus_shadow_lookup(ptr, &info))
    return 0;
  if (base)
    *base = info.base;
  if (size)
    *size = (size_t)((char *)info.bound - (char *)info.base);
  return 1;
}

/* -------------------------------------------------------------------------
 * Out-of-band effective type tags (issue #182)
 *
 * Type names/sizes are interned into a small static registry; the resulting
 * non-zero tag is stored in parallel metadata (low-fat slot meta for opted-in
 * allocations, the existing live slot otherwise).  Registration and checks
 * use cplus_alloc_mutex, so no new lock or global constructor is needed and
 * the fast bounds path stays untouched.  Mismatches always go through
 * cplus_print_fatal -> CPLUS_EXIT_TYPE_CONFUSION (105), never through a raw
 * abort, so the diagnostic and exit code remain deterministic.
 * ------------------------------------------------------------------------- */

#define CPLUS_TYPE_REGISTRY_MAX 256u
#define CPLUS_TYPE_MEMBER_MAX 512u
#define CPLUS_TYPE_NAME_MAX 64u
#define CPLUS_TYPE_MEMBER_NAME_MAX 32u

typedef struct cplus_type_entry {
  uint64_t tag;
  size_t size;
  char name[CPLUS_TYPE_NAME_MAX];
} cplus_type_entry;

typedef struct cplus_type_member_entry {
  uint64_t parent;
  uint64_t member;
  size_t offset;
  size_t size;
  char name[CPLUS_TYPE_MEMBER_NAME_MAX];
} cplus_type_member_entry;

static cplus_type_entry cplus_type_registry[CPLUS_TYPE_REGISTRY_MAX];
static size_t cplus_type_registry_count;
static cplus_type_member_entry cplus_type_members[CPLUS_TYPE_MEMBER_MAX];
static size_t cplus_type_member_count;

/* FNV-1a over a bounded name plus the size hint.  Zero is reserved for
 * "untagged", so a zero hash is remapped to one. */
static uint64_t cplus_type_hash(const char *name, size_t size_hint,
                                uint64_t seed) {
  uint64_t h = seed ? seed : UINT64_C(14695981039346656037);
  const char *s = name ? name : "";
  for (size_t i = 0u; i < CPLUS_TYPE_NAME_MAX && s[i]; ++i) {
    h ^= (uint64_t)(unsigned char)s[i];
    h *= UINT64_C(1099511628211);
  }
  h ^= (uint64_t)size_hint + UINT64_C(0x9e3779b97f4a7c15) + (h << 6) + (h >> 2);
  return h ? h : 1u;
}

cplus_type_tag cplus_type_tag_hash(const char *name, size_t size_hint) {
  return cplus_type_hash(name, size_hint, 0u);
}

cplus_type_tag cplus_type_tag_register(const char *name, size_t size_hint) {
  const char *s = name ? name : "";
  cplus_alloc_lock();
  for (size_t i = 0u; i < cplus_type_registry_count; ++i) {
    if (cplus_type_registry[i].size == size_hint &&
        strncmp(cplus_type_registry[i].name, s, CPLUS_TYPE_NAME_MAX) == 0) {
      uint64_t tag = cplus_type_registry[i].tag;
      cplus_alloc_unlock();
      return tag;
    }
  }
  cplus_type_tag tag = 0u;
  if (cplus_type_registry_count < CPLUS_TYPE_REGISTRY_MAX) {
    tag = cplus_type_hash(s, size_hint, 0u);
    int clash = 0;
    for (unsigned tries = 0u; tries < 8u; ++tries) {
      clash = 0;
      for (size_t i = 0u; i < cplus_type_registry_count; ++i) {
        if (cplus_type_registry[i].tag == tag) {
          clash = 1;
          break;
        }
      }
      if (!clash)
        break;
      tag = cplus_type_hash(s, size_hint, tag);
    }
    if (clash)
      tag = 0u;
    if (tag != 0u) {
      cplus_type_entry *e = &cplus_type_registry[cplus_type_registry_count++];
      memset(e, 0, sizeof(*e));
      e->tag = tag;
      e->size = size_hint;
      size_t n = 0u;
      while (n + 1u < CPLUS_TYPE_NAME_MAX && s[n])
        ++n;
      memcpy(e->name, s, n);
    }
  }
  cplus_alloc_unlock();
  return tag;
}

int cplus_type_tag_set(void *ptr, cplus_type_tag tag) {
  if (!ptr || tag == 0u)
    return 0;
  cplus_alloc_lock();
  int ok = 0;
  size_t index;
  uint32_t klass;
  if (cplus_lowfat_region_of(ptr, &index, &klass)) {
    cplus_lowfat_slot_meta *m = &cplus_lowfat_regions[klass].meta[index];
    if (cplus_lf_load_u32(&m->state) == 1u &&
        cplus_lowfat_slot_base(klass, index) == ptr) {
      cplus_lf_store_u32(&m->state, 0u);
      m->type_tag = tag;
      cplus_lf_store_u32(&m->state, 1u);
      ok = 1;
    }
  }
  if (!ok) {
    cplus_live_slot *slot = cplus_find_live(ptr);
    if (!slot)
      slot = cplus_shadow_find_locked(ptr);
    if (slot) {
      slot->type_tag = tag;
      /* An interior pointer into a low-fat slot resolves in the live table
       * via containment; mirror the tag into the O(1) metadata as well so
       * both lookup paths observe the same effective type. */
      size_t base_index;
      uint32_t base_class;
      if (cplus_lowfat_region_of(slot->ptr, &base_index, &base_class)) {
        cplus_lowfat_slot_meta *m =
            &cplus_lowfat_regions[base_class].meta[base_index];
        cplus_lf_store_u32(&m->state, 0u);
        m->type_tag = tag;
        cplus_lf_store_u32(&m->state, 1u);
      }
      ok = 1;
    }
  }
  cplus_alloc_unlock();
  return ok;
}

cplus_type_tag cplus_type_tag_get(const void *ptr) {
  if (!ptr)
    return CPLUS_TYPE_TAG_NONE;
  void *base = NULL;
  size_t size = 0u;
  uint64_t generation = 0u, tag = 0u;
  uint32_t klass = 0u;
  if (cplus_lowfat_resolve(ptr, &base, &size, &generation, &tag, &klass, NULL))
    return tag;
  cplus_alloc_lock();
  cplus_live_slot *slot = cplus_shadow_find_locked(ptr);
  uint64_t result = slot ? slot->type_tag : 0u;
  cplus_alloc_unlock();
  return result;
}

void cplus_type_tag_check(const void *ptr, cplus_type_tag expected,
                          const char *file, int line) {
  if (!ptr || expected == 0u)
    cplus_print_fatal(CPLUS_EXIT_TYPE_CONFUSION, "type confusion", file, line,
                      "effective type check needs a live pointer and a tag");
  cplus_type_tag actual = cplus_type_tag_get(ptr);
  if (actual != expected) {
    char msg[192];
    snprintf(msg, sizeof(msg),
             "effective type tag mismatch: got %llu, expected %llu",
             (unsigned long long)actual, (unsigned long long)expected);
    cplus_print_fatal(CPLUS_EXIT_TYPE_CONFUSION, "type confusion", file, line,
                      msg);
  }
}

int cplus_type_member_register(cplus_type_tag parent, const char *name,
                               size_t offset, size_t size,
                               cplus_type_tag member) {
  if (!parent || !member)
    return 0;
  const char *s = name ? name : "";
  cplus_alloc_lock();
  int ok = 0;
  if (cplus_type_member_count < CPLUS_TYPE_MEMBER_MAX) {
    cplus_type_member_entry *e =
        &cplus_type_members[cplus_type_member_count++];
    memset(e, 0, sizeof(*e));
    e->parent = parent;
    e->member = member;
    e->offset = offset;
    e->size = size;
    size_t n = 0u;
    while (n + 1u < CPLUS_TYPE_MEMBER_NAME_MAX && s[n])
      ++n;
    memcpy(e->name, s, n);
    ok = 1;
  }
  cplus_alloc_unlock();
  return ok;
}

void cplus_type_member_check(const void *ptr, cplus_type_tag parent,
                             cplus_type_tag member, size_t offset,
                             size_t bytes, const char *file, int line) {
  void *base = NULL;
  void *bound = NULL;
  size_t size = 0u;
  if (!cplus_lowfat_bounds(ptr, &base, &size)) {
    /* cplus_shadow_base_bound returns a one-past-the-end ADDRESS, not a
     * size_t length (the original call passed &size as void **). */
    if (!cplus_shadow_base_bound(ptr, &base, &bound))
      cplus_print_fatal(CPLUS_EXIT_TYPE_CONFUSION, "type confusion", file, line,
                        "member access on an untracked pointer");
    size = (size_t)((uintptr_t)bound - (uintptr_t)base);
  }
  cplus_type_tag actual = cplus_type_tag_get(base);
  if (actual != parent)
    cplus_print_fatal(CPLUS_EXIT_TYPE_CONFUSION, "type confusion", file, line,
                      "object effective type does not match downcast parent");
  if (offset > size || bytes > size - offset)
    cplus_print_fatal(CPLUS_EXIT_TYPE_CONFUSION, "type confusion", file, line,
                      "member access lies outside the object's bounds");
  cplus_alloc_lock();
  int found = 0, ok = 1;
  for (size_t i = 0u; i < cplus_type_member_count; ++i) {
    cplus_type_member_entry *e = &cplus_type_members[i];
    if (e->parent == parent && e->offset == offset) {
      found = 1;
      if (e->member != member || bytes > e->size)
        ok = 0;
      break;
    }
  }
  cplus_alloc_unlock();
  if (found && !ok)
    cplus_print_fatal(CPLUS_EXIT_TYPE_CONFUSION, "type confusion", file, line,
                      "member tag does not match the registered layout");
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
/* Windows has no pthreads; use CRITICAL_SECTION and the Win32 TLS API so
 * locks are real and TLS slots are per-thread rather than silent no-ops. */
typedef struct {
  CRITICAL_SECTION cs;
} cplus_win_mutex;
typedef struct {
  DWORD key;
} cplus_win_tls;

void *cplus_mutex_new(void) {
  cplus_win_mutex *m = (cplus_win_mutex *)malloc(sizeof(*m));
  if (!m)
    return NULL;
  InitializeCriticalSection(&m->cs);
  return m;
}
void cplus_mutex_lock(void *m) {
  if (m)
    EnterCriticalSection(&((cplus_win_mutex *)m)->cs);
}
void cplus_mutex_unlock(void *m) {
  if (m)
    LeaveCriticalSection(&((cplus_win_mutex *)m)->cs);
}
void cplus_mutex_free(void *m) {
  if (!m)
    return;
  DeleteCriticalSection(&((cplus_win_mutex *)m)->cs);
  free(m);
}
void *cplus_tls_new(void) {
  cplus_win_tls *s = (cplus_win_tls *)malloc(sizeof(*s));
  if (!s)
    return NULL;
  s->key = TlsAlloc();
  if (s->key == TLS_OUT_OF_INDEXES) {
    free(s);
    return NULL;
  }
  return s;
}
void *cplus_tls_get(void *slot) {
  return slot ? TlsGetValue(((cplus_win_tls *)slot)->key) : NULL;
}
void cplus_tls_set(void *slot, void *v) {
  if (slot)
    (void)TlsSetValue(((cplus_win_tls *)slot)->key, v);
}
void cplus_tls_free(void *slot) {
  if (!slot)
    return;
  TlsFree(((cplus_win_tls *)slot)->key);
  free(slot);
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

/* Each `print(...)` invocation owns one output buffer. Values are appended to
 * it and flushed by cplus_print_end(), so arguments evaluated while printing
 * can never interleave with this invocation's own output. The buffer grows on
 * demand (no silent truncation); if growth fails, the pending bytes and the
 * new chunk are written directly, which preserves data at the cost of the
 * interleaving guarantee in that out-of-memory case. */
#define CPLUS_PRINT_MAX_DEPTH 64

/* print() nests across calls on one thread; a process-global stack races and
 * can hand a buffer to another thread after a longjmp. */
#if defined(_MSC_VER)
#define CPLUS_THREAD_LOCAL __declspec(thread)
#else
#define CPLUS_THREAD_LOCAL __thread
#endif
static CPLUS_THREAD_LOCAL char *cplus_print_buf[CPLUS_PRINT_MAX_DEPTH];
static CPLUS_THREAD_LOCAL size_t cplus_print_cap[CPLUS_PRINT_MAX_DEPTH];
static CPLUS_THREAD_LOCAL size_t cplus_print_len[CPLUS_PRINT_MAX_DEPTH];
static CPLUS_THREAD_LOCAL int cplus_print_first[CPLUS_PRINT_MAX_DEPTH];
static CPLUS_THREAD_LOCAL int cplus_print_depth = 0;
/* Number of nested begin() calls that could not reserve a slot because all
 * CPLUS_PRINT_MAX_DEPTH slots were already in use.  Their matching end()
 * calls consume this counter instead of popping/flushing a real slot. */
static CPLUS_THREAD_LOCAL int cplus_print_overflow = 0;

/* Buffers are kept across print() calls on one thread to avoid realloc churn,
 * so a short-lived worker thread must still release its heap buffers when it
 * exits.  The key's value is a small heap record that mirrors every buffer
 * pointer: on some platforms the __thread arrays are already torn down when
 * TLS key destructors run, so the destructor must not read them. */
typedef struct cplus_print_thread_state {
  char *buf[CPLUS_PRINT_MAX_DEPTH];
} cplus_print_thread_state;
static CPLUS_THREAD_LOCAL cplus_print_thread_state *cplus_print_tls_state = NULL;

static void cplus_print_tls_drop(cplus_print_thread_state *state) {
  if (!state)
    return;
  for (int d = 0; d < CPLUS_PRINT_MAX_DEPTH; ++d)
    free(state->buf[d]);
  free(state);
}

#if defined(CPLUS_POSIX)
static pthread_key_t cplus_print_tls_key;
static int cplus_print_tls_key_ok = 0;
static pthread_once_t cplus_print_tls_once = PTHREAD_ONCE_INIT;

static void cplus_print_tls_cleanup(void *value) {
  cplus_print_tls_drop((cplus_print_thread_state *)value);
}
static void cplus_print_tls_init(void) {
  cplus_print_tls_key_ok =
      pthread_key_create(&cplus_print_tls_key, cplus_print_tls_cleanup) == 0;
}
static void cplus_print_tls_register(void) {
  if (cplus_print_tls_state)
    return;
  pthread_once(&cplus_print_tls_once, cplus_print_tls_init);
  if (!cplus_print_tls_key_ok)
    return;
  cplus_print_thread_state *state =
      (cplus_print_thread_state *)calloc(1u, sizeof(*state));
  if (!state)
    return;
  if (pthread_setspecific(cplus_print_tls_key, state) != 0) {
    free(state);
    return;
  }
  cplus_print_tls_state = state;
}
#elif defined(_WIN32)
static DWORD cplus_print_tls_index = FLS_OUT_OF_INDEXES;
static INIT_ONCE cplus_print_tls_once = INIT_ONCE_STATIC_INIT;

static void CALLBACK cplus_print_tls_cleanup(PVOID value) {
  cplus_print_tls_drop((cplus_print_thread_state *)value);
}
static BOOL CALLBACK cplus_print_tls_init(PINIT_ONCE once, PVOID param,
                                          PVOID *context) {
  (void)once;
  (void)param;
  (void)context;
  cplus_print_tls_index = FlsAlloc(cplus_print_tls_cleanup);
  return TRUE;
}
static void cplus_print_tls_register(void) {
  if (cplus_print_tls_state)
    return;
  InitOnceExecuteOnce(&cplus_print_tls_once, cplus_print_tls_init, NULL, NULL);
  if (cplus_print_tls_index == FLS_OUT_OF_INDEXES)
    return;
  cplus_print_thread_state *state =
      (cplus_print_thread_state *)calloc(1u, sizeof(*state));
  if (!state)
    return;
  if (!FlsSetValue(cplus_print_tls_index, (PVOID)state)) {
    free(state);
    return;
  }
  cplus_print_tls_state = state;
}
#else
static void cplus_print_tls_register(void) {} /* no TLS destructor support */
#endif

void cplus_print_begin(void) {
  cplus_print_tls_register();
  if (cplus_print_depth < CPLUS_PRINT_MAX_DEPTH) {
    int d = cplus_print_depth++;
    cplus_print_len[d] = 0;
    cplus_print_first[d] = 1;
  } else {
    ++cplus_print_overflow;
  }
}

static void cplus_print_append(int d, const char *text, size_t n) {
  if (n == 0)
    return;
  /* Fail closed before computing want: len + n + 1 can wrap around SIZE_MAX,
   * which would skip reallocation and overflow cplus_print_buf[d] in memcpy. */
  if (cplus_print_len[d] >= SIZE_MAX ||
      n > SIZE_MAX - 1u - cplus_print_len[d])
    cplus_print_fatal(CPLUS_EXIT_RESOURCE, "out of memory", NULL, 0,
                      "print buffer size overflow");
  size_t want = cplus_print_len[d] + n + 1u;
  if (want > cplus_print_cap[d]) {
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
    /* Keep the destructor's record in sync: on thread exit the __thread
     * arrays may already be gone when the key callback runs. */
    if (cplus_print_tls_state)
      cplus_print_tls_state->buf[d] = grown;
    cplus_print_cap[d] = cap;
  }
  memcpy(cplus_print_buf[d] + cplus_print_len[d], text, n);
  cplus_print_len[d] += n;
  cplus_print_buf[d][cplus_print_len[d]] = '\0';
}

void cplus_print_end(void) {
  if (cplus_print_overflow > 0) {
    /* This end matches a begin that reserved no slot; flushing slot 63 here
     * would clobber the enclosing print()'s buffered output. */
    --cplus_print_overflow;
    return;
  }
  if (cplus_print_depth > 0) {
    int d = cplus_print_depth - 1;
    if (cplus_print_len[d] > 0) {
      fwrite(cplus_print_buf[d], 1, cplus_print_len[d], stdout);
    }
    putchar('\n');
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

/* Floating point scanning is token-based like the integer paths: scanf("%f")
 * accepts NaN/Inf, hides ERANGE on some libcs, and happily stores a numeric
 * prefix of "42xyz" while leaving "xyz" to desynchronise the next scan. */
static void cplus_scan_float_checked(void *dest, size_t size, const char *expr,
                                     const char *file, int line) {
  char token[128];
  char detail[384];
  int rc = scanf("%127s", token);
  char *end = NULL;
  float v;
  const char *target = (expr && *expr) ? expr : "variable";
  (void)size; /* caller checks the destination extent before this call */
  if (rc != 1) {
    snprintf(detail, sizeof(detail),
             "could not read input for '%s' (float) - input ended unexpectedly",
             target);
    cplus_print_fatal(CPLUS_EXIT_IO, "input error", file, line, detail);
  }
  errno = 0;
  v = strtof(token, &end);
  if (end == token || *end != '\0') {
    snprintf(detail, sizeof(detail),
             "cannot store '%s' into '%s' (float) - expected a valid floating-point number, not text/extra characters",
             token, target);
    cplus_print_fatal(CPLUS_EXIT_IO, "input error", file, line, detail);
  }
  if (errno == ERANGE || !isfinite((double)v)) {
    snprintf(detail, sizeof(detail),
             "cannot store '%s' into '%s' (float) - expected a finite representable number, not NaN/Infinity or an out-of-range value",
             token, target);
    cplus_print_fatal(CPLUS_EXIT_CONVERSION, "input overflow", file, line,
                      detail);
  }
  *(float *)dest = v;
}

static void cplus_scan_double_checked(void *dest, size_t size, const char *expr,
                                      const char *file, int line) {
  char token[128];
  char detail[384];
  int rc = scanf("%127s", token);
  char *end = NULL;
  double v;
  const char *target = (expr && *expr) ? expr : "variable";
  (void)size; /* caller checks the destination extent before this call */
  if (rc != 1) {
    snprintf(detail, sizeof(detail),
             "could not read input for '%s' (double) - input ended unexpectedly",
             target);
    cplus_print_fatal(CPLUS_EXIT_IO, "input error", file, line, detail);
  }
  errno = 0;
  v = strtod(token, &end);
  if (end == token || *end != '\0') {
    snprintf(detail, sizeof(detail),
             "cannot store '%s' into '%s' (double) - expected a valid floating-point number, not text/extra characters",
             token, target);
    cplus_print_fatal(CPLUS_EXIT_IO, "input error", file, line, detail);
  }
  if (errno == ERANGE || !isfinite(v)) {
    snprintf(detail, sizeof(detail),
             "cannot store '%s' into '%s' (double) - expected a finite representable number, not NaN/Infinity or an out-of-range value",
             token, target);
    cplus_print_fatal(CPLUS_EXIT_CONVERSION, "input overflow", file, line,
                      detail);
  }
  *(double *)dest = v;
}

/* Common fail-closed path for a destination that cannot hold the scanned
 * value.  Never truncate: a partial token would silently corrupt the caller
 * and leave the rest of the input for the next scan. */
static void cplus_scan_bad_size(cplus_exit_code code, void *dest, size_t size,
                                const char *target, const char *file,
                                int line) {
  char detail[256];
  snprintf(detail, sizeof(detail),
           "scan destination '%s' (%p) has %zu bytes; not enough space for "
           "the scanned value",
           target, dest, size);
  cplus_print_fatal(code, "input error", file, line, detail);
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
    if (size < sizeof(float))
      cplus_scan_bad_size(CPLUS_EXIT_BOUNDS, dest, size, target, file, line);
    cplus_scan_float_checked(dest, size, target, file, line);
    return;
  case CPLUS_SCAN_DOUBLE:
    if (size < sizeof(double))
      cplus_scan_bad_size(CPLUS_EXIT_BOUNDS, dest, size, target, file, line);
    cplus_scan_double_checked(dest, size, target, file, line);
    return;
  case CPLUS_SCAN_CHAR:
    if (size < sizeof(char))
      cplus_scan_bad_size(CPLUS_EXIT_BOUNDS, dest, size, target, file, line);
    rc = scanf(" %c", (char *)dest);
    break;
  case CPLUS_SCAN_STR: {
    char token[4096];
    rc = scanf("%4095s", token);
    if (rc != 1)
      break;
    size_t n = strlen(token);
    /* A token that filled the buffer continues on stdin; consume the rest so
     * the next scan cannot desynchronise on the tail of this one. */
    if (n == sizeof(token) - 1u) {
      int ch;
      while ((ch = fgetc(stdin)) != EOF && ch != ' ' && ch != '\t' &&
             ch != '\n' && ch != '\r' && ch != '\f' && ch != '\v')
        ;
    }
    /* Fail closed instead of silently truncating into a smaller buffer. */
    if (size == 0 || n + 1u > size)
      cplus_scan_bad_size(CPLUS_EXIT_BOUNDS, dest, size, target, file, line);
    memcpy(dest, token, n + 1u);
    break;
  }
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
