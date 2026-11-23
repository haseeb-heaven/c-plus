/* Unsafe: read of uninitialized local. STRICT must reject. */
int main(void) { int x; return x; }
