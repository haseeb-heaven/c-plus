/* Unsafe: null dereference through a callee. STRICT must reject. */
static void deref(int *p) { *p = 1; }
int main(void) { deref(0); return 0; }
