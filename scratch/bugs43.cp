// C+ bug catalog 43 - bug IDs 695-704
// Theme: API-contract / FFI mistakes - pointer contracts, aliasing and
// undefined behaviour at the boundary between C and its library APIs.
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helpers with restrict-qualified parameters (bug 701). */
static void scale701(double *restrict out, const double *restrict in, int n) {
  for (int i = 0; i < n; i++)
    out[i] = in[i] * 2.0;
}

/* Helper that mutates through a pointer (bug 700). */
static void overwrite700(char *p) { *p = 'z'; }

// ============================================================================
// 695. Type-punned read of a double as an integer in a logging helper
// ============================================================================
void bug695_type_pun_log(void) {
  double reading = 42.5;
  // BUG 695: the double object is inspected through a uint64_t lvalue, which
  //          breaks the effective-type rule.
  uint64_t raw = *(uint64_t *)&reading;
  printf("raw=%llx\n", (unsigned long long)raw);
  // SAFE FIX: memcpy into a uint64_t object before formatting it.
}

// ============================================================================
// 696. Relational comparison of pointers into different objects
// ============================================================================
void bug696_relational_ptr_cmp(void) {
  int left[2] = {0}, right[2] = {0};
  // BUG 696: comparing pointers into distinct arrays with < is undefined.
  if (left < right)
    printf("left first\n");
  // SAFE FIX: compare indices or use == / != when objects are unrelated.
}

// ============================================================================
// 697. Pointer arithmetic more than one past the end of an array
// ============================================================================
void bug697_pointer_past_end(void) {
  int cells[3] = {1, 2, 3};
  // BUG 697: cells+4 is beyond one-past-the-end; dereferencing it is UB.
  int *bad = cells + 4;
  printf("%d\n", *bad);
  // SAFE FIX: keep the index within [0, count] and check before dereferencing.
}

// ============================================================================
// 698. Empty infinite loop waiting for an external condition
// ============================================================================
void bug698_empty_infinite_loop(void) {
  // BUG 698: a loop with no observable side effect may be optimized away.
  while (1) {
  }
  // SAFE FIX: give the loop a volatile/atomic condition or a real wait.
}

// ============================================================================
// 699. fflush(stdin) used to discard pending input
// ============================================================================
void bug699_fflush_stdin(void) {
  // BUG 699: fflush on an input stream is undefined behaviour in ISO C.
  fflush(stdin);
  // SAFE FIX: read and discard characters explicitly up to a newline.
}

// ============================================================================
// 700. Casting away const inside a mutating helper call
// ============================================================================
void bug700_const_helper(void) {
  const char literal[] = "abc";
  // BUG 700: overwrite700 writes through a pointer derived from a const object.
  overwrite700((char *)literal);
  // SAFE FIX: copy the string into a mutable buffer before calling the helper.
}

// ============================================================================
// 701. restrict violation in a scaling routine
// ============================================================================
void bug701_restrict_scale(void) {
  double samples[3] = {1.0, 2.0, 3.0};
  // BUG 701: out and in overlap (both start at samples), violating restrict.
  scale701(samples, samples, 3);
  // SAFE FIX: use separate arrays or a routine without restrict on both args.
}

// ============================================================================
// 702. sizeof(pointer) used as the allocation size for a struct
// ============================================================================
void bug702_sizeof_ptr_alloc(void) {
  struct Node {
    int key;
    char name[32];
  } *node;
  // BUG 702: sizeof(node) is the pointer width, not sizeof(struct Node).
  node = (struct Node *)malloc(sizeof(node));
  // SAFE FIX: allocate sizeof(*node) or sizeof(struct Node).
  free(node);
}

// ============================================================================
// 703. Overlapping memcpy in a right-shift helper
// ============================================================================
void bug703_memcpy_shift(void) {
  unsigned char bytes[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  // BUG 703: destination begins inside the source range; memcpy is undefined.
  memcpy(bytes + 1, bytes, 7);
  // SAFE FIX: use memmove for potentially overlapping ranges.
}

// ============================================================================
// 704. Passing a plain char to isdigit()
// ============================================================================
void bug704_ctype_char(void) {
  char input = (char)0x80;
  // BUG 704: negative plain chars are outside isdigit's valid input domain.
  if (isdigit(input))
    printf("digit\n");
  // SAFE FIX: isdigit((unsigned char)input).
}

// ============================================================================
// Entry point: intentionally does not call any of the bug functions above.
// ============================================================================
int main(void) { return 0; }
