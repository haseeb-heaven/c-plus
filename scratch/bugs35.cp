// Numeric and conversion bugs 615-624:
// integer-to-pointer and pointer-to-integer truncation, widening/narrowing in
// array indexing, strtol base confusion, and atoi overflow/misparse.
// These functions are intentionally never called from main().
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// BUG 615: Forging a pointer from a small integer literal.
// ============================================================================
void bug615_int_to_ptr_literal(void) {
  int *p = (int *)(uintptr_t)0x1234u; // BUG: unverifiable provenance.
  // SAFE FIX: Only cast integers that came from a real pointer (round trip).
  (void)p;
}

// ============================================================================
// BUG 616: Truncating a 64-bit address through a 32-bit integer on the way
//          back to a pointer.
// ============================================================================
void bug616_int_to_ptr_truncation(void) {
  uintptr_t addr = (uintptr_t)0x00007fff12345678ULL;
  uint32_t narrow = (uint32_t)addr; // BUG: drops the high 32 bits.
  void *p = (void *)(uintptr_t)narrow;
  // SAFE FIX: Keep the full uintptr_t; never shrink a pointer-sized integer.
  (void)p;
}

// ============================================================================
// BUG 617: Storing a pointer in a 16-bit integer loses all high bits.
// ============================================================================
void bug617_ptr_to_int_short(void) {
  int object = 7;
  short s = (short)(uintptr_t)&object; // BUG: massive truncation.
  // SAFE FIX: Use uintptr_t for any integer representation of a pointer.
  (void)s;
}

// ============================================================================
// BUG 618: Storing a pointer in an `unsigned int` truncates on 64-bit hosts.
// ============================================================================
void bug618_ptr_to_int_unsigned(void) {
  char buffer[8];
  unsigned int u = (unsigned int)(uintptr_t)buffer; // BUG: 64 -> 32 bits.
  // SAFE FIX: Store in uintptr_t, not unsigned int.
  (void)u;
}

// ============================================================================
// BUG 619: An unsigned index underflows below zero and is used to subscript
//          an array, wrapping to a huge value.
// ============================================================================
void bug619_unsigned_underflow_index(unsigned u) {
  int arr[4] = {1, 2, 3, 4};
  unsigned idx = u - 1u; // BUG: wraps to UINT_MAX when u == 0.
  // SAFE FIX: Check `u > 0` before subtracting, then use a bounds check.
  if (idx < 4u)
    (void)arr[idx];
}

// ============================================================================
// BUG 620: Narrowing a 64-bit index to `int` can turn a large positive
//          offset into a negative subscript.
// ============================================================================
void bug620_narrow_index_cast(unsigned long long big) {
  int arr[8] = {0};
  int idx = (int)big; // BUG: values > INT_MAX become negative.
  // SAFE FIX: Keep the index as size_t and verify it against the array length.
  if (idx >= 0 && idx < 8)
    (void)arr[idx];
}

// ============================================================================
// BUG 621: strtol with base 0 interprets a leading-zero string as octal.
// ============================================================================
void bug621_strtol_base0_octal(void) {
  const char *text = "010";
  long v = strtol(text, NULL, 0); // BUG: returns 8, not 10.
  // SAFE FIX: Pass base 10 for decimal input, or validate the prefix.
  (void)v;
}

// ============================================================================
// BUG 622: strtol with base 0 fails on a bare hex string with no 0x prefix.
// ============================================================================
void bug622_strtol_base16_missing_prefix(void) {
  const char *text = "FF";
  long v = strtol(text, NULL, 0); // BUG: returns 0; base 0 needs 0x.
  // SAFE FIX: Pass base 16 explicitly when the input is bare hexadecimal.
  (void)v;
}

// ============================================================================
// BUG 623: atoi on an overflowing decimal string has undefined behaviour and
//          provides no ERANGE indication.
// ============================================================================
void bug623_atoi_overflow(void) {
  const char *text = "99999999999999999999";
  int v = atoi(text); // BUG: out-of-range; no error reporting.
  // SAFE FIX: Use strtol and check errno == ERANGE and the end pointer.
  (void)v;
}

// ============================================================================
// BUG 624: atoi silently returns 0 for a non-numeric string.
// ============================================================================
void bug624_atoi_non_numeric(void) {
  const char *text = "not-a-number";
  int v = atoi(text); // BUG: indistinguishable from a real zero.
  // SAFE FIX: Use strtol and verify endptr != text.
  (void)v;
}

int main(void) { return 0; }
