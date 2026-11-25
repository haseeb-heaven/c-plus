// C+ bug catalog 40 - bug IDs 665-674
// Theme: API-contract / FFI mistakes - sizeof/ABI assumptions, function
// pointer signatures, variadic format attributes and size_t parameters.
#define _POSIX_C_SOURCE 200809L
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper for bug665: an innocent function whose address is taken. */
static int score665(int x) { return x + 1; }

/* Helper for bug668/672: a printf-style wrapper lacking a format attribute. */
static void log668(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
}

/* Helper for bug669: an out-parameter that writes a size_t. */
static void store669(size_t *slot) { *slot = 64; }

// ============================================================================
// 665. Wrong sizeof: function-pointer size used as a data buffer size
// ============================================================================
void bug665_sizeof_fnptr_alloc(void) {
  int (*handler)(int) = score665;
  // BUG 665: sizeof(handler) is the size of a code pointer, not of the
  //          int array the buffer is meant to hold.
  void *buffer = malloc(sizeof(handler));
  // SAFE FIX: allocate sizeof(int) * count and keep the count explicitly.
  free(buffer);
}

// ============================================================================
// 666. sizeof(pointer) passed as the length of a memset
// ============================================================================
void bug666_sizeof_ptr_memset(void) {
  unsigned char *block = (unsigned char *)malloc(256);
  // BUG 666: sizeof(block) is the pointer width (8), not the 256-byte block.
  memset(block, 0, sizeof(block));
  // SAFE FIX: pass the allocated element count (256) as the length.
  free(block);
}

// ============================================================================
// 667. Function pointer cast to an incompatible signature
// ============================================================================
void bug667_fnptr_sig_cast(void) {
  // BUG 667: score665 takes int and returns int; calling it through a pointer
  //          to a double(double) function mispasses arguments and results.
  double (*mismatch)(double) = (double (*)(double))&score665;
  (void)mismatch(1.5);
  // SAFE FIX: use the exact declared function pointer type.
}

// ============================================================================
// 668. Variadic wrapper without __attribute__((format))
// ============================================================================
void bug668_variadic_no_format_attr(void) {
  // BUG 668: log668 has no format(printf,1,2) attribute, so the compiler
  //          cannot check its arguments; a wrong format silently misfires.
  log668("%s %d\n", "value", 42);
  // SAFE FIX: declare
  //   __attribute__((format(printf, 1, 2))) static void log668(...);
}

// ============================================================================
// 669. Passing int* where a size_t* out-parameter is expected
// ============================================================================
void bug669_intptr_to_sizetp(void) {
  int n = 0;
  // BUG 669: store669 writes a full size_t through a pointer to a 4-byte int.
  store669((size_t *)&n);
  // SAFE FIX: declare 'n' as size_t and pass &n.
}

// ============================================================================
// 670. Struct layout assumption across an ABI boundary
// ============================================================================
void bug670_abi_struct_assume(void) {
  struct Wire {
    uint8_t kind;
    uint32_t id;
  } msg = {1, 7};
  // BUG 670: assuming 'id' sits at byte offset 1 ignores tail/interior
  //          padding chosen by the platform ABI.
  const unsigned char *raw = (const unsigned char *)&msg;
  uint32_t id;
  memcpy(&id, raw + 1, sizeof id);
  // SAFE FIX: use offsetof(struct Wire, id) or an explicit wire format.
  (void)id;
}

// ============================================================================
// 671. Wrong printf length modifier
// ============================================================================
void bug671_printf_wrong_length(void) {
  int small = 3;
  // BUG 671: %lld promises a long long but 'small' is an int.
  printf("%lld\n", small);
  // SAFE FIX: use %d for int, or cast the value to long long.
}

// ============================================================================
// 672. %zu used with a non-size_t argument
// ============================================================================
void bug672_zu_wrong_type(void) {
  int count = 12;
  // BUG 672: %zu requires size_t; an int is passed through a varargs slot.
  log668("count=%zu\n", count);
  // SAFE FIX: cast to size_t: (size_t)count.
}

// ============================================================================
// 673. memcpy of a padded struct into a hash input
// ============================================================================
void bug673_memcpy_padding_hash(void) {
  struct Record {
    char tag;
    uint32_t seq;
  } a = {'k', 9}, b = {'k', 9};
  // BUG 673: padding bytes between tag and seq are indeterminate; hashing
  //          the copied bytes gives unstable results across runs/ABIs.
  struct Record copy;
  memcpy(&copy, &a, sizeof copy);
  unsigned h = 0;
  const unsigned char *p = (const unsigned char *)&copy;
  for (size_t i = 0; i < sizeof copy; i++)
    h = h * 31u + p[i];
  printf("%u %u\n", h, (unsigned)memcmp(&a, &b, sizeof a));
  // SAFE FIX: hash each field explicitly, never the struct padding.
}

// ============================================================================
// 674. GNU void* pointer arithmetic
// ============================================================================
void bug674_void_ptr_arith(void) {
  void *base = malloc(16);
  // BUG 674: pointer arithmetic on void* is a GNU extension; its scale is
  //          implementation-defined and it is not portable ISO C.
  void *slot = base + 4;
  *(int *)slot = 1;
  // SAFE FIX: cast to a typed pointer first, e.g. ((char *)base) + 4.
  free(base);
}

// ============================================================================
// Entry point: intentionally does not call any of the bug functions above.
// ============================================================================
int main(void) { return 0; }
