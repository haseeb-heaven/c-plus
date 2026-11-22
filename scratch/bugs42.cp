// C+ bug catalog 42 - bug IDs 685-694
// Theme: API-contract / FFI mistakes - printf format attributes, length
// modifiers, variadic mismatches and memcpy/ctype contracts.
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A variadic wrapper with no format attribute (bugs 686/692). */
static void emit686(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}

/* A variadic reader that fetches the wrong type (bug 694). */
static int sum694(int count, ...) {
  va_list ap;
  va_start(ap, count);
  int total = 0;
  for (int i = 0; i < count; i++)
    total += (int)va_arg(ap, double);
  va_end(ap);
  return total;
}

// ============================================================================
// 685. Untrusted format string passed directly to printf
// ============================================================================
void bug685_untrusted_format(int argc, char **argv) {
  // BUG 685: a caller-controlled string is interpreted as a printf format;
  //          %n or %s can read or write arbitrary memory.
  if (argc > 1)
    printf(argv[1]);
  // SAFE FIX: printf("%s", argv[1]) so the input is data, not a format.
}

// ============================================================================
// 686. Variadic wrapper missing __attribute__((format))
// ============================================================================
void bug686_missing_format_attr(void) {
  // BUG 686: without format(printf,1,2) the compiler cannot validate calls to
  //          emit686, so a bad format/argument pair goes unnoticed.
  emit686("%s %d\n", "x", 1);
  // SAFE FIX: annotate the declaration with the format attribute.
}

// ============================================================================
// 687. Wrong printf length modifier
// ============================================================================
void bug687_wrong_length(void) {
  int small = 7;
  // BUG 687: %lu reads a long unsigned from an int varargs slot.
  printf("%lu\n", small);
  // SAFE FIX: use %d for int, or cast explicitly to unsigned long.
}

// ============================================================================
// 688. %zu paired with a signed int
// ============================================================================
void bug688_zu_wrong_type(void) {
  int amount = -3;
  // BUG 688: %zu expects size_t; a negative int becomes a huge unsigned value.
  printf("amount=%zu\n", amount);
  // SAFE FIX: cast to size_t after validating the value, or use %d.
}

// ============================================================================
// 689. Hashing a memcpy'd struct including its padding bytes
// ============================================================================
void bug689_memcpy_padding_hash(void) {
  struct Entry {
    uint8_t flag;
    uint32_t key;
  } src = {1, 0xABCD}, dst;
  // BUG 689: padding between flag and key is indeterminate; hashing the copied
  //          object can produce different values for identical logical input.
  memcpy(&dst, &src, sizeof dst);
  unsigned h = 0;
  const unsigned char *b = (const unsigned char *)&dst;
  for (size_t i = 0; i < sizeof dst; i++)
    h = h * 33u + b[i];
  printf("%u\n", h);
  // SAFE FIX: hash flag and key individually into a fixed byte buffer.
}

// ============================================================================
// 690. Overlapping memcpy instead of memmove
// ============================================================================
void bug690_memcpy_overlap(void) {
  char text[16] = "abcdef";
  // BUG 690: source and destination ranges overlap, which memcpy forbids.
  memcpy(text + 2, text, 4);
  printf("%s\n", text);
  // SAFE FIX: use memmove when the ranges may overlap.
}

// ============================================================================
// 691. Passing a plain (possibly negative) char to <ctype.h>
// ============================================================================
void bug691_ctype_plain_char(void) {
  char c = (char)0xFF;
  // BUG 691: negative char values are not valid for isalpha(); its argument
  //          must be EOF or an unsigned char value.
  if (isalpha(c))
    printf("letter\n");
  // SAFE FIX: cast to unsigned char: isalpha((unsigned char)c).
}

// ============================================================================
// 692. Wrapper forwarding a non-literal format to vprintf
// ============================================================================
void bug692_nonliteral_vprintf(int argc, char **argv) {
  // BUG 692: the wrapper is a chain that turns caller input into a format
  //          string; with no format attribute nothing checks it at compile time.
  if (argc > 1)
    emit686(argv[1]);
  // SAFE FIX: make the wrapper take a literal format and pass data separately.
}

// ============================================================================
// 693. sprintf into a fixed buffer without a bound
// ============================================================================
void bug693_sprintf_no_bound(void) {
  char buf[8];
  // BUG 693: the formatted text can exceed 8 bytes and overflow the stack.
  sprintf(buf, "%d-%s", 12345, "a-long-suffix");
  // SAFE FIX: snprintf(buf, sizeof buf, ...) and check the return value.
}

// ============================================================================
// 694. va_arg type mismatch against the actual promoted argument
// ============================================================================
void bug694_va_arg_mismatch(void) {
  // BUG 694: sum694 fetches a double for every argument, but the caller
  //          supplies ints; reading a double from an int varargs slot is UB.
  printf("%d\n", sum694(3, 1, 2, 3));
  // SAFE FIX: fetch the exact promoted type the caller passed (int here).
}

// ============================================================================
// Entry point: intentionally does not call any of the bug functions above.
// ============================================================================
int main(void) { return 0; }
