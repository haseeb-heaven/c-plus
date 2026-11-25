// Numeric and conversion bugs 625-634:
// widening/narrowing in array indexing, FP NaN comparisons, fmod by zero,
// fegetround misuse, and integer promotion inside bit masks.
// These functions are intentionally never called from main().
#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// BUG 625: A widened 64-bit index used directly on a small array can exceed
//          the array bound (the index is not narrowed back to a safe size).
// ============================================================================
void bug625_widening_index_oob(long index) {
  int arr[4] = {1, 2, 3, 4};
  // BUG: no bounds check; a value such as 9 indexes past the array.
  int value = arr[index];
  // SAFE FIX: Validate `0 <= index && index < 4` before subscripting.
  (void)value;
}

// ============================================================================
// BUG 626: Comparing a floating value with NaN using `!=` is always true.
// ============================================================================
void bug626_nan_inequality(double v) {
  double nan_value = NAN; // indirect so the catalog itself stays buildable
  if (v != nan_value) {   // BUG: always true, even when v is NaN.
    puts("different");
  }
  // SAFE FIX: Use `if (!isnan(v))` for the intended test.
}

// ============================================================================
// BUG 627: The idiom `v != v` is meant to test NaN but can be optimised away
//          and is unreadable; FP comparison does not behave like integers.
// ============================================================================
void bug627_nan_self_compare(double v) {
  if (v != v) { // BUG: relies on IEEE NaN semantics; use isnan().
    puts("nan");
  }
  // SAFE FIX: Use `if (isnan(v))` which is explicit and robust.
}

// ============================================================================
// BUG 628: fmod with a zero divisor returns NaN and raises no trap.
// ============================================================================
void bug628_fmod_zero_literal(void) {
  double r = fmod(1.0, 0.0); // BUG: domain error; result is NaN.
  // SAFE FIX: Reject a zero divisor before calling fmod.
  (void)r;
}

// ============================================================================
// BUG 629: fmod divisor computed from input may be zero.
// ============================================================================
void bug629_fmod_input_zero(const char *text) {
  double divisor = strtod(text, NULL); // may be 0.0
  double r = fmod(42.0, divisor);      // BUG: NaN when divisor == 0.
  // SAFE FIX: Check `divisor != 0.0` (and isfinite) before fmod.
  (void)r;
}

// ============================================================================
// BUG 630: remainder() with a zero divisor has the same domain problem.
// ============================================================================
void bug630_remainder_zero(void) {
  double r = remainder(3.0, 0.0); // BUG: NaN / domain error.
  // SAFE FIX: Validate the divisor before calling remainder().
  (void)r;
}

// ============================================================================
// BUG 631: Assuming FE_TONEAREST is numeric zero; the FE_* constants are
//          implementation-defined bitmasks, not small ordinal numbers.
// ============================================================================
void bug631_fegetround_assume_zero(void) {
  int mode = fegetround();
  if (mode == 0) { // BUG: FE_TONEAREST need not be 0.
    puts("nearest");
  }
  // SAFE FIX: Compare against the FE_TONEAREST macro itself.
}

// ============================================================================
// BUG 632: Testing the fegetround() result with a bitmask that does not
//          correspond to a single rounding-mode constant.
// ============================================================================
void bug632_fegetround_mismatch(void) {
  int mode = fegetround();
  if ((mode & 0x3) == FE_DOWNWARD) { // BUG: masks away mode bits.
    puts("down");
  }
  // SAFE FIX: Compare the unmasked result to FE_DOWNWARD directly.
}

// ============================================================================
// BUG 633: Integer promotion when left-shifting a uint8_t by 24; the value
//          is promoted to `int` and the sign bit can be set (UB).
// ============================================================================
void bug633_promotion_mask_shift(uint8_t byte) {
  uint32_t word = (uint32_t)(byte << 24); // BUG: byte promotes to int.
  // SAFE FIX: Cast to uint32_t before shifting: ((uint32_t)byte << 24).
  (void)word;
}

// ============================================================================
// BUG 634: Sign extension when forming a mask with `~` on a promoted
//          small integer produces unexpected high bits.
// ============================================================================
void bug634_promotion_mask_complement(uint8_t bits) {
  uint32_t mask = ~bits; // BUG: ~ promotes to int, sign-extends.
  // SAFE FIX: Use unsigned arithmetic: `~(uint32_t)bits`.
  (void)mask;
}

int main(void) { return 0; }
