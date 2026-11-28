/*===-- test_checked_arithmetic.c - checked helper regression tests --------===
 *
 * Direct, standalone tests for the runtime checked-arithmetic helpers.  The
 * C+ compiler emits inline fast paths for these operations, so calling the
 * runtime symbols directly is the only way to pin down their exact semantics
 * (including signedness and the fatal exit codes).  Each expected-failure
 * case runs in a child process: a SIGFPE or an unexpected trap is reported as
 * a test failure instead of taking down the whole test run.
 *
 *===----------------------------------------------------------------------===*/

#include "cplus/cplus.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures = 0;

static void fail(const char *name, const char *detail) {
  printf("  FAIL %s: %s\n", name, detail);
  failures++;
}

typedef int64_t (*binary_fn)(int64_t, int64_t, unsigned, unsigned);
typedef int64_t (*narrow_fn)(uint64_t, unsigned, unsigned, unsigned, unsigned);

static void expect_i64(const char *name, int64_t got, int64_t want) {
  if (got != want) {
    char buf[160];
    snprintf(buf, sizeof(buf), "got %" PRId64 ", want %" PRId64, got, want);
    fail(name, buf);
  }
}

static void expect_u64(const char *name, uint64_t got, uint64_t want) {
  if (got != want) {
    char buf[160];
    snprintf(buf, sizeof(buf), "got 0x%" PRIx64 ", want 0x%" PRIx64, got, want);
    fail(name, buf);
  }
}

/* Run a helper in a child and require a controlled fatal exit.  This also
 * catches the SIGFPE from issue #64, where the unsigned path used idiv. */
static void expect_fatal_binary(const char *name, binary_fn fn, uint64_t a,
                                uint64_t b, unsigned bits, unsigned isSigned,
                                int expected_code) {
  fflush(stdout);
  pid_t pid = fork();
  if (pid < 0) {
    fail(name, "fork failed");
    return;
  }
  if (pid == 0) {
    (void)fn((int64_t)a, (int64_t)b, bits, isSigned);
    _Exit(0);
  }
  int status = 0;
  if (waitpid(pid, &status, 0) != pid) {
    fail(name, "waitpid failed");
    return;
  }
  if (!WIFEXITED(status)) {
    char buf[160];
    snprintf(buf, sizeof(buf), "child died without exiting (status 0x%x)",
             status);
    fail(name, buf);
  } else if (WEXITSTATUS(status) != expected_code) {
    char buf[160];
    snprintf(buf, sizeof(buf), "exit %d, want %d", WEXITSTATUS(status),
             expected_code);
    fail(name, buf);
  }
}

static void expect_fatal_narrow(const char *name, uint64_t value, unsigned sb,
                                unsigned ss, unsigned db, unsigned ds) {
  fflush(stdout);
  pid_t pid = fork();
  if (pid < 0) {
    fail(name, "fork failed");
    return;
  }
  if (pid == 0) {
    (void)__cplus_checked_narrow_i64(value, sb, ss, db, ds);
    _Exit(0);
  }
  int status = 0;
  if (waitpid(pid, &status, 0) != pid) {
    fail(name, "waitpid failed");
    return;
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != CPLUS_EXIT_CONVERSION) {
    char buf[160];
    snprintf(buf, sizeof(buf), "status 0x%x, want exit %d", status,
             CPLUS_EXIT_CONVERSION);
    fail(name, buf);
  }
}

