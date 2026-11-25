// C+ bug catalog 39 - bug IDs 655-664
// Theme: API-contract / FFI mistakes - aliasing, const-cast writes and
// type-punning across a C ABI boundary.
#define _POSIX_C_SOURCE 200809L
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Helper used by bug655: a routine whose contract promises non-aliasing.    */
/* ------------------------------------------------------------------------- */
static void add655(int *restrict dst, const int *restrict src, int n) {
  for (int i = 0; i < n; i++)
    dst[i] += src[i];
}

/* ------------------------------------------------------------------------- */
/* Helper used by bug661: a custom copy loop with restrict pointers.         */
/* ------------------------------------------------------------------------- */
static void copy661(uint32_t *restrict dst, const uint32_t *restrict src,
                    int n) {
  for (int i = 0; i < n; i++)
    dst[i] = src[i];
}

/* ------------------------------------------------------------------------- */
/* Helpers used by bug664: an API documented to mutate through its pointer.  */
/* ------------------------------------------------------------------------- */
static void touch664(int *slot) { *slot += 1; }

static void from_void664(void *slot) { *(int *)slot = 7; }

// ============================================================================
// 655. restrict violation: overlapping restrict arguments
// ============================================================================
void bug655_restrict_alias_add(void) {
  int values[4] = {1, 2, 3, 4};
  // BUG 655: add655() promises dst and src never alias, but dst+1 overlaps src.
  add655(values + 1, values, 3);
  // SAFE FIX: use disjoint buffers or a routine without restrict when overlap
  //           is possible.
}

// ============================================================================
// 656. Casting away const to write the original object
// ============================================================================
void bug656_const_cast_write(void) {
  const int limit = 100;
  // BUG 656: 'limit' is genuinely const; writing through the cast is UB.
  int *mutable = (int *)&limit;
  *mutable = 200;
  printf("%d\n", limit);
  // SAFE FIX: copy into a mutable variable, or declare the object non-const.
}

// ============================================================================
// 657. Strict-aliasing violation: float read through uint32_t*
// ============================================================================
void bug657_float_bits_pun(void) {
  float value = 2.5f;
  // BUG 657: a float object cannot legally be read through a uint32_t lvalue.
  uint32_t bits = *(uint32_t *)&value;
  printf("%08x\n", bits);
  // SAFE FIX: memcpy(&bits, &value, sizeof bits) into a uint32_t object.
}

// ============================================================================
// 658. Passing int* where size_t* is expected, then writing
// ============================================================================
void bug658_intptr_as_sizetp(void) {
  int count = 4;
  // BUG 658: the callee writes a size_t through a pointer to an int object.
  size_t *out = (size_t *)&count;
  *out = 4096;
  // SAFE FIX: declare 'count' as size_t and pass &count directly.
}

// ============================================================================
// 659. Reading the inactive member of a union (type punning)
// ============================================================================
void bug659_union_inactive_read(void) {
  union Number {
    float f;
    uint32_t u;
  } num;
  num.f = 3.5f;
  // BUG 659: reading num.u reads the wrong effective type in C.
  uint32_t raw = num.u;
  printf("%u\n", raw);
  // SAFE FIX: copy the bytes with memcpy into a separate uint32_t variable.
}

// ============================================================================
// 660. Casting away const through a void* intermediate
// ============================================================================
void bug660_const_via_voidp(void) {
  const char label[] = "read-only";
  // BUG 660: the void* round-trip hides the const cast; the following write
  //          still modifies a genuinely const object.
  void *raw = (void *)label;
  ((char *)raw)[0] = 'X';
  // SAFE FIX: build a mutable copy (e.g. a char array) before modifying.
}

// ============================================================================
// 661. restrict violation in a custom word-copy routine
// ============================================================================
void bug661_restrict_copy_alias(void) {
  uint32_t words[4] = {1, 2, 3, 4};
  // BUG 661: dst (words+1) and src (words) overlap across the restrict boundary.
  copy661(words + 1, words, 3);
  // SAFE FIX: pass non-overlapping ranges or drop restrict on both parameters.
}

// ============================================================================
// 662. Strict-aliasing violation: double read through uint64_t*
// ============================================================================
void bug662_double_to_u64_pun(void) {
  double d = 1.0 / 3.0;
  // BUG 662: the double object is read through a uint64_t lvalue.
  uint64_t bits = *(uint64_t *)&d;
  printf("%llx\n", (unsigned long long)bits);
  // SAFE FIX: memcpy into a uint64_t object to inspect the representation.
}

// ============================================================================
// 663. Struct type-punning: pointer cast to an incompatible layout
// ============================================================================
void bug663_struct_type_pun(void) {
  struct Small {
    char tag;
    int value;
  } s = {'A', 17};
  // BUG 663: reading 'code' through a pointer to an unrelated struct reads
  //          bytes outside the effective type of the object.
  struct Other {
    uint64_t code;
  } *o = (struct Other *)&s;
  printf("%llx\n", (unsigned long long)o->code);
  // SAFE FIX: memcpy the shared prefix into a properly typed object.
}

// ============================================================================
// 664. Casting away const when calling a mutating API
// ============================================================================
void bug664_const_api_mutate(void) {
  const int frozen = 5;
  // BUG 664: touch664() and from_void664() both write through their pointer;
  //          passing a const object violates the API contract.
  touch664((int *)&frozen);
  from_void664((void *)&frozen);
  // SAFE FIX: pass a mutable copy and copy the result back explicitly.
}

// ============================================================================
// Entry point: intentionally does not call any of the bug functions above.
// ============================================================================
int main(void) { return 0; }
