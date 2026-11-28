/* POSITIVE function boundary: caller passes a provably non-null address. */
static void deref(int *p) { *p = 1; }
int main(void) { int x = 0; deref(&x); return x == 1 ? 0 : 1; }