/* Issue #64: unsigned division/remainder must use unsigned arithmetic. */
static void test_divrem(void) {
  expect_u64("div u64 max/2",
             (uint64_t)__cplus_checked_div_i64((int64_t)UINT64_MAX, 2, 64, 0),
             UINT64_MAX / 2);
  expect_u64("rem u64 max%2",
             (uint64_t)__cplus_checked_rem_i64((int64_t)UINT64_MAX, 2, 64, 0),
             UINT64_MAX % 2);
  /* The original signed idiv here trapped with SIGFPE. */
  expect_u64("div u64 high/max",
             (uint64_t)__cplus_checked_div_i64((int64_t)(UINT64_C(1) << 63),
                                               (int64_t)UINT64_MAX, 64, 0),
             0u);
  expect_u64("rem u64 high%max",
             (uint64_t)__cplus_checked_rem_i64((int64_t)(UINT64_C(1) << 63),
                                               (int64_t)UINT64_MAX, 64, 0),
             UINT64_C(1) << 63);
  expect_u64("div u64 max/max",
             (uint64_t)__cplus_checked_div_i64((int64_t)UINT64_MAX,
                                               (int64_t)UINT64_MAX, 64, 0),
             1u);
  expect_u64("rem u64 max%max",
             (uint64_t)__cplus_checked_rem_i64((int64_t)UINT64_MAX,
                                               (int64_t)UINT64_MAX, 64, 0),
             0u);
  expect_u64("div u32 max/2",
             (uint64_t)__cplus_checked_div_i64((int64_t)UINT32_MAX, 2, 32, 0),
             UINT32_MAX / 2);
  expect_u64("rem u32 max%2",
             (uint64_t)__cplus_checked_rem_i64((int64_t)UINT32_MAX, 2, 32, 0),
             UINT32_MAX % 2);
  expect_i64("div signed -7/2",
             __cplus_checked_div_i64(-7, 2, 64, 1), -3);
  expect_i64("rem signed -7%2",
             __cplus_checked_rem_i64(-7, 2, 64, 1), -1);
  expect_i64("div signed min/2",
             __cplus_checked_div_i64(INT64_MIN, 2, 64, 1), INT64_MIN / 2);

  expect_fatal_binary("div u64 by zero", __cplus_checked_div_i64, 42, 0, 64, 0,
                      CPLUS_EXIT_ARITHMETIC);
  expect_fatal_binary("rem u64 by zero", __cplus_checked_rem_i64, 42, 0, 64, 0,
                      CPLUS_EXIT_ARITHMETIC);
  expect_fatal_binary("div signed min/-1", __cplus_checked_div_i64,
                      (uint64_t)INT64_MIN, (uint64_t)-1, 64, 1,
                      CPLUS_EXIT_ARITHMETIC);
  expect_fatal_binary("rem signed min/-1", __cplus_checked_rem_i64,
                      (uint64_t)INT64_MIN, (uint64_t)-1, 64, 1,
                      CPLUS_EXIT_ARITHMETIC);
  expect_fatal_binary("div signed i32 min/-1", __cplus_checked_div_i64,
                      (uint64_t)(int64_t)INT32_MIN, (uint64_t)-1, 32, 1,
                      CPLUS_EXIT_ARITHMETIC);
  expect_fatal_binary("rem signed i32 min/-1", __cplus_checked_rem_i64,
                      (uint64_t)(int64_t)INT32_MIN, (uint64_t)-1, 32, 1,
                      CPLUS_EXIT_ARITHMETIC);
}

/* Issue #65: unsigned right shifts are logical; signed left shifts reject a
 * bit reaching the sign bit at every width. */
