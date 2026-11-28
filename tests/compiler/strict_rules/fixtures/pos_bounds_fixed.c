/* POSITIVE fixed bounds: compile-time constant within [0, N). */
int main(void) { int a[3] = {1, 2, 3}; a[2] = 9; return a[2] == 9 ? 0 : 1; }
