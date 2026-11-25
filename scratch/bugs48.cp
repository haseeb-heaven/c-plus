/* bugs48.cp - sequence points, evaluation order and macro double-evaluation.
 *
 * IDs 745-754: self-assignment through ++, multiple unsequenced modifications,
 *              unsequenced function arguments, double-evaluating macros and
 *              side effects hidden inside sizeof of a variable-length array.
 *
 * These are compile-time negative fixtures.  main() deliberately does NOT
 * call any buggy function: the catalog documents the pattern, it is not a
 * runnable crash demo.  clang's -Wunsequenced is suppressed locally so the
 * buggy expression stays visible; expressions rejected by the C+ checker
 * itself are shown in comments with the compiled safe equivalent below.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// 745. `i = i++` writes i twice between sequence points (undefined behavior).
// ============================================================================
void bug745_self_assign_postinc(void) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsequenced"
  // BUG 745: the ++ side effect and the assignment are unsequenced, so the
  // stored value is unspecified and the program has undefined behavior.
  int i = 0;
  i = i++;
#pragma clang diagnostic pop
  // SAFE FIX: a single modification: `i = i + 1;` or `i += 1;`.
  (void)i;
}

// ============================================================================
// 746. `i = ++i + 1` combines a pre-increment with an assignment to i.
// ============================================================================
void bug746_assign_preinc_plus_one(void) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsequenced"
  // BUG 746: two modifications of i without a sequence point; the order of
  // the ++ and the assignment is unspecified.
  int i = 1;
  i = ++i + 1;
#pragma clang diagnostic pop
  // SAFE FIX: compute into a separate variable, then assign once.
  (void)i;
}

// ============================================================================
// 747. Unsequenced modifications passed as two function arguments.
// ============================================================================
static int bug747_add(int a, int b) { return a + b; }

void bug747_unsequenced_call_args(void) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsequenced"
  // BUG 747: argument evaluation order is unspecified, so `i++` may be
  // evaluated in either order and the result of add() is not deterministic.
  int i = 0;
  int r = bug747_add(i++, i++);
#pragma clang diagnostic pop
  // SAFE FIX: increment in separate full expressions before the call.
  (void)r;
}

// ============================================================================
// 748. Index expression and indexed element modified in one full expression.
// ============================================================================
void bug748_index_and_element_unsequenced(void) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsequenced"
  // BUG 748: `a[i] = i++` reads i for the index while modifying i in the
  // right operand; the read and write are unsequenced.
  int a[4] = {0, 0, 0, 0};
  int i = 0;
  a[i] = i++;
#pragma clang diagnostic pop
  // SAFE FIX: use `a[i] = 1; i++;` so there is a sequence point between them.
  (void)a;
}

// ============================================================================
// 749. Unsequenced increments in printf varargs.
// ============================================================================
void bug749_unsequenced_printf_args(void) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsequenced"
  // BUG 749: the two `i++` argument evaluations are unsequenced relative to
  // each other; which value prints first is unspecified.
  int i = 0;
  printf("%d %d\n", i++, i++);
#pragma clang diagnostic pop
  // SAFE FIX: print the old and new values from distinct expressions.
}

// ============================================================================
// 750. Macro `MAX(a, b)` evaluates its argument twice.
// ============================================================================
#define BUG750_MAX(a, b) ((a) > (b) ? (a) : (b))

void bug750_max_macro_double_eval(void) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsequenced"
  // BUG 750: `i++` appears twice in the expansion, so i is incremented once
  // for the comparison and again for the selected branch.
  int i = 0;
  int j = 2;
  int m = BUG750_MAX(i++, j);
#pragma clang diagnostic pop
  // SAFE FIX: increment first, then pass plain variables to the macro.
  (void)m;
}

// ============================================================================
// 751. Macro `SQUARE(x)` multiplies x by itself and evaluates x twice.
// ============================================================================
#define BUG751_SQUARE(x) ((x) * (x))

void bug751_square_macro_double_eval(void) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsequenced"
  // BUG 751: `SQUARE(i++)` expands to `(i++) * (i++)`, modifying i twice in
  // one expression without a sequence point.
  int i = 2;
  int s = BUG751_SQUARE(i++);
#pragma clang diagnostic pop
  // SAFE FIX: `int v = i++; int s = v * v;`.
  (void)s;
}

// ============================================================================
// 752. `sizeof(a[n++])` evaluates the VLA size expression at run time.
// ============================================================================
void bug752_sizeof_vla_increments_index(int n) {
  // BUG 752: when the operand is a VLA, sizeof is a runtime expression and
  // the `n++` side effect really happens.  The C+ checker rejects this:
  //   int a[n]; size_t bytes = sizeof(a[n++]);
  // SAFE FIX: compute the extent separately and use a fixed-size array or
  // cplus-managed allocation.
  int a[4] = {0, 0, 0, 0};
  size_t bytes = sizeof a;
  printf("%zu %d\n", bytes, n);
}

// ============================================================================
// 753. Function-call side effect hidden in the size of a VLA.
// ============================================================================
static int bug753_tick(int *counter) { return ++*counter; }

void bug753_sizeof_vla_calls_function(int n) {
  // BUG 753: `sizeof(a[bug753_tick(&n)])` runs tick() in the sizeof operand;
  // the call is an observable side effect that a compile-time sizeof would
  // not have.  Rejected by the C+ checker as a VLA-sizeof expression.
  // SAFE FIX: call tick() in its own statement and use a fixed extent.
  int a[4] = {0, 0, 0, 0};
  (void)bug753_tick;
  printf("%zu\n", sizeof a);
}

// ============================================================================
// 754. A VLA sizeof used directly as an allocation size.
// ============================================================================
void bug754_vla_sizeof_as_alloc_size(int n) {
  // BUG 754: `malloc(sizeof(int[n++]))` evaluates n++ at run time while
  // deciding the allocation size, so the counter advances as a side effect
  // of what looks like a constant expression.  Rejected by the C+ checker.
  // SAFE FIX: compute the byte count explicitly: `size_t bytes = (size_t)n
  // * sizeof(int); void *p = cplus_alloc(bytes);`.
  size_t bytes = (size_t)n * sizeof(int);
  void *p = malloc(bytes);
  free(p);
}

int main(void) { return 0; }
