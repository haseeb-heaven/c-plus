/* Unsafe: shift count equals width. STRICT must reject. */
int main(void) { int x = 1; return x << 32; }
