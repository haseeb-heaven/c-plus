/* NEGATIVE join: one arm of the phi is outside the callee's bounds. */
static void wr(int *p, int c) { int i = c ? 1 : 6; p[i] = 1; }
int main(void) { int a[4] = {0}; wr(a, 0); return a[0]; }
