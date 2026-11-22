// Numeric and conversion bugs 645-654:
// mixed numeric/conversion mistakes: NaN comparisons, overflow in
// arithmetic and allocation size, divide/modulo and shift errors, and
// string-to-integer conversion pitfalls.
// These functions are intentionally never called from main().
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// BUG 645: Comparing a value with NaN using `!=` (always true).
// ============================================================================
void bug645_nan_inequality(double v) {
  double nan_value = NAN; // indirect so the catalog itself stays buildable
  if (nan_value != v) {   // BUG: always true, even when v is NaN.
    puts("different");
  }
  // SAFE FIX: Use `if (!isnan(v))` instead of comparing against NaN.
}

// ============================================================================
// BUG 646: Using NaN in an ordered comparison chain; every comparison is
//          false and the intended branch is never taken.
// ============================================================================
void bug646_nan_in_chain(double x) {
  double nan_value = NAN;
  if (x < nan_value || x > nan_value) { // BUG: both tests false for NaN.
    puts("in range");
  }
  // SAFE FIX: Reject NaN up front with isnan(), then compare.
}

// ============================================================================
// BUG 647: Signed overflow converting seconds to milliseconds in int.
// ============================================================================
void bug647_signed_overflow_time(int seconds) {
  int ms = seconds * 1000; // BUG: overflows for seconds > ~2.1 million.
  // SAFE FIX: Compute in int64_t and range-check before narrowing.
  (void)ms;
}

// ============================================================================
// BUG 648: Unsigned wrap when computing an allocation size.
// ============================================================================
void bug648_unsigned_array_size_wrap(size_t n) {
  size_t bytes = n * sizeof(int); // BUG: wraps; malloc gets a tiny size.
  // SAFE FIX: Check `n > SIZE_MAX / sizeof(int)` before multiplying.
  void *p = malloc(bytes);
  free(p);
}

// ============================================================================
// BUG 649: Division by a divisor obtained from command-line input.
// ============================================================================
void bug649_div_zero_from_argv(const char *arg) {
  int divisor = atoi(arg); // may be 0
  int value = 100 / divisor; // BUG: UB when divisor == 0.
  // SAFE FIX: Check divisor != 0 before dividing.
  (void)value;
}

// ============================================================================
// BUG 650: Relying on the sign of the result of modulo with a negative
//          operand; C specifies truncation toward zero, not floor.
// ============================================================================
void bug650_mod_negative(void) {
  int r = -7 % 3; // BUG: result sign is implementation-defined in old C.
  // SAFE FIX: Normalise with `((x % m) + m) % m` when m > 0.
  (void)r;
}

// ============================================================================
// BUG 651: Shift count taken from input without validation.
// ============================================================================
void bug651_shift_count_from_input(const char *arg) {
  int amount = atoi(arg);
  uint32_t value = 1u << amount; // BUG: UB for amount < 0 or >= 32.
  // SAFE FIX: Validate 0 <= amount < 32 before shifting.
  (void)value;
}

// ============================================================================
// BUG 652: strtol result used without checking errno for ERANGE.
// ============================================================================
void bug652_strtol_errno_ignored(const char *text) {
  errno = 0;
  long v = strtol(text, NULL, 10); // BUG: ERANGE may be set.
  // SAFE FIX: Check `errno != ERANGE` and the end pointer after strtol.
  (void)v;
}

// ============================================================================
// BUG 653: strtoul with base 0 on a leading-zero decimal string.
// ============================================================================
void bug653_strtoul_base0_leading_zero(void) {
  const char *text = "0755";
  unsigned long v = strtoul(text, NULL, 0); // BUG: parsed as octal (493).
  // SAFE FIX: Pass base 10 when the input is decimal with leading zeros.
  (void)v;
}

// ============================================================================
// BUG 654: atol on an overflowing string has undefined behaviour.
// ============================================================================
void bug654_atol_overflow(void) {
  const char *text = "92233720368547758080";
  long v = atol(text); // BUG: out of long range; no error signal.
  // SAFE FIX: Use strtol with errno/ERANGE checks.
  (void)v;
}

int main(void) { return 0; }
