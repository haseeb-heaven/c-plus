/* STRICT must reject a non-local jump that skips a live owner's destructor.
 * Plain setjmp/longjmp with no C+ owner remains valid C. */
#include <setjmp.h>
#include <stdio.h>
resource struct R { int id; };
void R_drop(R *r) { printf("drop %d\n", r->id); r->id = 0; }
static jmp_buf jb;
static void escape(void) { longjmp(jb, 1); }
int main(void) {
  if (setjmp(jb) == 0) {
    R a = {7};
    escape();
    R_drop(&a);
  }
  printf("after\n");
  return 0;
}
