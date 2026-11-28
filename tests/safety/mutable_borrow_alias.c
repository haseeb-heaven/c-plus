/* Rust-style exclusive mutable borrow: two &x arguments to a callee that
 * writes through both parameter slots alias the same object. */
void f(int *a, int *b) { *a = 1; *b = 2; }
int main(void) { int x = 0; f(&x, &x); return x; }
