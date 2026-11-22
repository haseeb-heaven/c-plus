// Numeric and conversion bugs 635-644:
// size_t underflow in subtraction and casts to smaller integer types before
// comparison (signedness/sign-extension logic errors).
// These functions are intentionally never called from main().
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// BUG 635: A negative index computed at runtime applied to a heap buffer.
// ============================================================================
void bug635_heap_negative_index(int index) {
  int *buf = malloc(4 * sizeof(int));
  if (buf) {
    // BUG: a negative index (e.g. -1) walks before the allocation.
    int value = buf[index];
    // SAFE FIX: Validate 0 <= index && index < 4 before subscripting.
    (void)value;
    free(buf);
  }
}

// ============================================================================
// BUG 636: size_t capacity subtraction underflows when used < reserved.
// ============================================================================
void bug636_size_t_sub_underflow(size_t used, size_t reserved) {
  size_t capacity = used - reserved; // BUG: wraps to SIZE_MAX when used<reserved
  // SAFE FIX: Check `used >= reserved` before subtracting.
  (void)capacity;
}

// ============================================================================
// BUG 637: Subtracting a prefix length from strlen() result underflows on
//          strings shorter than the prefix.
// ============================================================================
void bug637_strlen_prefix_underflow(const char *s) {
  size_t len = strlen(s);
  size_t suffix = len - 4u; // BUG: wraps when len < 4.
  // SAFE FIX: Check `len >= 4` first, or use a saturating subtraction.
  (void)suffix;
}

// ============================================================================
// BUG 638: Casting a long to short before comparison flips a large positive
//          value into a negative one.
// ============================================================================
void bug638_cast_long_to_short_compare(long value) {
  if ((short)value > 0) { // BUG: 70000 casts to 4464, may be negative.
    puts("positive");
  }
  // SAFE FIX: Compare the original `value`, not a narrowed copy.
}

// ============================================================================
// BUG 639: Casting an unsigned bit pattern to int before a > 0 test yields
//          a negative value for high-bit patterns.
// ============================================================================
void bug639_cast_unsigned_to_signed_compare(unsigned pattern) {
  if ((int)pattern > 0) { // BUG: 0xFFFFFFFF becomes -1.
    puts("positive");
  }
  // SAFE FIX: Compare the unsigned value directly against 0u.
}

// ============================================================================
// BUG 640: Depending on plain `char` signedness when comparing a byte that
//          has the high bit set.
// ============================================================================
void bug640_char_signedness_compare(unsigned char byte) {
  char c = (char)byte;
  if (c > 0) { // BUG: false for 0xFF on signed-char platforms.
    puts("positive");
  }
  // SAFE FIX: Compare the unsigned char directly.
}

// ============================================================================
// BUG 641: A negative int compared with an unsigned int converts the int to
//          a huge unsigned value, reversing the test.
// ============================================================================
void bug641_negative_vs_unsigned_compare(int negative, unsigned small) {
  if (negative < small) { // BUG: -1 becomes UINT_MAX, so test is false.
    puts("less");
  }
  // SAFE FIX: Compare as signed after validating both operands.
}

// ============================================================================
// BUG 642: Truncating a float to int before comparing changes the result.
// ============================================================================
void bug642_float_to_int_trunc_compare(float f) {
  if ((int)f == 2) { // BUG: 1.9f truncates to 1.
    puts("two");
  }
  // SAFE FIX: Compare the floating value with a tolerance.
}

// ============================================================================
// BUG 643: Narrowing a double to float before equality comparison loses
//          precision and gives a false result.
// ============================================================================
void bug643_double_to_float_narrow_compare(double d) {
  if ((float)d == 0.1) { // BUG: 0.1 double narrows unequally to float.
    puts("equal");
  }
  // SAFE FIX: Compare in the original type with an epsilon.
}

// ============================================================================
// BUG 644: Casting a large unsigned value to int before a < 0 test.
// ============================================================================
void bug644_uint_to_int_wrap_compare(unsigned value) {
  if ((int)value < 0) { // BUG: 0xF0000000 becomes a negative int.
    puts("negative");
  }
  // SAFE FIX: Test the high bit using an unsigned mask instead.
}

int main(void) { return 0; }
