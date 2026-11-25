// Numeric and conversion bugs 605-614:
// signed overflow, unsigned loop underflow, invalid shift counts,
// division/modulo by zero from input, and FP comparison with NaN.
// These functions are intentionally never called from main().
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// BUG 605: Signed addition overflow when incrementing INT_MAX.
// ============================================================================
void bug605_signed_add_overflow(void) {
  int x = INT_MAX;
  int y = x + 1; // UB: signed overflow wraps/UB.
  // SAFE FIX: Use int64_t or guard with `if (x == INT_MAX)` before adding.
  (void)y;
}

// ============================================================================
// BUG 606: Signed multiplication overflow computing a rectangle area.
// ============================================================================
void bug606_signed_mul_overflow(int width, int height) {
  int area = width * height; // UB when product exceeds INT_MAX.
  // SAFE FIX: Widen to int64_t or check width > INT_MAX / height first.
  (void)area;
}

// ============================================================================
// BUG 607: Negating INT_MIN is undefined (the value is not representable).
// ============================================================================
void bug607_negate_int_min(void) {
  int x = INT_MIN;
  int y = -x; // UB: -INT_MIN overflows.
  // SAFE FIX: Promote to int64_t before negating, or reject INT_MIN.
  (void)y;
}

// ============================================================================
// BUG 608: Signed subtraction overflow at the lower bound.
// ============================================================================
void bug608_signed_sub_overflow(void) {
  int x = INT_MIN;
  int y = x - 1; // UB: result is below INT_MIN.
  // SAFE FIX: Check `x == INT_MIN` before decrementing or use a wider type.
  (void)y;
}

// ============================================================================
// BUG 609: Unsigned underflow in a descending loop bound: `i >= 0` is
//          always true for an unsigned counter, producing an infinite loop.
// ============================================================================
void bug609_unsigned_loop_underflow(size_t n) {
  for (size_t i = n; i >= 0; --i) { // BUG: i >= 0 is always true.
    (void)i;
  }
  // SAFE FIX: Loop with `for (size_t i = n; i-- > 0;)` or use `i != SIZE_MAX`.
}

// ============================================================================
// BUG 610: Countdown loop wraps at zero because the condition uses `<` on
//          an unsigned index that is decremented past zero.
// ============================================================================
void bug610_unsigned_countdown_wrap(unsigned n) {
  unsigned i = n;
  while (i-- < n) { // BUG: after i wraps to UINT_MAX the condition stays true.
    (void)i;
  }
  // SAFE FIX: Use `while (i > 0) { --i; ... }` so zero terminates the loop.
}

// ============================================================================
// BUG 611: Left-shifting a 32-bit value by the width of the type (32).
// ============================================================================
void bug611_shift_left_ge_width(unsigned count) {
  uint32_t one = 1u;
  uint32_t bad = one << count; // UB when count >= 32 (e.g. 32).
  // SAFE FIX: Validate count < 32, or use a 64-bit type before shifting.
  (void)bad;
}

// ============================================================================
// BUG 612: Right-shifting by a negative count computed from input.
// ============================================================================
void bug612_shift_right_negative(int amount) {
  int value = 1024;
  int bad = value >> amount; // UB when amount < 0.
  // SAFE FIX: Validate `0 <= amount < 32` before shifting.
  (void)bad;
}

// ============================================================================
// BUG 613: Division by a divisor derived from user input that may be zero.
// ============================================================================
void bug613_div_by_input_zero(int divisor) {
  int value = 100 / divisor; // UB when divisor == 0.
  // SAFE FIX: Check `if (divisor == 0) return;` before dividing.
  (void)value;
}

// ============================================================================
// BUG 614: FP comparison with NaN. A NaN is never equal to anything, so a
//          direct `==`/`!=` test silently takes the wrong branch.
// ============================================================================
void bug614_nan_equality(void) {
  double v = 0.0;
  double nan_value = NAN; // indirect so the catalog itself stays buildable
  if (v == nan_value) {   // BUG: always false; use isnan().
    puts("equal");
  }
  // SAFE FIX: Use `if (isnan(v))` instead of comparing against NaN.
}

int main(void) { return 0; }