static void test_shift(void) {
  expect_u64("shr u64 max>>1",
             (uint64_t)__cplus_checked_shr_i64((int64_t)UINT64_MAX, 1, 64, 0),
             UINT64_MAX >> 1);
  expect_u64("shr u64 high>>63",
             (uint64_t)__cplus_checked_shr_i64((int64_t)(UINT64_C(1) << 63),
                                               63, 64, 0),
             1u);
  expect_u64("shr u32 max>>1",
             (uint64_t)__cplus_checked_shr_i64((int64_t)UINT32_MAX, 1, 32, 0),
             UINT32_MAX >> 1);
  expect_u64("shr u32 max>>31",
             (uint64_t)__cplus_checked_shr_i64((int64_t)UINT32_MAX, 31, 32, 0),
             1u);
  expect_i64("shr signed -8>>2",
             __cplus_checked_shr_i64(-8, 2, 64, 1), -2);
  expect_i64("shr signed -1>>63",
             __cplus_checked_shr_i64(-1, 63, 64, 1), -1);
  expect_u64("shl u64 max<<1",
             (uint64_t)__cplus_checked_shl_i64((int64_t)UINT64_MAX, 1, 64, 0),
             UINT64_MAX << 1);
  expect_i64("shl signed 1<<30", __cplus_checked_shl_i64(1, 30, 32, 1),
             INT64_C(1) << 30);
  expect_i64("shl signed 1<<62", __cplus_checked_shl_i64(1, 62, 64, 1),
             INT64_C(1) << 62);
  expect_i64("shl signed 0<<63", __cplus_checked_shl_i64(0, 63, 64, 1), 0);

  expect_fatal_binary("shl signed 1<<31", __cplus_checked_shl_i64, 1, 31, 32,
                      1, CPLUS_EXIT_ARITHMETIC);
  expect_fatal_binary("shl signed 2<<30", __cplus_checked_shl_i64, 2, 30, 32,
                      1, CPLUS_EXIT_ARITHMETIC);
  expect_fatal_binary("shl signed 1<<63", __cplus_checked_shl_i64, 1, 63, 64,
                      1, CPLUS_EXIT_ARITHMETIC);
  expect_fatal_binary("shl signed (1<<60)<<10", __cplus_checked_shl_i64,
                      (uint64_t)(INT64_C(1) << 60), 10, 64, 1,
                      CPLUS_EXIT_ARITHMETIC);
  expect_fatal_binary("shl signed negative", __cplus_checked_shl_i64,
                      (uint64_t)-1, 1, 64, 1, CPLUS_EXIT_ARITHMETIC);
  expect_fatal_binary("shl count == width", __cplus_checked_shl_i64, 1, 64, 64,
                      1, CPLUS_EXIT_ARITHMETIC);
  expect_fatal_binary("shr negative count", __cplus_checked_shr_i64, 1,
                      (uint64_t)-1, 64, 0, CPLUS_EXIT_ARITHMETIC);
}

/* Issue #66: 64-bit signed values must be sign-extended, and lossy
 * signed/unsigned 64-bit conversions must be rejected. */
static void test_narrow(void) {
  expect_i64("narrow i64 -1 -> i32",
             __cplus_checked_narrow_i64(UINT64_MAX, 64, 1, 32, 1), -1);
  expect_i64("narrow i32 -1 -> i64",
             __cplus_checked_narrow_i64((uint64_t)(int64_t)(int32_t)-1, 32, 1,
                                        64, 1),
             -1);
  expect_i64("narrow i64 -1 -> i64",
             __cplus_checked_narrow_i64(UINT64_MAX, 64, 1, 64, 1), -1);
  expect_i64("narrow i32 min -> i64",
             __cplus_checked_narrow_i64(
                 (uint64_t)(int64_t)(int32_t)INT32_MIN, 32, 1, 64, 1),
             (int64_t)INT32_MIN);
  expect_i64("narrow i64 42 -> i32",
             __cplus_checked_narrow_i64(42, 64, 1, 32, 1), 42);
  expect_u64("narrow u64 max -> u64",
             (uint64_t)__cplus_checked_narrow_i64(UINT64_MAX, 64, 0, 64, 0),
             UINT64_MAX);
  expect_u64("narrow u32 max -> u64",
             (uint64_t)__cplus_checked_narrow_i64(UINT32_MAX, 32, 0, 64, 0),
             UINT32_MAX);

  expect_fatal_narrow("narrow i64 -1 -> u64", UINT64_MAX, 64, 1, 64, 0);
  expect_fatal_narrow("narrow u64 max -> i64", UINT64_MAX, 64, 0, 64, 1);
  expect_fatal_narrow("narrow i64 min -> i32", (uint64_t)INT64_MIN, 64, 1, 32,
                      1);
  expect_fatal_narrow("narrow u64 max -> u32", UINT64_MAX, 64, 0, 32, 0);
}

int main(void) {
  test_divrem();
  test_shift();
  test_narrow();
  if (failures)
    return 1;
  printf("checked_arithmetic: ok\n");
  return 0;
}
