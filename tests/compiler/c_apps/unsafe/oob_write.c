/* Unsafe: constant out-of-bounds write. STRICT must reject. */
int main(void) { int a[4] = {0}; a[7] = 3; return a[7]; }
