/* bugs49.cp - enum range, implicit switch fallthrough, do/while(0) macros,
 * goto across VLAs, compound-literal lifetime and flexible-array copies.
 *
 * IDs 755-764: values outside the enumerator set, missing break statements,
 *              control flow escaping a do{...}while(0) wrapper, jumping into
 *              the scope of a VLA, and objects copied without their payload.
 *
 * These are compile-time negative fixtures.  main() deliberately does NOT
 * call any buggy function: the catalog documents the pattern, it is not a
 * runnable crash demo.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// 755. Casting an arbitrary integer into an enum leaves its valid range.
// ============================================================================
enum Bug755Color { BUG755_RED = 0, BUG755_GREEN = 1, BUG755_BLUE = 2 };

void bug755_enum_out_of_range_cast(void) {
  // BUG 755: the value 42 is not a member of enum Bug755Color.  Converting an
  // out-of-range integer to an enum is implementation-defined and comparisons
  // against enumerators may not behave as expected.
  enum Bug755Color c = (enum Bug755Color)42;
  // SAFE FIX: validate the integer before converting; use a plain int when
  // the value can legitimately fall outside the enumerator set.
  (void)c;
}

// ============================================================================
// 756. Enum produced by arithmetic overflows the enumerator range.
// ============================================================================
void bug756_enum_arithmetic_out_of_range(void) {
  // BUG 756: BUG755_BLUE + 10 is outside the declared enumerator set, so the
  // stored enum no longer represents a defined named value.
  enum Bug755Color c = (enum Bug755Color)(BUG755_BLUE + 10);
  // SAFE FIX: keep the arithmetic in an int and range-check before casting.
  (void)c;
}

// ============================================================================
// 757. Missing break makes case 1 fall through into case 2.
// ============================================================================
void bug757_switch_fallthrough_double_action(int code) {
  // BUG 757: case 1 deliberately falls through to case 2 because there is no
  // break, so handling "one" also executes the "two" branch.
  switch (code) {
  case 1:
    printf("one\n");
  case 2:
    printf("two\n");
    break;
  default:
    break;
  }
  // SAFE FIX: add `break;` at the end of case 1, or use an explicit
  // `/* fall through */` comment when the fallthrough is intentional.
}

// ============================================================================
// 758. Fallthrough into a branch that frees the same resource twice.
// ============================================================================
void bug758_switch_fallthrough_double_free(int code) {
  char *buf = (char *)malloc(8);
  if (!buf)
    return;
  // BUG 758: the absent break in case 1 lets case 2 run as well, freeing buf
  // twice through two distinct paths in the same switch.
  switch (code) {
  case 1:
    free(buf);
  case 2:
    free(buf);
    break;
  default:
    break;
  }
  // SAFE FIX: `break;` after the first free, and set buf = NULL after freeing.
}

// ============================================================================
// 759. `break` inside a do{...}while(0) macro exits the macro, not the loop.
// ============================================================================
#define BUG759_SKIP_IF_NEG(x)                                                  \
  do {                                                                         \
    if ((x) < 0)                                                               \
      break;                                                                   \
  } while (0)

void bug759_macro_break_wrong_loop(void) {
  int i;
  int hits = 0;
  // BUG 759: the break only leaves the macro's own do-while, so the enclosing
  // for-loop keeps iterating and `hits` is incremented for negative values
  // too.  A reader expecting the break to leave the for-loop is wrong.
  for (i = -1; i < 2; ++i) {
    BUG759_SKIP_IF_NEG(i);
    ++hits;
  }
  // SAFE FIX: return a boolean from the macro and use a normal `if`/`continue`
  // in the enclosing loop.
  (void)hits;
}

// ============================================================================
// 760. `continue` inside a do{...}while(0) macro continues the macro loop.
// ============================================================================
#define BUG760_CONTINUE_IF_NEG(x)                                              \
  do {                                                                         \
    if ((x) < 0)                                                               \
      continue;                                                                \
  } while (0)

void bug760_macro_continue_wrong_loop(void) {
  int i;
  int sum = 0;
  // BUG 760: continue applies to the nearest enclosing loop, which is the
  // macro's do-while, not the surrounding for-loop.  The "skip" therefore
  // does not skip the for-loop body as intended.
  for (i = -1; i < 2; ++i) {
    BUG760_CONTINUE_IF_NEG(i);
    sum += i;
  }
  // SAFE FIX: make the macro side-effect-free and test its result in the
  // enclosing loop: `if (is_negative(i)) continue;`.
  (void)sum;
}

// ============================================================================
// 761. Forward goto jumps into the scope of a VLA.
// ============================================================================
void bug761_goto_into_vla_scope(int n) {
  // BUG 761: jumping to `skip` enters the scope of `int a[n]` without running
  // its declaration.  The array's lifetime and size are undefined:
  //   goto skip;
  //   int a[n];
  // skip: printf("%zu\n", sizeof a);
  // The C+ checker rejects the VLA with [CPLUS-OWNER].
  // SAFE FIX: declare the array before any jump into its scope, or avoid goto
  // and use structured control flow / a managed allocation.
  printf("jump target\n");
}

// ============================================================================
// 762. Backward goto re-enters a VLA declaration from after its scope.
// ============================================================================
void bug762_goto_reenters_vla_scope(int n) {
  int attempts = 0;
  // BUG 762: on the second pass the backward goto crosses the VLA
  // declaration again while a pointer saved from the first pass is still
  // around; the old VLA storage is already dead:
  //   again:
  //     if (attempts == 1) use(saved);
  //     int a[n]; saved = a; attempts++; goto again;
  // The C+ checker rejects the VLA with [CPLUS-OWNER].
  // SAFE FIX: allocate once outside the loop with a fixed, checked extent.
  (void)attempts;
}

// ============================================================================
// 763. Compound-literal address cached in longer-lived static storage.
// ============================================================================
static const int *bug763_static_cache;

void bug763_compound_literal_static_escape(void) {
  // BUG 763: a compound literal in block scope has automatic lifetime; once
  // this function returns, bug763_static_cache points to dead stack storage.
  bug763_static_cache = &(const int){42};
  // SAFE FIX: copy the value into static storage, or use an allocation whose
  // lifetime matches the cache.
  (void)bug763_static_cache;
}

// ============================================================================
// 764. Flexible array member lost when the containing struct is copied.
// ============================================================================
struct Bug764Flex {
  size_t n;
  unsigned char payload[];
};

void bug764_flexible_array_copy_by_value(void) {
  struct Bug764Flex *p = (struct Bug764Flex *)malloc(sizeof(*p) + 16);
  if (!p)
    return;
  p->n = 16;
  // BUG 764: a by-value copy of a struct with a flexible array member copies
  // only the header; the trailing payload bytes are not part of the object
  // and are silently dropped.
  struct Bug764Flex copy = *p;
  // SAFE FIX: allocate a new object and memcpy the header plus `n` payload
  // bytes explicitly with a bound check.
  (void)copy;
  free(p);
}

int main(void) { return 0; }
