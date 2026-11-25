/* NEGATIVE fixed bounds: compile-time constant index beyond N-1. */
int main(void) { int a[3] = {1, 2, 3}; a[5] = 9; return a[5]; }
