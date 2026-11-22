/* bugs50.cp - type punning, _Generic, static assertions, designated
 * initializers, implicit declarations and varargs without a prototype.
 *
 * IDs 765-774: reading/writing an object through an incompatible type,
 *              generic selections with no matching association, assertions
 *              that assert the wrong thing, duplicated designated fields,
 *              undeclared functions and unchecked variadic calls.
 *
 * These are compile-time negative fixtures.  main() deliberately does NOT
 * call any buggy function: the catalog documents the pattern, it is not a
 * runnable crash demo.  Expressions rejected by the compiler or the C+
 * checker are shown in comments with a safe compiled equivalent.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ============================================================================
// 765. Reading a float's bits through a uint32_t lvalue (strict aliasing).
// ============================================================================
void bug765_type_punned_float_read(void) {
  // BUG 765: `* (uint32_t *)&f` reads the float object through an lvalue of
  // an incompatible type, which violates strict aliasing and is undefined.
  // The C+ checker rejects it with [CPLUS-FFI]:
  //   uint32_t bits = *(uint32_t *)&f;
  // SAFE FIX: copy the object representation with memcpy into a uint32_t.
  float f = 3.14f;
  uint32_t bits = 0;
  memcpy(&bits, &f, sizeof bits);
  printf("0x%x\n", bits);
}

// ============================================================================
// 766. Writing an int object through a float lvalue (strict aliasing).
// ============================================================================
void bug766_type_punned_int_write(void) {
  // BUG 766: storing through `*(float *)&x` where x is an int modifies the
  // object through an incompatible lvalue.  Also rejected as [CPLUS-FFI]:
  //   *(float *)&x = 1.0f;
  // SAFE FIX: keep the declared type, or memcpy a representation only into a
  // suitably typed destination object.
  int x = 7;
  x = 1;
  printf("%d\n", x);
}

// ============================================================================
// 767. _Generic selection with no compatible association.
// ============================================================================
#define BUG767_KIND(x) _Generic((x), int: 1, float: 2)

void bug767_generic_incompatible_type(void) {
  // BUG 767: a double controlling expression matches neither `int` nor
  // `float`, so the generic selection is a hard compile error:
  //   int k = BUG767_KIND((double)1.0);
  // SAFE FIX: add a `default:` association or list the actual type.
  int k = BUG767_KIND(1);
  (void)k;
}

// ============================================================================
// 768. _Generic without default on an unexpected pointer type.
// ============================================================================
#define BUG768_TAG(x) _Generic((x), int: 1, char *: 2)

void bug768_generic_missing_default(void) {
  // BUG 768: passing a `const char *` matches neither association because
  // _Generic requires compatible types (qualifiers matter), so there is no
  // valid selection:
  //   int k = BUG768_TAG("literal");
  // SAFE FIX: add a `default: 0` association for unhandled types.
  int k = BUG768_TAG((char *)0);
  (void)k;
}

// ============================================================================
// 769. _Static_assert that states a false requirement.
// ============================================================================
void bug769_static_assert_false(void) {
  // BUG 769: this assertion is false on the target ABI and makes the whole
  // translation unit fail to compile:
  //   _Static_assert(sizeof(int) == 8, "int must be 8 bytes");
  // SAFE FIX: assert a true, portable invariant such as CHAR_BIT >= 8.
  printf("%zu\n", sizeof(int));
}

// ============================================================================
// 770. Static assertion encoding a layout assumption that does not hold.
// ============================================================================
struct Bug770Pair {
  char c;
  int i;
};

void bug770_static_assert_wrong_layout(void) {
  // BUG 770: assuming no padding between a char and an int is wrong on most
  // ABIs, so this assertion fails:
  //   _Static_assert(offsetof(struct Bug770Pair, i) == 1, "no padding");
  // SAFE FIX: compute the real offset with offsetof and assert that instead.
  printf("%zu\n", sizeof(struct Bug770Pair));
}

// ============================================================================
// 771. A later designated initializer overwrites an earlier field.
// ============================================================================
struct Bug771Point {
  int x;
  int y;
};

void bug771_designated_init_overwrite(void) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winitializer-overrides"
  // BUG 771: `.x = 1` is silently overwritten by the second `.x = 9`, so the
  // intended first value never reaches the object.
  struct Bug771Point p = {.x = 1, .y = 2, .x = 9};
#pragma clang diagnostic pop
  // SAFE FIX: list each designator only once: `{.x = 9, .y = 2}`.
  (void)p;
}

// ============================================================================
// 772. Duplicated designator in a nested struct initializer.
// ============================================================================
struct Bug772Inner {
  int a;
  int b;
};
struct Bug772Outer {
  struct Bug772Inner in;
  int tag;
};

void bug772_nested_designated_overwrite(void) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winitializer-overrides"
  // BUG 772: the second `.a` overrides the first one in the same nested
  // designator list, and `.tag` is left at zero unintentionally.
  struct Bug772Outer o = {.in.a = 3, .in.a = 7, .tag = 1};
#pragma clang diagnostic pop
  // SAFE FIX: one designator per member and initialize every intended field.
  (void)o;
}

// ============================================================================
// 773. Calling a function with no declaration in scope (implicit decl).
// ============================================================================
void bug773_implicit_function_declaration(void) {
  // BUG 773: C99 removed implicit function declarations; calling an
  // undeclared name is a hard error and would otherwise assume an int
  // return and int arguments:
  //   int r = bug773_missing_declaration(5);
  // SAFE FIX: declare the function in a header (or before the call) with its
  // real signature and include that header.
  int r = 0;
  (void)r;
}

// ============================================================================
// 774. Varargs call with no prototype to check argument types.
// ============================================================================
static void bug774_vlog(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  // The callee reads an int, but without a format attribute the compiler
  // cannot verify the actual argument types at the call site.
  int value = va_arg(ap, int);
  va_end(ap);
  (void)value;
}

void bug774_varargs_missing_prototype(void) {
  // BUG 774: `bug774_vlog("%d", 1.0)` passes a double where va_arg reads an
  // int.  A plain function declaration/definition with `...` gives no
  // format checking, so the mismatch compiles and is undefined at runtime.
  // SAFE FIX: annotate the function with
  //   __attribute__((format(printf, 1, 2)))
  // or provide a typed wrapper instead of raw varargs.
  bug774_vlog("%d", 1.0);
}

int main(void) { return 0; }
